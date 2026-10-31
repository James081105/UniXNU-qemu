// SPDX-License-Identifier: BSD-2-Clause-Patent
// The accelerator iPadOS's Metal discovery finds (IOServiceMatching("IOAcceleratorES") -> MetalPluginName ->
// /System/Library/Extensions/QemuMetal.bundle -> initWithAcceleratorPort:), and the connection that gives the plugin its window to the
// Mac: the memory of run-qemu.py --shared-window that SharedWindow.cpp found, which QemuMetalRemote.cpp and qemu-metal-host share.
//
// As sheng's Adreno accelerator (drivers/NativeAdreno/NativeAdrenoAccelerator.cpp), the class derives from the original IOAcceleratorES
// of IOGPUFamily (drivers/NativeMetal/OriginalAcceleratorABI.h, bound to the audited original addresses by
// build-native-legacy-display.py), and is named after Apple's accelerator class. Its static initializer is taken out of __mod_init_func
// and run by QemuMetalPublish.cpp once IOGPUFamily has loaded, since the parent metaclass exists only then. The open grant of backboardd
// is the accelerator's NativeSandboxGrantProcess property (patches/xnu/zzw-native-adreno-sandbox-grant.patch); every other process opens
// it under its own sandbox profile, which admits Metal's connection class: the connection is named AGXDeviceUserClient for that, as sheng's.
//
// Each connection owns one slot of the window (QemuMetalWire.h qm_slot), its own memory to the Mac: the render server slot 0, any other
// process the first free one of the rest. Open type kOpenType; memory type 0 is the slot, which the plugin maps write-back cacheable, as
// the kernel and the Mac do.
//
// A SECOND open type, kHandoffOpenType, carries the display hand-off instead. sheng-fb-gpu=1 makes the display driver offer each finished
// frame to a privileged client instead of copying it (drivers/NativeMobileDisplay/NativeGOPPresenter.cpp), and the presenter accepts an
// offer only while such a client is inside waitSwap. That client is the plugin's scanout thread, and it reaches the presenter through this
// connection because the original sandbox profiles admit this class and not the presenter's own service -- the same arrangement as sheng's
// Adreno accelerator (drivers/NativeAdreno/NativeAdrenoPublish.cpp), which is where these three numbers come from. A hand-off connection
// serves nothing else: it mounts no GPU work, so it takes no slot of the window and writes no owner name. Selectors
// kHandoffBase + the presenter's NGP_METHOD numbers go to native_gop_handoff_call unchanged; memory type kScanoutType is the writable
// scanout, which the presenter hands out only in GPU mode.
#include "OriginalAcceleratorABI.h"
#include "QemuMetalWire.h"
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOUserClient.h>
#include <libkern/OSAtomic.h>
#include <kern/task.h>

extern "C" {
extern volatile uint64_t native_shared_window_base, native_shared_window_size;
extern volatile uint32_t native_shared_window_stage;
volatile uint32_t native_qemu_metal_opens, native_qemu_metal_refusals;
/* The hand-off's own counters, separate from the slot opens because they mean something different: a slot open is a process that got a
 * window, a hand-off open is one connection that can serve frames. */
volatile uint32_t native_qemu_metal_handoff_opens, native_qemu_metal_handoff_calls, native_qemu_metal_handoff_refusals,
                  native_qemu_metal_scanout_maps, native_qemu_metal_handoff_probes, native_qemu_metal_handoff_last_selector,
                  native_qemu_metal_handoff_last_method, native_qemu_metal_handoff_last_status;
volatile char native_qemu_metal_owners[QM_SLOTS][17];   // the process of each slot, for qemu-ps.py --value
IOService *native_qemu_metal_accelerator_allocate(void);
unsigned native_qemu_metal_accelerator_instance_size(void);
IOService *native_qemu_metal_grant_allocate(void);
void proc_selfname(char *buffer, int size);
// The display hand-off's kernel side (drivers/NativeMobileDisplay/NativeGOPPresenter.cpp): the three methods, and the writable scanout.
IOReturn native_gop_handoff_call(uint32_t op, task_t task, const void *in, size_t inSize, void *out, size_t outSize);
IOMemoryDescriptor *native_gop_handoff_memory(void);
}

