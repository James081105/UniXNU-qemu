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
| `configs/qemu_root_defconfig` | `qemu_defconfig` plus the verified VirtIO block driver, the owner root chain, the SystemOS union mount, the Data mount and the frame buffer display: the original iPadOS userland |
| `feeds.toml` | IOStorageFamily ([UniXNU#7](https://github.com/UniXNU/UniXNU/issues/7)) and dyld 1165.3 (for `audit-target-display-query.py`), which UniXNU's feeds do not list |
| `scripts/make-qemu-adt.py` | Device tree: `make-dt.py` (GICv3, NVRAM proxy, memory window, SHA2-384, platform-only AMFI, root matching, the VirtIO MMIO windows of QEMU's own device tree, `vmm-present`) plus the platform seed and the iPad7,1 identity of the board trees; the UFS test nodes of `make-dt.py` are removed |
| `scripts/build-darwinboot-efi.py` | DarwinBoot as a standalone driver image |
| `scripts/run-qemu.py` | Stage the ESP, boot, keep the serial log and take screenshots; `--native-user` boots UniXNU's static test process from mockfs, `--os-root` attaches the OS and SystemOS images, `--accel hvf` runs on this Mac's CPU |
| `scripts/qemu-ps.py` | The guest's processes (PID, state, name) of a running `run-qemu.py`, read from the kernel's `allproc` through the QEMU monitor; `--value` reads kernel globals such as the display's counters, `--log` saves the kernel log |
| `scripts/qemu_esp.py` | The ESP of a QEMU observation with UniXNU's `gdb-stop.py`: the payload with the kernel step's kernel and extra arguments |
| `scripts/build-display-abi.py` | The inputs of UniXNU's `audit-mobile-graphics-legacy.py`: this board's kernel as the candidate and the original `IOMobileFramebufferLegacy` observed in QEMU |
| `scripts/build-native-legacy-display.py`, `package/LegacyDisplayProbe.cpp`, `package/KeyStoreProbe.cpp` | The display archive: UniXNU-sheng's builder and probes reduced to NativeLegacyDisplay, its probe, NativeGOPPresenter and the AppleKeyStore stand-in (sheng's `KeyStoreProbe.cpp`, unchanged, and NativeKeyStoreEvents), without sheng's touch, HID, CPU frequency, GPU power, log and reset objects |
| `scripts/trace-render.py` | Stops at sites of QuartzCore's render path in backboardd (UniXNU's `prepare-original-cpu-render-observer.py` plans) and records registers and memory |
| `scripts/snapshot-ui-threads.py` | Stops the userland once, after some seconds or when a named process crashes, and reads the threads of the UI processes (saved user registers and frame records) |
| `scripts/build-storage-abi.py` | The original block storage ABI (`build/storage-abi/interface-audit.json`): makes the inputs of UniXNU's `audit-storage-interface.py` from this board's kernel and runs it |
| `scripts/build-owner-systemos-policy.py` | The owner-signed SystemOS policy, which no UniXNU script makes ([UniXNU#7](https://github.com/UniXNU/UniXNU/issues/7)) |
| `scripts/build-os-root.py` | Everything `qemu_root_defconfig` and its payload need besides the kernel step: the storage ABI, the OS and SystemOS images, the owner key, both policies, the policy archive, the verified VirtIO and RAM block drivers, the Data seed, the display archive and the original trust cache bundle |
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

TCG emulates a Cortex-A710 and is slow: the userland below needs about 5 minutes to SpringBoard. `--accel hvf` runs the guest on this Mac's CPU through Hypervisor.framework (tried on an M3 Max: SpringBoard within 100 seconds). The device tree must carry the timer frequency of the accelerator (62.5 MHz under TCG, the host's `hw.tbfrequency` under HVF), so build the payload with `UNIXNU_QEMU_ACCEL=hvf` for it; `run-qemu.py` refuses a payload built for the other one.

The kernel log appears neither on the screen nor on the serial port. `qemu-ps.py --log FILE` saves the kernel's message buffer (the last 128 KiB) of a running VM.

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

### iPadOS userland

After the build above (`qemu_defconfig`):

```sh
cd out/qemu/tree && python3 scripts/build-os-root.py && cd ../../..
export XNU_OWNER_DATA_VOLUME_UUID=$(python3 -c "import json; print(json.load(open('../UniXNU-qemu/.cache/firmware/owner-data/seed.json'))['volume_uuid'])")
./unixnu defconfig qemu_root_defconfig -D ../UniXNU-qemu
./unixnu build kernel -D ../UniXNU-qemu
UNIXNU_QEMU_ACCEL=hvf ./unixnu build payload -D ../UniXNU-qemu --unique-chip-id "$(cat ../UniXNU-qemu/.cache/platform-seed)" \
  --trust-cache out/qemu/tree/build/original-static-system-trust/original-static-system.bundle \
  --data-ramdisk ../UniXNU-qemu/.cache/firmware/owner-data/owner-data-1g.dbr

python3 out/qemu/tree/scripts/run-qemu.py --os-root --accel hvf --display cocoa --seconds 1800
python3 out/qemu/tree/scripts/qemu-ps.py          # in a second shell, while it runs
```

`build-os-root.py` skips each step whose output exists. The first run boots the `qemu_defconfig` kernel in QEMU once (for the storage ABI), signs and checks the 1,483,785 blocks of the OS disk image and the 806,400 blocks of the SystemOS image, and attaches the OS image read-only for the time of the OS policy and the Data seed. The owner private key stays in `out/qemu/tree/build/verified-root-host/keys`; a new key needs new policies, a new kernel and payload. The Data seed and its volume UUID stay in `.cache/firmware/owner-data` of this repository; `qemu_root_defconfig` takes the UUID from the environment because every seed has its own. The seed is grown to 1 GiB: the userland fills 32 MiB within two minutes (backboardd then crashes with ENOSPC), and NativeRamBlock accepts writes to a seed above 2 GiB only through sheng's UFS store. Pages are allocated as they are written; after 2.5 minutes about 260 MiB are.

`--os-root` attaches the OS disk image read-only (without mounting it) and gives QEMU the raw device and the SystemOS image file as two VirtIO block devices. The kernel picks each disk's policy by its size and checks every block it reads, mounts the OS volume by its UUID and accepts it as the root, mounts SystemOS read-only as a union over `/`, mounts the Data ramdisk at `/private/var` and starts the original `/sbin/launchd`. `qemu-ps.py` shows the userland: under HVF after 2.5 minutes about 210 processes (logd, configd, securityd, lockdownd, usermanagerd, backboardd, SpringBoard and the rest of the system daemons). Memory is tight: the kernel manages the 4 GiB DarwinBoot window only, and about 17 MiB stay free. Without the display (an earlier state of `qemu_root_defconfig`) there were about 120 and no SpringBoard, and no panic in a 30-minute run.

The display: under `offline-mobile-graphics-load=1` the kernel loads IOMobileGraphicsFamily, and the probe creates NativeLegacyDisplay, a subclass of the original `IOMobileFramebufferLegacy`, on the UEFI frame buffer (ramfb, 800x600) and publishes it under `/arm-io` as the system display. There is no Metal device, and QuartzCore's original CPU renderer draws the frames, which the display copies to the frame buffer. Under HVF the iPadOS welcome screen ("Hello" in turn in each language, with the status bar) appears between 105 and 120 seconds after the start.

What it takes, found with `trace-render.py` and `snapshot-ui-threads.py`, in order from the screen backwards:

1. The render server ignores every update of the built-in display until a client creates a context with `definesDisplayBounds` for it: `CA::WindowServer::IOMFBDisplay::ignore_update_p` returns true while the display mode is the sentinel -1 (a built-in display without `TimingElements`/`ColorElements` has no other mode, as on the hardware) and no update has carried such a context. Until then the screen keeps the boot image.
2. SpringBoard creates it once it has finished starting. Its main thread waited, in `-[SpringBoard _completeStartupAfterMainSceneConnect:]` → `-[SBHIconManager init]`, on a synchronous XPC request to chronod.
3. chronod's main thread waited, in its ActivityKit setup, on liveactivitiesd.
4. liveactivitiesd crashed at every start: `SessionSyncEngine`'s `Keybag.shared` force-unwraps the result of `MKBEventsRegister`, which is NULL without an AppleKeyStore service.
5. sheng's display archive carries an AppleKeyStore stand-in (NativeKeyStoreEvents, started by `KeyStoreProbe.cpp` with `offline-key-events=1` and `sheng-aks=1`); this repository first left it out as sheng hardware code. With it, the chain completes.

The display's counters:

```sh
python3 out/qemu/tree/scripts/qemu-ps.py --value native_legacy_stage --value native_legacy_client_calls \
  --value native_legacy_swap_validation_calls --value native_legacy_frame_count
```

`native_legacy_stage` 10 is a started phase-4 display.

About 30 seconds later, with no input, iPadOS turns the display off and sleeps the system: `IOPMrootDomain::powerChangeDone` → `IOCPUSleepKernel` → `ArmCpu::haltCPU`, and ArmCpu panics with `unsupported quiesce requested before timer/GIC disable` under `armcpu-diagnostics=1`. Under HVF the panic loop keeps the virtual CPU from exiting, so QEMU's monitor (screenshots, `qemu-ps.py`) stops answering as well.

Each missing piece stops the boot with its own panic:

| Missing | Panic |
| --- | --- |
| `--trust-cache` | `unexpected SIGKILL of launchd`, namespace 9 (EXEC) code 0x8 (SECURITY_POLICY): AMFI refuses launchd |
| SystemOS | `initproc failed to start`, namespace 6 (DYLD): `Library not loaded: /usr/lib/libSystem.B.dylib ... no dyld cache` |
| Data | `tzinit[17] exited ... exit status 30` (EROFS) |
| `vmm-present` | `watchdogd exited`, namespace 2 (SIGNAL) subcode 5 (SIGTRAP): without a VM, the original watchdogd traps when no watchdog service exists ([`check-watchdogd-branches.py`](../UniXNU/scripts/check-watchdogd-branches.py) of UniXNU) |

Gaps of a clean tree that this repository fills:

- `audit-storage-interface.py` reads a candidate kernel, a live observation and the AES audit's metaclass layout, which no UniXNU script produces; `build-storage-abi.py` makes them (see its header).
- No UniXNU script makes the SystemOS policy that `build-verified-root-probe.py --include-systemos` reads; `build-owner-systemos-policy.py` does.
- `build-verified-root-probe.py` compiles with the bsd compile record and `-Werror`; the record names `build/xnu/ios-obj/EXPORT_HDRS/libsa`, which the kernel build does not create (it creates the one under `build/xnu/obj`).
- `build-original-system-trust.py` reads `.cache/firmware/target-volumes/static-trustcache.bin`, the payload of the StaticTrustCache IM4P, and the IPSW from `refer/`; neither is produced or linked.
- The verified NativeVirtioBlock and NativeRamBlock reference NativeUfs hooks (`native_ufs_block_authenticates`, the `native_ufs_store_*` Data store), and the display references sheng's boot progress screen (`boot_progress_*`). They are declared weak, but a static kernel link still needs them: `package/LinkStubs.c` defines them (weak as well) as "not a UFS window", "no store" and "no boot progress screen".
- The display archive's builder exists only in UniXNU-sheng and compiles sheng's board probes; `audit-mobile-graphics-legacy.py` reads a candidate kernel and an observation of the original class that no script produces, and writes to `build/display-analysis` without creating it. `build-native-legacy-display.py` and `build-display-abi.py` of this repository cover them.
- `audit-target-display-query.py`, which makes the process layout of the user-space observations, checks `refer/dyld`, which `unixnu` neither fetches nor links; this repository's `feeds.toml` fetches it and `qemu_esp.py` links it.

sheng disables watchdogd with `sheng-disable-watchdogd=1` instead: its boot progress archive writes launchd's `disabled.plist` from the kernel. A copy written into the Data seed on this Mac does not work: launchd only takes the file from uid 0, and the seed's files belong to the user who made it.

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
| SystemOS union mount, dyld shared cache | works |
| Data ramdisk at `/private/var` (NativeRamBlock) | works |
| System daemons | about 120 processes, no panic in a 30-minute run |
| Frame buffer display (NativeLegacyDisplay) | registered as the system display; backboardd opens it |
| AppleKeyStore stand-in (NativeKeyStoreEvents) | liveactivitiesd, chronod and SpringBoard finish starting |
| CPU-rendered screen | the iPadOS welcome screen, about 2 minutes after the start under HVF |
| Idle sleep | about 30 seconds later the system sleeps and ArmCpu panics (`unsupported quiesce`); no input device yet |

## License

BSD-2-Clause-Patent. See [LICENSE](LICENSE). The copied and adapted scripts, `ShengPlatformPower.cpp` and `LegacyDisplayProbe.cpp` come from UniXNU-sheng under the same license.
