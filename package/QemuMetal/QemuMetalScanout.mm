// SPDX-License-Identifier: BSD-2-Clause-Patent
// The plugin's scanout thread: it serves the display driver's frames with the GPU.
//
// sheng-fb-gpu=1 makes the display driver offer each finished frame to a privileged client instead of copying it itself
// (drivers/NativeMobileDisplay/NativeGOPPresenter.cpp), and the presenter accepts an offer only while such a client is inside waitSwap.
// This is that client. It is why native_gop_gpu_offers stayed 0 on the boots where the Metal device was open and rendering: every ring from
// the shared window to an open device had a measurement, and the next ring -- a client waiting for a frame -- did not exist.
//
// The calls reach the presenter through a SECOND connection of the accelerator, opened with type 0x4148 (QemuMetalAccelerator.cpp's
// kHandoffOpenType), because the original sandbox profiles admit that class and not the presenter's own service. That connection serves
// nothing else: it takes no slot of the window and writes no owner name, so a slot open's measurement (opens=1, owners="back") still means
// what it meant while a second connection serves the display.
//
// Which external-method selectors a profile admits is decided outside this process, so every call travels through the tunnel
// {magic, method, payload...} on a selector found by probing -- the arrangement sheng's Adreno plugin uses for the same reason
// (drivers/NativeMetal/NativeAdrenoUserTest.h's Gpu::probe). The scanout itself is memory type 0x5343, which the presenter hands out only
// in GPU mode.
//
// A refused offer is not fatal: the display driver falls back to its own copy for that frame and offers again on the next one
// (NativeLegacyDisplay.cpp:205). Only a GPU copy that REPORTS failure latches the driver onto its own path for good
// (finishGpuSwap -> gpuBypass), so this client reports every copy's verdict rather than guessing.
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "GpuScanoutClient.h"
#include "GpuScanoutPresenter.h"
// Plain C++ with no IOKit in it, so the selector search is compilable and
// runnable by the host's test suite as well as by this file. See its own header
// for the bug that is the reason it exists.
#include "QemuMetalSelector.h"

extern "C" void qemu_metal_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