namespace {
constexpr UInt32 kOpenType = 0x514d;          // 'QM'
// 'HA': a connection that serves only the display hand-off. No slot, no owner name, no GPU work.
constexpr UInt32 kHandoffOpenType = 0x4148;
// 0x7000 + the presenter's NGP_METHOD numbers (NativeGOPWire.h): 0 mode, 1 present (CPU, refused here), 2 wait, 3 complete.
constexpr uint32_t kHandoffBase = 0x7000, kHandoffLast = 0x7003;
// 'SC': the writable firmware scanout, which the presenter hands out only in GPU mode (sheng-fb-gpu=1).
constexpr UInt32 kScanoutType = 0x5343;
// 'NTNL': the tunnel's first word, as sheng's Adreno accelerator (drivers/NativeAdreno/NativeAdrenoPublish.cpp). The sandbox resolves a
// filter policy for this connection class at open time and then admits only the selectors its profile lists -- so which selector numbers
// reach this object is decided outside this driver, and a call that carries its method in the first eight bytes can use ANY admitted one.
// 0xffffffff as the method is the probe that proves a selector was admitted.
constexpr uint32_t kTunnelMagic = 0x4e544e4c, kProbe = 0xffffffffu;
volatile UInt8 gSlots[QM_SLOTS];
}

class AGXAccelerator : public IOAcceleratorES {
    OSDeclareDefaultStructors(AGXAccelerator)
public:
    IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AGXAccelerator, IOAcceleratorES)

class AGXDeviceUserClient : public IOUserClient {
    OSDeclareDefaultStructors(AGXDeviceUserClient)
    int slot = -1;
    bool handoff = false;
    void giveBack();
public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    IOReturn clientClose() APPLE_KEXT_OVERRIDE;
    IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory) APPLE_KEXT_OVERRIDE;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments, IOExternalMethodDispatch *dispatch,
                            OSObject *target, void *reference) APPLE_KEXT_OVERRIDE;
    void setHandoffOnly() {handoff = true;}
};
OSDefineMetaClassAndStructors(AGXDeviceUserClient, IOUserClient)

// A process besides the render server opens a grant of its own instead of the accelerator: the kernel's open path grants a service's
// connection in place of the sandbox verdict to the one process its NativeSandboxGrantProcess names (zzw-native-adreno-sandbox-grant.patch),
// and the profiles of processes such as iconservicesagent refuse AGXDeviceUserClient. No IOAcceleratorES: the discovery never sees it.
// QemuMetalPublish.cpp publishes one for each name run-qemu.py --metal-clients gives.
class QemuMetalGrant : public IOService {
    OSDeclareDefaultStructors(QemuMetalGrant)
public:
    IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(QemuMetalGrant, IOService)

static IOReturn openClient(IOService *provider, task_t owningTask, void *securityID, UInt32 type, IOUserClient **handler)
{
    if (!handler) {native_qemu_metal_refusals = native_qemu_metal_refusals + 1; return kIOReturnBadArgument;}
    // The display hand-off's connection, first: it is a different kind of client, and a slot is exactly what it must not take.
    if (type == kHandoffOpenType) {
        AGXDeviceUserClient *client = new AGXDeviceUserClient;
        if (!client) return kIOReturnNoMemory;
        client->setHandoffOnly();
        if (!client->initWithTask(owningTask, securityID, type) || !client->attach(provider)) {
            OSSafeReleaseNULL(client);
            native_qemu_metal_handoff_refusals = native_qemu_metal_handoff_refusals + 1;
            return kIOReturnNoMemory;
        }
        if (!client->start(provider)) {
            client->detach(provider);
            client->release();
            native_qemu_metal_handoff_refusals = native_qemu_metal_handoff_refusals + 1;
            return kIOReturnExclusiveAccess;
        }
        native_qemu_metal_handoff_opens = native_qemu_metal_handoff_opens + 1;
        *handler = client;
        return kIOReturnSuccess;
    }
    if (type != kOpenType) {native_qemu_metal_refusals = native_qemu_metal_refusals + 1; return kIOReturnBadArgument;}
    AGXDeviceUserClient *client = new AGXDeviceUserClient;
    if (!client || !client->initWithTask(owningTask, securityID, type) || !client->attach(provider)) {
        OSSafeReleaseNULL(client);
        return kIOReturnNoMemory;
    }
    if (!client->start(provider)) {
        client->detach(provider);
        client->release();
        native_qemu_metal_refusals = native_qemu_metal_refusals + 1;
        return kIOReturnExclusiveAccess;
    }
    native_qemu_metal_opens = native_qemu_metal_opens + 1;
    *handler = client;
    return kIOReturnSuccess;
}
IOReturn AGXAccelerator::newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler)
{
    (void)properties;
    return openClient(this, owningTask, securityID, type, handler);
}
IOReturn QemuMetalGrant::newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler)
{
    (void)properties;
    return openClient(this, owningTask, securityID, type, handler);
}

