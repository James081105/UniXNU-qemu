// SPDX-License-Identifier: BSD-2-Clause-Patent
// Publishes QemuMetalAccelerator.cpp's accelerator when run-qemu.py --shared-window gave the guest a window (SharedWindow.cpp), from the
// late probe, before launchd: IOGPUFamily is loaded (prelinked, but nothing else loads it), the staged classes are registered under it
// and the accelerator is attached to the platform with the keys the original Metal discovery reads, as sheng's publishMetal
// (UniXNU-sheng/package/GpuPowerProbe.cpp). Then a grant for each other process run-qemu.py names (QemuMetalAccelerator.cpp).
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOService.h>
#include <libkern/OSKextLib.h>
#include <libkern/c++/OSMetaClass.h>
#include <pexpert/pexpert.h>

extern "C" {
extern volatile uint32_t native_shared_window_stage;
extern kmod_info_t g_kernel_kmod_info;
volatile uint32_t native_qemu_metal_stage, native_qemu_metal_grants;
volatile int native_qemu_metal_status;
void native_qemu_metal_static_init(void) __asm__("__GLOBAL__sub_I_QemuMetalAccelerator.cpp");
IOService *native_qemu_metal_accelerator_allocate(void);
unsigned native_qemu_metal_accelerator_instance_size(void);
IOService *native_qemu_metal_grant_allocate(void);
bool native_fw_cfg_read(const char *name, char *text, size_t capacity);
void native_qemu_metal_publish(void);
}

extern "C" {
// Default off. Stages: 1 load asked, 2 parent qualified, 3 class registered,
// 4 service published. These record discovery, never rendered frames.
volatile uint32_t native_software_discovery_stage;
volatile int native_software_discovery_status;
volatile uint32_t native_software_discovery_parent_bytes;
volatile uint32_t native_software_discovery_child_bytes;
volatile uint32_t native_software_discovery_inherits;
volatile uint32_t native_software_discovery_metal_properties;
volatile uint64_t native_software_discovery_object;
volatile uint64_t native_software_discovery_registry_id;
void native_software_discovery_static_init(void) __asm__("__GLOBAL__sub_I_SoftwareDiscovery.cpp");
IOService *native_software_discovery_allocate(void);
unsigned native_software_discovery_instance_size(void);
void native_software_discovery_publish(void);
}

void native_software_discovery_publish(void)
{
    unsigned enabled = 0;
    PE_parse_boot_argn("unipad-software-discovery", &enabled, sizeof(enabled));
    if (enabled != 1 || native_software_discovery_stage) return;
    native_software_discovery_stage = 1;
    native_software_discovery_status = OSKextLoadKextWithIdentifier("com.apple.iokit.IOGPUFamily");
    if (native_software_discovery_status) return;
    const OSSymbol *name = OSSymbol::withCString("IOAcceleratorES");
    const OSMetaClass *parent = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
    OSSafeReleaseNULL(name);
    native_software_discovery_parent_bytes = parent ? parent->getClassSize() : 0;
    if (!parent || native_software_discovery_parent_bytes != 136) {
        native_software_discovery_status = kIOReturnNotFound;
        return;
    }
    native_software_discovery_stage = 2;
    void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if (!handle) {native_software_discovery_status = kIOReturnNoResources; return;}
    native_software_discovery_static_init();
    native_software_discovery_status = OSMetaClass::postModLoad(handle);
    if (native_software_discovery_status) return;
    native_software_discovery_child_bytes = native_software_discovery_instance_size();
    if (native_software_discovery_child_bytes != 136) {
        native_software_discovery_status = kIOReturnInvalid;
        return;
    }
    native_software_discovery_stage = 3;
    IOService *service = native_software_discovery_allocate();
    if (!service || !service->init()) {
        OSSafeReleaseNULL(service);
        native_software_discovery_status = kIOReturnNoMemory;
        return;
    }
    native_software_discovery_inherits = service->metaCast("IOAcceleratorES") != nullptr;
    native_software_discovery_metal_properties =
        (service->getProperty("MetalPluginName") ? 1U : 0U) |
        (service->getProperty("MetalPluginClassName") ? 2U : 0U);
    if (!native_software_discovery_inherits || native_software_discovery_metal_properties) {
        service->release();
        native_software_discovery_status = kIOReturnInvalid;
        return;
    }
    service->setName("sgx");
    service->setProperty("UniPadDiscoveryOnly", true);
    if (!service->attach(IOService::getPlatform())) {
        service->release();
        native_software_discovery_status = kIOReturnNotAttached;
        return;
    }
    service->registerService();
    native_software_discovery_object = reinterpret_cast<uintptr_t>(service);
    native_software_discovery_registry_id = service->getRegistryEntryID();
    native_software_discovery_stage = 4;
    service->release();
}

