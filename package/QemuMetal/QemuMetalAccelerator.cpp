// SPDX-License-Identifier: BSD-2-Clause-Patent
// The accelerator iPadOS's Metal discovery finds (IOServiceMatching("IOAcceleratorES") -> MetalPluginName ->
// /System/Library/Extensions/QemuMetal.bundle -> initWithAcceleratorPort:), and the connection that gives the plugin its window to the
// Mac: the memory of run-qemu.py --shared-window that SharedWindow.cpp found, which QemuMetalRemote.cpp and qemu-metal-host share.
//
// As sheng's Adreno accelerator (drivers/NativeAdreno/NativeAdrenoAccelerator.cpp), the class derives from the original IOAcceleratorES
// of IOGPUFamily (drivers/NativeMetal/OriginalAcceleratorABI.h, bound to the audited original addresses by
// build-native-legacy-display.py), and is named after Apple's accelerator class. Its static initializer is taken out of __mod_init_func
// and run by QemuMetalPublish.cpp once IOGPUFamily has loaded, since the parent metaclass exists only then. The open grant of backboardd
// is the accelerator's NativeSandboxGrantProcess property (patches/xnu/zzw-native-adreno-sandbox-grant.patch).
//
// One connection at a time owns the window (the render server's plugin); open type kOpenType, memory type 0 is the window. The plugin maps
// it write-back cacheable, as the kernel and the Mac do.
#include "OriginalAcceleratorABI.h"
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOUserClient.h>
#include <libkern/OSAtomic.h>
#include <kern/task.h>

extern "C" {
extern volatile uint64_t native_shared_window_base, native_shared_window_size;
extern volatile uint32_t native_shared_window_stage;
volatile uint32_t native_qemu_metal_opens, native_qemu_metal_refusals;
IOService *native_qemu_metal_accelerator_allocate(void);
unsigned native_qemu_metal_accelerator_instance_size(void);
}

namespace {
constexpr UInt32 kOpenType = 0x514d;          // 'QM'
volatile bool gOwned;
}

class AGXAccelerator : public IOAcceleratorES {
    OSDeclareDefaultStructors(AGXAccelerator)
public:
    IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AGXAccelerator, IOAcceleratorES)

class QemuMetalUserClient : public IOUserClient {
    OSDeclareDefaultStructors(QemuMetalUserClient)
    bool owner = false;
public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    IOReturn clientClose() APPLE_KEXT_OVERRIDE;
    IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(QemuMetalUserClient, IOUserClient)

IOReturn AGXAccelerator::newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler)
{
    (void)properties;
    if (type != kOpenType || !handler) {native_qemu_metal_refusals = native_qemu_metal_refusals + 1; return kIOReturnBadArgument;}
    QemuMetalUserClient *client = new QemuMetalUserClient;
    if (!client || !client->initWithTask(owningTask, securityID, type) || !client->attach(this)) {
        OSSafeReleaseNULL(client);
        return kIOReturnNoMemory;
    }
    if (!client->start(this)) {
        client->detach(this);
        client->release();
        native_qemu_metal_refusals = native_qemu_metal_refusals + 1;
        return kIOReturnExclusiveAccess;
    }
    native_qemu_metal_opens = native_qemu_metal_opens + 1;
    *handler = client;
    return kIOReturnSuccess;
}

bool QemuMetalUserClient::start(IOService *provider)
{
    if (!IOUserClient::start(provider)) return false;
    // The window has one heap allocator, in its owner's process.
    if (!OSCompareAndSwap8(0, 1, (volatile UInt8 *)&gOwned)) return false;
    owner = true;
    return true;
}
void QemuMetalUserClient::stop(IOService *provider)
{
    if (owner) {owner = false; gOwned = false;}
    IOUserClient::stop(provider);
}
IOReturn QemuMetalUserClient::clientClose()
{
    if (owner) {owner = false; gOwned = false;}
    terminate();
    return kIOReturnSuccess;
}
IOReturn QemuMetalUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory)
{
    if (type != 0 || !owner || !memory || native_shared_window_stage < 2) return kIOReturnBadArgument;
    const IOOptionBits direction = static_cast<IOOptionBits>(kIODirectionInOut) | static_cast<IOOptionBits>(kIOMemoryMapperNone);
    *memory = IOMemoryDescriptor::withAddressRange(native_shared_window_base, native_shared_window_size, direction, TASK_NULL);
    if (options) *options = 0;
    return *memory ? kIOReturnSuccess : kIOReturnNoMemory;
}

IOService *native_qemu_metal_accelerator_allocate(void) {return new AGXAccelerator;}
unsigned native_qemu_metal_accelerator_instance_size(void) {return sizeof(AGXAccelerator);}
