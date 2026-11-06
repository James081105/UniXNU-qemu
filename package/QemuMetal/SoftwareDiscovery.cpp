// SPDX-License-Identifier: BSD-2-Clause-Patent
// Discovery experiment only. No rendering API or operational overrides.
#include "OriginalAcceleratorABI.h"

class UniPadSoftwareDiscovery : public IOAcceleratorES {
    OSDeclareDefaultStructors(UniPadSoftwareDiscovery)
};
OSDefineMetaClassAndStructors(UniPadSoftwareDiscovery, IOAcceleratorES)

static_assert(sizeof(UniPadSoftwareDiscovery) == 136, "Exact audited original base extent");

extern "C" IOService *native_software_discovery_allocate(void)
{
    return new UniPadSoftwareDiscovery;
}

extern "C" unsigned native_software_discovery_instance_size(void)
{
    return sizeof(UniPadSoftwareDiscovery);
}