bool AGXDeviceUserClient::start(IOService *provider)
{
    if (!IOUserClient::start(provider)) return false;
    // A hand-off connection serves frames and owns nothing: it takes no slot of the window and leaves native_qemu_metal_owners alone,
    // which is what keeps a slot open's measurement (opens=1, owners="back") meaningful while a second connection serves the display.
    if (handoff) return true;
    // A slot has one heap allocator, in its owner's process. The opener is the calling process.
    char name[17] = {};
    proc_selfname(name, sizeof name);
    const bool server = !strncmp(name, "backboardd", sizeof name);
    for (unsigned i = server ? 0 : 1; i < (server ? 1u : QM_SLOTS); i++) {
        if (!OSCompareAndSwap8(0, 1, &gSlots[i])) continue;
        slot = (int)i;
        for (unsigned c = 0; c < sizeof name; c++) native_qemu_metal_owners[i][c] = name[c];
        return true;
    }
    return false;
}
void AGXDeviceUserClient::giveBack()
{
    if (slot < 0) return;
    native_qemu_metal_owners[slot][0] = 0;
    gSlots[slot] = 0;
    slot = -1;
}
void AGXDeviceUserClient::stop(IOService *provider)
{
    giveBack();
    IOUserClient::stop(provider);
}
IOReturn AGXDeviceUserClient::clientClose()
{
    giveBack();
    terminate();
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory)
{
    // The writable scanout, for the hand-off's GPU copy. The presenter hands it out only in GPU mode; a refused call is counted, so a
    // guest without sheng-fb-gpu=1 says so here instead of failing silently inside the plugin's copy.
    if (type == kScanoutType) {
        if (!memory || !options) return kIOReturnBadArgument;
        IOMemoryDescriptor *scanout = native_gop_handoff_memory();
        if (!scanout) {
            native_qemu_metal_handoff_refusals = native_qemu_metal_handoff_refusals + 1;
            native_qemu_metal_handoff_last_selector = type;
            native_qemu_metal_handoff_last_status = kIOReturnNotReady;
            return kIOReturnNotReady;
        }
        *memory = scanout;                 // already retained for the caller
        *options = kIOMapAnywhere | kIOMapInhibitCache;   // firmware framebuffer: device memory, never cached by the CPU
        native_qemu_metal_scanout_maps = native_qemu_metal_scanout_maps + 1;
        return kIOReturnSuccess;
    }
    if (type != 0 || slot < 0 || !memory || native_shared_window_stage < 2) return kIOReturnBadArgument;
    uint64_t offset = 0, bytes = 0;
    qm_slot(native_shared_window_size, (unsigned)slot, &offset, &bytes);
    const IOOptionBits direction = static_cast<IOOptionBits>(kIODirectionInOut) | static_cast<IOOptionBits>(kIOMemoryMapperNone);
    *memory = IOMemoryDescriptor::withAddressRange(native_shared_window_base + offset, bytes, direction, TASK_NULL);
    if (options) *options = 0;
    return *memory ? kIOReturnSuccess : kIOReturnNoMemory;
}

