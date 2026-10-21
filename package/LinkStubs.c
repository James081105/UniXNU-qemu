// SPDX-License-Identifier: BSD-2-Clause-Patent
// Definitions of hooks that the patched XNU and the linked drivers reference from components the qemu defconfigs
// leave out. The kernel link needs each symbol even where its caller never runs or treats it as absent.

#include <stdint.h>

// offline-component-load-probes.patch, bsd_init: called on offline-legacy-register=1 (NativeLegacyDisplay), which
// board.toml does not pass.
void native_legacy_display_probe(void);
void native_legacy_display_probe(void)
{
}

// NativeVirtioBlock in verified mode (qemu_root_defconfig) asks NativeUfs whether a provider is one of its windows.
// It declares the hook weak, but a static kernel link still requires a definition; -1 means "not a UFS window", the
// same as the hook being absent.
int native_ufs_block_authenticates(void *provider, const char *uuid, uint8_t *digest);
int native_ufs_block_authenticates(void *provider, const char *uuid, uint8_t *digest)
{
  (void)provider;
  (void)uuid;
  (void)digest;
  return -1;
}
