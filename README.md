# UniXNU-qemu

Device repository for the QEMU AArch64 `virt` machine, laid out like [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng). QEMU stands in for a tablet: the kernel, DarwinBoot and UniXNU are the same as on the boards, so they can be brought up here before a device is at hand. The kernel configuration is kept the same as `pipa_defconfig` of UniXNU-pipa.

What QEMU replaces:

| On a tablet | Here |
| --- | --- |
| Mu-Silicium firmware with DarwinBoot inside | QEMU's EDK II AArch64 firmware; the payload loads DarwinBoot as a driver |
| Payload volume in the boot image ramdisk | The payload volume's files on a read-only FAT drive |
| Board device tree (`make-<board>-adt.py`) | `make-qemu-adt.py`, around UniXNU's `scripts/qemu-entry/make-dt.py` |
| Board GIC, timer, panel | QEMU GICv3, the architected timer at 62.5 MHz, ramfb |

What QEMU does not cover: the stock bootloader, the board firmware, UFS, touch, the panel and the GPU.

## Files

| File | Contents |
| --- | --- |
| `board.toml` | Board, IPSW, kernel, payload and ADT settings |
| `configs/qemu_defconfig` | Kernel configuration up to the root mount, and the mockfs test root |
| `configs/qemu_root_defconfig` | `qemu_defconfig` plus the verified VirtIO block driver and the owner root chain: the original OS volume as the root |
| `feeds.toml` | IOStorageFamily, which UniXNU's feeds do not list ([UniXNU#7](https://github.com/UniXNU/UniXNU/issues/7)) |
| `scripts/make-qemu-adt.py` | Device tree: `make-dt.py` (GICv3, NVRAM proxy, memory window, SHA2-384, platform-only AMFI, root matching, the VirtIO MMIO windows of QEMU's own device tree) plus the platform seed and the iPad7,1 identity of the board trees; the UFS test nodes of `make-dt.py` are removed |
| `scripts/build-darwinboot-efi.py` | DarwinBoot as a standalone driver image |
| `scripts/run-qemu.py` | Stage the ESP, boot, keep the serial log and take screenshots; `--native-user` boots UniXNU's static test process from mockfs, `--os-root` attaches the OS disk image |
| `scripts/build-storage-abi.py` | The original block storage ABI (`build/storage-abi/interface-audit.json`): makes the inputs of UniXNU's `audit-storage-interface.py` from this board's kernel and runs it |
| `scripts/build-os-root.py` | Everything `qemu_root_defconfig` and its payload need besides the kernel step: the storage ABI, the OS disk image, the owner key, the OS policy, the policy archive, the verified VirtIO driver and the original trust cache bundle |
| `scripts/build-platform-power.py`, `package/ShengPlatformPower.cpp` | The original-PMGR bridge of UniXNU-sheng (no hardware access) |
| `scripts/build-link-stubs.py`, `package/LinkStubs.c` | No-op hooks for probes of left-out components |
| `scripts/build-arm-driver.py`, `scripts/patch-xnu-entry-probe.py`, `scripts/xnu-panic-screen.c` | Copied unchanged from UniXNU-sheng |

## Build and run

Needs QEMU (`brew install qemu`) besides the [host tools](https://unixnu.com/guide/requirements). Until [UniXNU#5](https://github.com/UniXNU/UniXNU/pull/5) is merged, a first kernel build needs its branch `fix/kernel-bootstrap`.

```sh
cd ~/agents-home/UniXNU
export UNIXNU_IPSW=~/agents-home/UniXNU/out/ipsw/iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw
export UNIXNU_UEFI=~/agents-home/uefi-pipa        # only for the panic screen font of the payload step

./unixnu defconfig qemu_defconfig -D ../UniXNU-qemu
./unixnu ipsw fetch -D ../UniXNU-qemu
./unixnu build package kernel -D ../UniXNU-qemu
./unixnu build payload -D ../UniXNU-qemu --unique-chip-id "$(cat ../UniXNU-qemu/.cache/platform-seed)"

python3 out/qemu/tree/scripts/run-qemu.py --display cocoa
```

`run-qemu.py` writes `out/qemu/run/<time>/` with the serial log, the QEMU command and the screenshots. `--seconds` and `--every` set the run time and the screenshot interval; without `--display cocoa` it runs without a window.

The platform seed in `.cache/platform-seed` is created once per checkout and stays out of Git.

On a new tree the PMGR bridge has no reference kernel yet: the first `build kernel` links a no-op in its place and says so, and the second links the bridge.

### Test process from mockfs

```sh
python3 out/qemu/tree/scripts/run-qemu.py --native-user --display cocoa
```

`--native-user` builds UniXNU's static test process (`build-native-user-fixture.py`) if needed and loads it as the ramdisk; XNU's mockfs (`CONFIG_XNU_SOURCE_MOCKFS`) mounts it on `md0` (`rd=md0`) and serves it as `/sbin/launchd`. The process's own trust cache replaces `kernel.macho.trustcache`, and the run removes `root-matching` from the device tree. The process calls `getpid` and `sysctl hw.pagesize`, stores the results and then loops. Before stopping QEMU the script reads the CPU state and those results through the QEMU monitor:

```
native user: PC 00000001000003d4 (checkpoint 1000003d4), EL0, getpid 1, sysctl status 0, hw.pagesize 4096
```

The screen keeps the three entry probe bars over the UniXNU menu: the kernel draws nothing unless it panics.

### OS volume as the root

After the build above (`qemu_defconfig`):

```sh
cd out/qemu/tree && python3 scripts/build-os-root.py && cd ../../..
./unixnu defconfig qemu_root_defconfig -D ../UniXNU-qemu
./unixnu build kernel -D ../UniXNU-qemu
./unixnu build payload -D ../UniXNU-qemu --unique-chip-id "$(cat ../UniXNU-qemu/.cache/platform-seed)" \
  --trust-cache out/qemu/tree/build/original-static-system-trust/original-static-system.bundle

python3 out/qemu/tree/scripts/run-qemu.py --os-root --display cocoa
```

`build-os-root.py` skips each step whose output exists. The first run boots the `qemu_defconfig` kernel in QEMU once (for the storage ABI), signs and checks the 1,483,785 blocks of the OS disk image, and attaches that image read-only for the time of the policy step. The owner private key stays in `out/qemu/tree/build/verified-root-host/keys`; a new key needs a new policy, kernel and payload.

`--os-root` attaches the OS disk image read-only (without mounting it) and gives QEMU the raw device as a VirtIO block device. The kernel reads every block through the owner-signed policy, mounts the OS volume by its UUID, accepts the root that APFS cannot authenticate without Apple's boot manifest, and starts the original `/sbin/launchd`. It ends with:

```
initproc failed to start -- exit reason namespace 6 subcode 0x1 description: Library not loaded: /usr/lib/libSystem.B.dylib
  Referenced from: <4B525992-8C69-315E-A51A-CC9433E45B51> /sbin/launchd
  Reason: tried: '/usr/lib/libSystem.B.dylib' (no such file, no dyld cache)
```

The dyld shared cache is on the SystemOS cryptex, which needs a second policy that no UniXNU script makes yet ([UniXNU#7](https://github.com/UniXNU/UniXNU/issues/7)). Without `--trust-cache` the kernel stops one step earlier: AMFI refuses launchd (`unexpected SIGKILL of launchd`, namespace 9 code 0x8).

Three gaps of a clean tree that `build-os-root.py` and this repository fill:

- `audit-storage-interface.py` reads a candidate kernel, a live observation and the AES audit's metaclass layout, which no UniXNU script produces; `build-storage-abi.py` makes them (see its header).
- `build-verified-root-probe.py` compiles with the bsd compile record and `-Werror`; the record names `build/xnu/ios-obj/EXPORT_HDRS/libsa`, which the kernel build does not create (it creates the one under `build/xnu/obj`).
- `build-original-system-trust.py` reads `.cache/firmware/target-volumes/static-trustcache.bin`, the payload of the StaticTrustCache IM4P, and the IPSW from `refer/`; neither is produced or linked.

The verified NativeVirtioBlock declares the NativeUfs hook `native_ufs_block_authenticates` weak, but a static kernel link still needs it: `package/LinkStubs.c` defines it as "not a UFS window".

## State

| Stage | Result |
| --- | --- |
| UniXNU menu, configuration, DarwinBoot as a driver | works |
| DarwinBoot hand-off, 4 GiB window, kernel entry, MMU and 4 KiB page tables | works |
| Platform expert, IOKit start | works |
| AMFI | starts with the PMGR bridge (`sheng-pmgr=2`); without it the kernel panics with `AMFI: No PMGR?` |
| BSD start, root mount (`qemu_defconfig`) | stops with `Failed to mount root device`: no storage driver |
| mockfs root on `md0`, first user process (`--native-user`) | works: the test process runs at EL0 as PID 1 and reads a 4 KiB `hw.pagesize` |
| Verified VirtIO block, OS volume as the root (`qemu_root_defconfig`, `--os-root`) | works: the owner-signed root is mounted and accepted |
| AMFI, original `/sbin/launchd` from the OS volume | starts with the original trust cache bundle |
| dyld | stops: no dyld shared cache without SystemOS |

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE). The copied scripts and `ShengPlatformPower.cpp` come from UniXNU-sheng under the same license.
