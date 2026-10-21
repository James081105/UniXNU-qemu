#!/usr/bin/env python3
"""Make what qemu_root_defconfig and its payload need besides the kernel step.

Run in the build tree after "unixnu build package kernel payload" with qemu_defconfig. In order:

1. build-storage-abi.py: the original block storage ABI the block drivers bind to (it boots that kernel in QEMU once).
2. The OS disk image and the SystemOS image of the IPSW (extract-target-volume.py) in .cache/firmware/target-volumes.
3. The owner key pair and the host verifier (kits/VerifiedRoot/tests/check.py) in build/verified-root-host. The
   private key never leaves the build tree; a new key means new policies and a new kernel.
4. With the OS disk image attached read-only and its volume mounted at target-volumes/os-mount (detached afterwards):
   build-owner-os-policy.py, which signs the hash of every 4 KiB block of the OS disk image and verifies them all
   again, and prepare-owner-data-fixture.py, a 32 MiB APFS Data seed with the OS volume's initial private/var, kept
   in .cache/firmware/owner-data.
5. build-owner-systemos-policy.py: the same for the SystemOS image, under the same key.
6. build-verified-root-probe.py --component os --include-systemos: both policies and the verifier as a kernel archive.
7. NativeVirtioBlock in verified mode, picking the policy by disk size, and NativeRamBlock, each with its
   original-class bindings.
8. The Data seed packed for DarwinBoot (DBRDISK1).
9. build-original-system-trust.py: the original static and System trust caches as one bundle, the payload's
   --trust-cache. Without it AMFI refuses to run launchd (exit reason namespace 9, EXEC, code 8, SECURITY_POLICY).
   Its static module is the payload of the StaticTrustCache IM4P that "unixnu ipsw fetch" extracts.

Each step is skipped when its output exists.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ipsw', type=Path, default=os.environ.get('UNIXNU_IPSW'), help='local IPSW (default: $UNIXNU_IPSW)')
parser.add_argument('--openssl-prefix', type=Path, default=Path('/opt/homebrew/opt/openssl@3'),
                    help='OpenSSL 3 of the host verifier (brew install openssl@3)')
args = parser.parse_args()
volumes = ROOT / '.cache/firmware/target-volumes'
data = ROOT / '.cache/firmware/owner-data'


def step(*command):
    print('==', ' '.join(str(part) for part in command), flush=True)
    subprocess.run([sys.executable, *command], cwd=ROOT, check=True)


if not (ROOT / 'build/storage-abi/interface-audit.json').is_file():
    step(ROOT / 'scripts/build-storage-abi.py')
for component, member in (('OS', '044-76040-038.dmg'), ('Cryptex1,SystemOS', '044-76034-038.dmg')):
    if not (volumes / member).is_file():
        if not args.ipsw:
            parser.error('the disk images are extracted from the IPSW: pass --ipsw or set UNIXNU_IPSW')
        step(ROOT / 'scripts/extract-target-volume.py', component, '--ipsw', args.ipsw)
if not (ROOT / 'build/verified-root-host/keys/owner.pem').is_file():
    step(ROOT / 'kits/VerifiedRoot/tests/check.py', '--output', ROOT / 'build/verified-root-host',
         '--openssl-prefix', args.openssl_prefix)
os_policy = ROOT / 'build/verified-root-os/verification.json'
if not os_policy.is_file() or not (data / 'seed.json').is_file():
    mount = volumes / 'os-mount'
    mount.mkdir(exist_ok=True)
    attached = plistlib.loads(subprocess.check_output(['hdiutil', 'attach', '-readonly', '-nobrowse', '-noverify',
                                                       '-noautofsck', '-mountpoint', mount, '-plist',
                                                       volumes / '044-76040-038.dmg']))
    whole = next(e['dev-entry'] for e in attached['system-entities'] if e.get('content-hint') == 'GUID_partition_scheme')
    try:
        if not os_policy.is_file():
            step(ROOT / 'scripts/build-owner-os-policy.py')
        if not (data / 'seed.json').is_file():
            step(ROOT / 'scripts/prepare-owner-data-fixture.py', '--output', data)
    finally:
        subprocess.run(['hdiutil', 'detach', whole], check=True)
if not (ROOT / 'build/verified-root-systemos/verification.json').is_file():
    step(ROOT / 'scripts/build-owner-systemos-policy.py')
if not (ROOT / 'build/verified-root-os-xnu-bundle/libVerifiedRootProbe.a').is_file():
    # The kernel build creates EXPORT_HDRS/libsa under build/xnu/obj only; the bsd compile record names the one under
    # ios-obj, and the probe compiles with -Werror, so a missing include directory stops it.
    (ROOT / 'build/xnu/ios-obj/EXPORT_HDRS/libsa').mkdir(parents=True, exist_ok=True)
    step(ROOT / 'scripts/build-verified-root-probe.py', '--component', 'os', '--include-systemos')
if not (ROOT / 'build/native-virtio-block-verified-bundle/libNativeVirtioBlock.a').is_file():
    step(ROOT / 'scripts/build-arm-driver.py', 'NativeVirtioBlock', '--verified-root', '--verified-root-bundle')
    step(ROOT / 'scripts/build-native-virtio-bindings.py', '--component', 'NativeVirtioBlock',
         '--directory', ROOT / 'build/native-virtio-block-verified-bundle')
if not (ROOT / 'build/native-ram-block/libNativeRamBlock.a').is_file():
    step(ROOT / 'scripts/build-arm-driver.py', 'NativeRamBlock')
    step(ROOT / 'scripts/build-native-virtio-bindings.py', '--component', 'NativeRamBlock',
         '--directory', ROOT / 'build/native-ram-block')
seed = data / 'owner-data-32.dbr'
if not seed.is_file():
    step(ROOT / 'drivers/DarwinBoot/Test/packed_ramdisk.py', data / 'owner-data-32.cdr', seed)
trust = ROOT / 'build/original-static-system-trust/original-static-system.bundle'
if not trust.is_file():
    static = volumes / 'static-trustcache.bin'
    if not static.is_file():
        blob = (ROOT.parent.parent / 'ipsw/21H461/044-76040-038.dmg.trustcache').read_bytes()

        def der(buffer, at):
            tag, size = buffer[at], buffer[at + 1]
            at += 2
            if size & 0x80:
                size, at = int.from_bytes(buffer[at:at + (size & 0x7f)], 'big'), at + (size & 0x7f)
            return tag, buffer[at:at + size], at + size

        _, sequence, _ = der(blob, 0)
        fields, cursor = [], 0
        while cursor < len(sequence):
            _, value, cursor = der(sequence, cursor)
            fields.append(value)
        module = fields[3]
        # The digest build-original-system-trust.py and build-native-user-fixture.py pin for this module.
        if fields[:2] != [b'IM4P', b'trst'] or (hashlib.sha256(module).hexdigest() !=
                                                'da88460e6e513438d5307e5c526f76497a74754476a24da29f239f0c90120fc2'):
            raise SystemExit('unexpected StaticTrustCache IM4P')
        static.write_bytes(module)
    # The script reads the IPSW from refer/, which unixnu does not link.
    ipsw = ROOT / 'refer/iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw'
    if not ipsw.exists():
        if not args.ipsw:
            parser.error('the System trust cache is read from the IPSW: pass --ipsw or set UNIXNU_IPSW')
        ipsw.symlink_to(Path(args.ipsw).resolve())
    step(ROOT / 'scripts/build-original-system-trust.py')
volume = json.loads((data / 'seed.json').read_text())['volume_uuid']
print('OS root inputs ready. Then:')
print(f'  export XNU_OWNER_DATA_VOLUME_UUID={volume}')
print('  unixnu defconfig qemu_root_defconfig; unixnu build kernel')
print(f'  unixnu build payload ... --trust-cache {trust} --data-ramdisk {seed}')
