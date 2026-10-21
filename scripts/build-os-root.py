#!/usr/bin/env python3
"""Make the two archives qemu_root_defconfig links: the verified NativeVirtioBlock and the OS root policy.

Run in the build tree after "unixnu build package kernel payload" with qemu_defconfig. In order:

1. build-storage-abi.py: the original block storage ABI the driver binds to (it boots that kernel in QEMU once).
2. The OS disk image of the IPSW (extract-target-volume.py OS) in .cache/firmware/target-volumes.
3. The owner key pair and the host verifier (kits/VerifiedRoot/tests/check.py) in build/verified-root-host. The
   private key never leaves the build tree; a new key means a new policy and kernel.
4. build-owner-os-policy.py: signs the hash of every 4 KiB block of the OS disk image and verifies them all again.
   It reads the image through a read-only attachment with the OS volume mounted at target-volumes/os-mount; the
   attachment is removed afterwards.
5. build-verified-root-probe.py --component os: the policy and the verifier as a kernel archive.
6. NativeVirtioBlock in verified mode and its original-class bindings.
7. build-original-system-trust.py: the original static and System trust caches as one bundle, the payload's
   --trust-cache. Without it AMFI refuses to run launchd (exit reason namespace 9, EXEC, code 8, SECURITY_POLICY).
   Its static module is the payload of the StaticTrustCache IM4P that "unixnu ipsw fetch" extracts.

Each step is skipped when its output exists.
"""
import argparse
import hashlib
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
image = volumes / '044-76040-038.dmg'


def step(*command):
    print('==', ' '.join(str(part) for part in command), flush=True)
    subprocess.run([sys.executable, *command], cwd=ROOT, check=True)


if not (ROOT / 'build/storage-abi/interface-audit.json').is_file():
    step(ROOT / 'scripts/build-storage-abi.py')
if not image.is_file():
    if not args.ipsw:
        parser.error('the OS disk image is extracted from the IPSW: pass --ipsw or set UNIXNU_IPSW')
    step(ROOT / 'scripts/extract-target-volume.py', 'OS', '--ipsw', args.ipsw)
if not (ROOT / 'build/verified-root-host/keys/owner.pem').is_file():
    step(ROOT / 'kits/VerifiedRoot/tests/check.py', '--output', ROOT / 'build/verified-root-host',
         '--openssl-prefix', args.openssl_prefix)
if not (ROOT / 'build/verified-root-os/verification.json').is_file():
    mount = volumes / 'os-mount'
    mount.mkdir(exist_ok=True)
    attached = plistlib.loads(subprocess.check_output(['hdiutil', 'attach', '-readonly', '-nobrowse', '-noverify',
                                                       '-noautofsck', '-mountpoint', mount, '-plist', image]))
    whole = next(e['dev-entry'] for e in attached['system-entities'] if e.get('content-hint') == 'GUID_partition_scheme')
    try:
        step(ROOT / 'scripts/build-owner-os-policy.py')
    finally:
        subprocess.run(['hdiutil', 'detach', whole], check=True)
if not (ROOT / 'build/verified-root-os-xnu/libVerifiedRootProbe.a').is_file():
    # The kernel build creates EXPORT_HDRS/libsa under build/xnu/obj only; the bsd compile record names the one under
    # ios-obj, and the probe compiles with -Werror, so a missing include directory stops it.
    (ROOT / 'build/xnu/ios-obj/EXPORT_HDRS/libsa').mkdir(parents=True, exist_ok=True)
    step(ROOT / 'scripts/build-verified-root-probe.py', '--component', 'os')
if not (ROOT / 'build/native-virtio-block-verified/libNativeVirtioBlock.a').is_file():
    step(ROOT / 'scripts/build-arm-driver.py', 'NativeVirtioBlock', '--verified-root')
    step(ROOT / 'scripts/build-native-virtio-bindings.py', '--component', 'NativeVirtioBlock',
         '--directory', ROOT / 'build/native-virtio-block-verified')
trust = ROOT / 'build/original-static-system-trust/original-static-system.bundle'
if not trust.is_file():
    static = volumes / 'static-trustcache.bin'
    if not static.is_file():
        data = (ROOT.parent.parent / 'ipsw/21H461/044-76040-038.dmg.trustcache').read_bytes()

        def der(blob, at):
            tag, size = blob[at], blob[at + 1]
            at += 2
            if size & 0x80:
                size, at = int.from_bytes(blob[at:at + (size & 0x7f)], 'big'), at + (size & 0x7f)
            return tag, blob[at:at + size], at + size

        _, sequence, _ = der(data, 0)
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
print('OS root inputs ready: unixnu defconfig qemu_root_defconfig, build kernel, then build payload with',
      f'--trust-cache {trust}')
