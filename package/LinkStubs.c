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

// NativeRamBlock keeps the Data overlay in a UFS store when NativeUfs reports a store mode (sheng-data-persist). Mode 0
// is no store, so the Data ramdisk stays in memory and the other hooks are never called; they refuse if they are.
unsigned native_ufs_store_mode(void);
unsigned native_ufs_store_mode(void)
{
  return 0;
}

int native_ufs_store_open(uint64_t blocks, uint64_t *first, const char **why);
int native_ufs_store_open(uint64_t blocks, uint64_t *first, const char **why)
{
  (void)blocks;
  (void)first;
  if (why)
    *why = "no UFS store on this board";
  return 0;
}

int native_ufs_store_read(uint64_t block, uint32_t count, void *buffer);
int native_ufs_store_read(uint64_t block, uint32_t count, void *buffer)
{
  (void)block;
  (void)count;
  (void)buffer;
  return 0;
}

int native_ufs_store_write(uint64_t block, uint32_t count, const void *buffer);
int native_ufs_store_write(uint64_t block, uint32_t count, const void *buffer)
{
  (void)block;
  (void)count;
  (void)buffer;
  return 0;
}

int native_ufs_store_sync(void);
int native_ufs_store_sync(void)
{
  return 0;
}

unsigned native_ufs_store_gpt_check(void);
unsigned native_ufs_store_gpt_check(void)
{
  return 0;
}