namespace {
constexpr uint32_t kHandoffOpenType = 0x4148;          // 'HA', QemuMetalAccelerator.cpp
constexpr uint32_t kTunnelMagic = 0x4e544e4c;          // 'NTNL'
constexpr uint32_t kProbe = 0xffffffffu;               // the method that only proves a selector was admitted
constexpr uint32_t kScanoutType = 0x5343;              // 'SC', the writable scanout
constexpr uint32_t kMapAnywhereInhibitCache = 0x101;   // kIOMapAnywhere | kIOMapInhibitCache
// The longest payload any hand-off method carries (NGPComplete, 16 bytes); 64 leaves room without inviting a mistake.
constexpr size_t kPayloadMax = 64;

using OpenFn = kern_return_t (*)(mach_port_t, task_port_t, uint32_t, mach_port_t *);
using CallFn = kern_return_t (*)(mach_port_t, uint32_t, const void *, size_t, void *, size_t *);
using MapFn = kern_return_t (*)(mach_port_t, uint32_t, task_port_t, mach_vm_address_t *, mach_vm_size_t *, uint32_t);

// One connection of the accelerator that serves only the display hand-off, plus the selector the sandbox admits for it.
struct Link {
    mach_port_t port = MACH_PORT_NULL;      // the accelerator service the plugin opened
    mach_port_t connection = MACH_PORT_NULL;
    uint32_t selector = 0;
    OpenFn openFn = nullptr;
    CallFn callFn = nullptr;
    MapFn mapFn = nullptr;
};

// {magic, method, payload...} in, the method's own output out. The payload is copied rather than borrowed: the wire records are small and
// a caller's buffer is not guaranteed to outlive the call's trip through the kernel.
//
// The selector is a parameter and not a field read, which is the whole of the
// fix for the bug recorded in QemuMetalSelector.h: the search for an admitted
// selector cannot consult the one field it is trying to fill in. Reading
// ``link.selector`` here made the loop ask the same selector 256 times.
int tunnel(Link &link, uint32_t selector, uint32_t method, const void *in, size_t inSize, void *out, size_t outSize) {
    uint8_t buffer[8 + kPayloadMax] = {};
    if (inSize > kPayloadMax) return -1;
    const uint32_t header[2] = {kTunnelMagic, method};
    memcpy(buffer, header, sizeof header);
    if (inSize) memcpy(buffer + 8, in, inSize);
    size_t produced = outSize;
    return link.callFn(link.connection, selector, buffer, 8 + inSize, out, out ? &produced : nullptr);
}

// The probe's context: the connection to ask, and nothing else. The selector is
// deliberately absent -- see tunnel's note about why.
struct Probe {
    Link *link;
};

// A probe is a tunnel call whose method is 0xffffffff: the driver answers success for any selector the profile admits and refuses every
// other one. There is no other way to learn which ones those are -- the profile is not readable from in here -- and assuming a particular
// selector is admitted is how a hand-off that never gets asked for a frame looks like a working one.
bool probeAdmits(uint32_t selector, void *context) {
    Probe *probe = static_cast<Probe *>(context);
    return tunnel(*probe->link, selector, kProbe, nullptr, 0, nullptr, 0) == 0;
}

// The Transport's context: the connection the three hand-off methods travel through.
struct Scanout {
    Link link;
};

int handoffCall(void *context, uint32_t method, const void *in, size_t inSize, void *out, size_t outSize) {
    Link &link = static_cast<Scanout *>(context)->link;
    return tunnel(link, link.selector, method, in, inSize, out, outSize);
}

int handoffMap(void *context, uint64_t *address, uint64_t *length) {
    Link &link = static_cast<Scanout *>(context)->link;
    if (!link.mapFn || !address || !length) return -1;
    mach_vm_address_t mapped = 0;
    mach_vm_size_t bytes = 0;
    const kern_return_t status = link.mapFn(link.connection, kScanoutType, mach_task_self(), &mapped, &bytes, kMapAnywhereInhibitCache);
    *address = mapped;
    *length = bytes;
    return status;
}

// The whole hand-off, on this thread. Returns after logging why, so the log says which ring is missing rather than only that no frame came.
void serve(id<MTLDevice> device, mach_port_t accelerator) {
    Scanout context;
    Link &link = context.link;
    void *iokit = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_NOW | RTLD_LOCAL);
    if (!iokit) {qemu_metal_log("scanout: IOKit did not open"); return;}
    link.openFn = (OpenFn)dlsym(iokit, "IOServiceOpen");
    link.callFn = (CallFn)dlsym(iokit, "IOConnectCallStructMethod");
    link.mapFn = (MapFn)dlsym(iokit, "IOConnectMapMemory64");
    link.port = accelerator;
    if (!link.openFn || !link.callFn || !link.mapFn) {qemu_metal_log("scanout: IOKit has no open/call/map"); return;}
    // A connection of its own, so serving a frame takes no slot of the window away from the render server.
    const kern_return_t opened = link.openFn(link.port, mach_task_self(), kHandoffOpenType, &link.connection);
    if (opened != KERN_SUCCESS || !link.connection) {
        qemu_metal_log("scanout: the hand-off connection was refused (0x%x)", opened);
        return;
    }
    // The selector the profile admits, found by asking. kNoSelector and
    // selector 0 are different answers, and the caller must be able to tell
    // them apart: a profile that admits selector 0 is a working hand-off, and
    // the broken version of this loop reported it as "nothing is admitted".
    Probe probe = {&link};
    const uint32_t selected = qemu_metal_selector::admittedSelector(probeAdmits, &probe);
    if (selected == qemu_metal_selector::kNoSelector) {
        qemu_metal_log("scanout: the profile admits no external-method selector for this class, so the presenter cannot be asked for a frame");
        return;
    }
    link.selector = selected;
    qemu_metal_log("scanout: hand-off connection on selector %u", link.selector);
    const gpuscanout::Transport transport = {&context, handoffCall, handoffMap, nullptr};
    gpuscanout::Stats stats = {};
    gpuscanout::Client client;
    if (!client.open(device, transport, &stats)) {
        // 2 the mode was refused, 3 the scanout was not mappable or not importable (a guest without sheng-fb-gpu=1 lands here).
        qemu_metal_log("scanout: open stopped at step %d (0x%x)", stats.step, stats.error);
        return;
    }
    qemu_metal_log("scanout: serving frames");
    uint64_t reported = 0, failures = 0;
    @try {
        while (client.serveOne(1000)) {
            if (stats.frames != reported || stats.failures != failures) {
                reported = stats.frames;
                failures = stats.failures;
                qemu_metal_log("scanout: %llu frames, %llu failures, last result %d, %llu verified / %llu differing rows",
                               (unsigned long long)stats.frames, (unsigned long long)stats.failures, stats.lastResult,
                               (unsigned long long)stats.verified, (unsigned long long)stats.mismatchRows);
            }
        }
    } @catch (NSException *exception) {
        // An exception escaping a worker thread aborts the whole host process, and this one runs in the render server (int-075/083).
        qemu_metal_log("scanout: %s", exception.reason.UTF8String ?: "?");
        return;
    }
    qemu_metal_log("scanout: stopped after %llu frames and %llu failures (result %d, 0x%x)", (unsigned long long)stats.frames,
                   (unsigned long long)stats.failures, stats.lastResult, stats.error);
}
}  // namespace

// Started by QemuMetalPlugin.mm once the device exists. On its own thread: a wait for a frame sleeps, and it must not hold up the render
// server's start. The device is retained by the caller for as long as this thread can run.
extern "C" void qemu_metal_serve_scanout(void *device, unsigned accelerator) {
    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attributes, 1u << 20);
    struct Job {
        void *device;
        unsigned accelerator;
    };
    Job *job = new Job{device, accelerator};
    if (pthread_create(&thread, &attributes, [](void *argument) -> void * {
            Job *j = static_cast<Job *>(argument);
            sleep(1);   // let the frontend finish registering before the first frame can arrive
            serve((__bridge id<MTLDevice>)j->device, j->accelerator);
            delete j;
            return nullptr;
        }, job) != 0) {
        delete job;
        qemu_metal_log("scanout: the thread did not start");
    }
    pthread_attr_destroy(&attributes);
}