IOService *native_qemu_metal_accelerator_allocate(void) {return new AGXAccelerator;}
unsigned native_qemu_metal_accelerator_instance_size(void) {return sizeof(AGXAccelerator);}
IOService *native_qemu_metal_grant_allocate(void) {return new QemuMetalGrant;}

// The display hand-off's three methods, reached through this class's connection instead of the presenter's own service. A wait sleeps, so
// it runs without any lock of ours; nothing here touches a slot, the window or the GPU. Every other selector is refused and counted, so a
// guest whose sandbox admits a different set of selectors says which one it tried.
//
// The privilege is the sandbox's, not ours: native_gop_handoff_call takes no capability argument, because the original profiles that admit
// this connection class do not admit the presenter's own service (NativeGOPPresenter.cpp's NativeGOPClient does check
// kIOClientPrivilegeAdministrator, and that is precisely the client a sandboxed process cannot open). So on this guest the display hand-off
// is reachable by exactly the processes the sandbox already lets open the accelerator -- which is the render server and the granted clients,
// the same set that can render at all.
//
// The two ways in, both measured:
//   * a call whose first eight bytes are the tunnel magic, which carries the method itself and may use any selector the profile admits;
//   * the method's own selector, 0x7000 + NGP_METHOD_*, for a guest whose profile happens to list it.
// The first is what the plugin uses, because the second cannot be relied on: which selectors a profile lists is not this driver's decision.
static IOReturn handoffRun(uint32_t method, const void *in, size_t inSize, void *out, size_t outSize)
{
    if (method < kHandoffBase || method > kHandoffLast) {
        native_qemu_metal_handoff_refusals = native_qemu_metal_handoff_refusals + 1;
        native_qemu_metal_handoff_last_method = method;
        native_qemu_metal_handoff_last_status = kIOReturnUnsupported;
        return kIOReturnUnsupported;
    }
    const IOReturn status = native_gop_handoff_call(method - kHandoffBase, current_task(), in, inSize, out, outSize);
    native_qemu_metal_handoff_calls = native_qemu_metal_handoff_calls + 1;
    native_qemu_metal_handoff_last_method = method;
    native_qemu_metal_handoff_last_status = status;
    return status;
}

IOReturn AGXDeviceUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *arguments, IOExternalMethodDispatch *,
                                             OSObject *, void *)
{
    if (!arguments) return kIOReturnBadArgument;
    // Which selector carried the call, kept apart from which method it carried: through the tunnel they are different numbers, and a
    // hand-off that never reaches the presenter has to say whether the profile admitted no selector at all or the method was refused.
    native_qemu_metal_handoff_last_selector = selector;
    // The tunnel: {magic, method, payload...} in, the method's exact output out. A probe (method 0xffffffff) proves the selector was
    // admitted and carries no payload, which is how the plugin finds a selector a profile lists without knowing the profile.
    if (arguments->structureInput && arguments->structureInputSize >= 8 &&
        static_cast<const uint32_t *>(arguments->structureInput)[0] == kTunnelMagic) {
        const uint32_t method = static_cast<const uint32_t *>(arguments->structureInput)[1];
        if (method == kProbe) {
            native_qemu_metal_handoff_probes = native_qemu_metal_handoff_probes + 1;
            return kIOReturnSuccess;
        }
        return handoffRun(method, static_cast<const uint8_t *>(arguments->structureInput) + 8,
                          arguments->structureInputSize - 8, arguments->structureOutput, arguments->structureOutputSize);
    }
    // The method's own selector, when the profile lists it.
    return handoffRun(selector, arguments->structureInput, arguments->structureInputSize, arguments->structureOutput,
                      arguments->structureOutputSize);
}
