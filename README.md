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
| `configs/qemu_defconfig` | Kernel configuration |
| `scripts/make-qemu-adt.py` | Device tree: `make-dt.py` (GICv3, NVRAM proxy, memory window, SHA2-384, platform-only AMFI) plus the platform seed and the iPad7,1 identity of the board trees; the UFS test nodes of `make-dt.py` are removed |
| `scripts/build-darwinboot-efi.py` | DarwinBoot as a standalone driver image |
| `scripts/run-qemu.py` | Stage the ESP, boot, keep the serial log and take screenshots |
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

## State

| Stage | Result |
| --- | --- |
| UniXNU menu, configuration, DarwinBoot as a driver | works |
| DarwinBoot hand-off, 4 GiB window, kernel entry, MMU and 4 KiB page tables | works |
| Platform expert, IOKit start | works |
| AMFI | starts with the PMGR bridge (`sheng-pmgr=2`); without it the kernel panics with `AMFI: No PMGR?` |
| BSD start, root mount | stops with `Failed to mount root device`: there is no root volume or storage driver yet |

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE). The copied scripts and `ShengPlatformPower.cpp` come from UniXNU-sheng under the same license.