// Stages: 1 IOGPUFamily load asked, 2 its IOAcceleratorES found, 3 own classes registered, 4 published.
void native_qemu_metal_publish(void)
{
    if (native_qemu_metal_stage || native_shared_window_stage < 4) return;
    native_qemu_metal_stage = 1;
    native_qemu_metal_status = OSKextLoadKextWithIdentifier("com.apple.iokit.IOGPUFamily");
    if (native_qemu_metal_status) return;
    const OSSymbol *name = OSSymbol::withCString("IOAcceleratorES");
    const OSMetaClass *parent = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
    OSSafeReleaseNULL(name);
    if (!parent || parent->getClassSize() != 136) {native_qemu_metal_status = kIOReturnNotFound; return;}
    native_qemu_metal_stage = 2;
    void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if (!handle) {native_qemu_metal_status = kIOReturnNoResources; return;}
    native_qemu_metal_static_init();
    native_qemu_metal_status = OSMetaClass::postModLoad(handle);
    if (native_qemu_metal_status) return;
    if (native_qemu_metal_accelerator_instance_size() != 136) {native_qemu_metal_status = kIOReturnInvalid; return;}
    native_qemu_metal_stage = 3;
    IOService *accelerator = native_qemu_metal_accelerator_allocate();
    if (!accelerator || !accelerator->init()) {OSSafeReleaseNULL(accelerator); native_qemu_metal_status = kIOReturnNoMemory; return;}
    // The keys the original _MTLRegisterDevices reads; the bundle exists through the addon mount (zzy-native-gpu-addon-mount.patch).
    accelerator->setProperty("MetalPluginName", "QemuMetal");
    accelerator->setProperty("MetalPluginClassName", "QemuMetalDevice");
    // The one process whose open of the accelerator and its client the kernel grants in place of the sandbox verdict
    // (zzw-native-adreno-sandbox-grant.patch): the render server.
    accelerator->setProperty("NativeSandboxGrantProcess", "backboardd");
    if (!accelerator->attach(IOService::getPlatform())) {accelerator->release(); native_qemu_metal_status = kIOReturnNotAttached; return;}
    accelerator->registerService();
    accelerator->release();
    native_qemu_metal_stage = 4;
    // A grant for each other process that gets a device (run-qemu.py --metal-clients: names separated by commas).
    char clients[256];
    if (!native_fw_cfg_read("opt/org.unixnu/metal-clients", clients, sizeof clients)) return;
    for (char *name = clients, *next; *name; name = next) {
        for (next = name; *next && *next != ','; next++) {}
        if (*next) *next++ = 0;
        if (!*name) continue;
        IOService *grant = native_qemu_metal_grant_allocate();
        if (!grant || !grant->init()) {OSSafeReleaseNULL(grant); continue;}
        grant->setProperty("NativeSandboxGrantProcess", name);
        if (grant->attach(IOService::getPlatform())) {
            grant->registerService();
            native_qemu_metal_grants = native_qemu_metal_grants + 1;
        }
        grant->release();
    }
}
