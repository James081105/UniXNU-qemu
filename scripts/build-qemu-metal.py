#!/usr/bin/env python3
"""Build the Metal device whose work runs on this Mac (package/QemuMetal), for the run-qemu.py --metal guest.

  build-qemu-metal.py host         qemu-metal-host, the Mac side (QemuMetalHost.mm)
  build-qemu-metal.py instruments  the guest side built for this Mac, against qemu-metal-host: NativeMetal's frontend over
                                   QemuMetalRemote.cpp, and the comparison instrument (QemuMetalCheck.mm)
  build-qemu-metal.py check        both, then the instrument against a host on a scratch window file: this Mac's QuartzCore
                                   renders a layer tree through the frontend, the window and the host, and the pixels are checked
  build-qemu-metal.py guest        the plugin bundle for iPadOS (QemuMetal.bundle, 4 KiB-signed), the owner trust cache that adds its
                                   CDHash to the two original modules, and a copy of the 1 GiB Data seed that carries the bundle in
                                   /private/var/ngpu/addons (the kernel's addon mount, sheng-gpu-addon=1) and the render server's
                                   framebuffer preferences

Output stays under build/qemu-metal. The frontend is UniXNU's (drivers/NativeMetal/NativeBridgeMetalFrontend.mm), unchanged; only
the bridge under it (bridge/NativeMetalObjects.h) is this board's.

`bundle` also links QemuMetalScanout.mm, this board's display hand-off client (sheng-fb-gpu=1): the thread that waits for the display
driver's frames and has the GPU copy them into the scanout. It is what makes native_gop_gpu_offers move at all -- the presenter refuses
every offer until a client is inside waitSwap -- and it is the reason a boot could show an open, rendering Metal device and a black screen
at the same time.
"""
import hashlib
import json
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / 'kits/qemu/QemuMetal'
METAL = ROOT / 'drivers/NativeMetal'
OUT = ROOT / 'build/qemu-metal'
FLAGS = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-deprecated-declarations']


def run(command, **kwargs):
    subprocess.run([str(part) for part in command], check=True, **kwargs)


def host():
    OUT.mkdir(parents=True, exist_ok=True)
    run(['xcrun', 'clang', '-O1', '-g', '-c', ROOT / 'kits/AirNir/Library.c', '-o', OUT / 'Library.o'])
    run(['xcrun', 'clang++', *FLAGS, '-fobjc-arc', '-I', KIT, '-I', ROOT / 'kits/AirNir', KIT / 'QemuMetalHost.mm', OUT / 'Library.o',
         '-framework', 'Foundation', '-framework', 'Metal', '-o', OUT / 'qemu-metal-host'])
    return OUT / 'qemu-metal-host'


def instruments():
    OUT.mkdir(parents=True, exist_ok=True)
    includes = ['-I', METAL, '-I', METAL / 'bridge', '-I', KIT]
    run(['xcrun', 'clang++', *FLAGS, *includes, '-c', KIT / 'QemuMetalRemote.cpp', '-o', OUT / 'QemuMetalRemote-mac.o'])
    run(['xcrun', 'clang++', *FLAGS, '-fobjc-arc', *includes, METAL / 'NativeBridgeMetalFrontend.mm', KIT / 'QemuMetalCheck.mm',
         KIT / 'QemuMetalLibraries.mm', KIT / 'QemuMetalGaps.mm', OUT / 'QemuMetalRemote-mac.o', '-framework', 'Foundation', '-framework', 'Metal',
         '-framework', 'QuartzCore',
         '-framework', 'CoreGraphics', '-framework', 'CoreImage', '-framework', 'ImageIO', '-framework', 'IOSurface',
         '-o', OUT / 'qemu-metal-check'])
    return OUT / 'qemu-metal-check'


def check():
    server, instrument = host(), instruments()
    window = OUT / 'check-window.bin'
    with window.open('wb') as f:
        f.truncate(512 << 20)
    log = OUT / 'check-host.log'
    log.unlink(missing_ok=True)
    process = subprocess.Popen([str(server), '--window', str(window), '--log', str(log)])
    try:
        time.sleep(0.5)
        result = subprocess.run([str(instrument), str(OUT)], capture_output=True, text=True, timeout=300,
                                env=dict(os.environ, QEMU_METAL_WINDOW=str(window)))
    finally:
        process.terminate()
        process.wait()
        window.unlink(missing_ok=True)
    (OUT / 'qemu-metal-check.log').write_text(result.stdout + result.stderr)
    print('\n'.join(line for line in (result.stdout + result.stderr).splitlines()
                    if line.startswith(('PASS', 'FAIL', 'missing', 'frontend', '%')) or 'pixels differ' in line or 'native-metal' in line))
    print(f'pictures: {OUT / "check-mac.png"}, {OUT / "check-remote.png"}; host log: {log}')
    if result.returncode or 'PASS' not in result.stdout:
        sys.exit(f'the CARenderer check failed (exit {result.returncode}); see {OUT / "qemu-metal-check.log"}')


IDENTIFIER = 'tech.xvanturing.QemuMetal'
BUNDLE = OUT / 'QemuMetal.bundle'
SEED = ROOT / '.cache/firmware/owner-data/owner-data-1g.dbr'
ORIGINAL_TRUST = ROOT / 'build/original-static-system-trust/original-static-system.bundle'


def bundle():
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
    ios = ['-target', 'arm64-apple-ios17.0', '-isysroot', sdk]
    includes = ['-I', METAL, '-I', METAL / 'bridge', '-I', KIT]
    shutil.rmtree(BUNDLE, ignore_errors=True)
    BUNDLE.mkdir(parents=True)
    run(['xcrun', 'clang++', *ios, *FLAGS, *includes, '-c', KIT / 'QemuMetalRemote.cpp', '-o', OUT / 'QemuMetalRemote-ios.o'])
    executable = BUNDLE / 'QemuMetal'
    # -Wl,-no_uuid is a HYPOTHESIS UNDER TEST, not a fix that has been shown to
    # work.  Measured on two consecutive runs whose only difference is board.toml:
    # CDHash a1fce7f8870b9ea068add82ddaa4bfd5b1343aad (run 41108366328) versus
    # 68518f18395a53dac252b52b2481902af17faa0c (run 41117052708), from the same
    # sources, the same flags and the same toolchain.  So the bundle's build is not
    # reproducible, and board.toml's trust_cache_sha256 cannot pin a digest that
    # moves -- a pin guaranteed to go stale converts "the bundle changed" into
    # "the board is wrong" on every run.
    #
    # Nothing in this repository has been shown to vary: there is no __DATE__,
    # __TIME__, arc4random or gettimeofday in package/QemuMetal or
    # drivers/NativeMetal, and the include paths are inside one tree at one pinned
    # commit.  The remaining candidate is ld64's per-link LC_UUID, which lands in
    # the first page and is therefore inside the ad-hoc signature's hashed range.
    # -no_uuid omits it; a bundle loaded with dlopen does not need one.
    #
    # What this buys either way: two runs with the SAME CDHash prove the build is
    # reproducible and the digest can be pinned again, and two runs with different
    # CDHashes refute this hypothesis and name the next candidate.  What it must
    # not do is silently produce a third answer nobody reads.
    run(['xcrun', 'clang++', *ios, *FLAGS, '-fobjc-arc', *includes, METAL / 'NativeBridgeMetalFrontend.mm', KIT / 'QemuMetalPlugin.mm',
         KIT / 'QemuMetalScanout.mm', KIT / 'QemuMetalLibraries.mm', KIT / 'QemuMetalGaps.mm', OUT / 'QemuMetalRemote-ios.o', '-bundle',
         '-Wl,-dead_strip', '-Wl,-no_uuid', '-lobjc', '-framework', 'Foundation', '-framework', 'Metal',
         '-framework', 'IOSurface', '-framework', 'CoreFoundation', '-o', executable])
    run(['strip', '-x', executable])
    (BUNDLE / 'Info.plist').write_bytes(plistlib.dumps(dict(
        CFBundleIdentifier=IDENTIFIER, CFBundleExecutable='QemuMetal', CFBundlePackageType='BNDL', CFBundleVersion='1',
        CFBundleShortVersionString='1', NSPrincipalClass='QemuMetalDevice', MinimumOSVersion='17.0'), fmt=plistlib.FMT_XML))
    # The kernel's code signing takes 4 KiB pages here (as sheng's plugin, build-adreno-metal-plugin.py).
    run(['codesign', '--force', '--sign', '-', '--pagesize', '4096', '--identifier', IDENTIFIER, BUNDLE])
    run(['codesign', '--verify', '--strict', BUNDLE])
    detail = subprocess.run(['codesign', '--display', '--verbose=4', BUNDLE], capture_output=True, text=True, check=True).stderr
    if not re.search(r'^Page size=4096$', detail, re.M):
        sys.exit('the signature must use 4 KiB pages')
    cdhash = re.search(r'^CDHash=([a-f0-9]{40})$', detail, re.M)[1]
    (OUT / 'trust.json').write_text(json.dumps(dict(cdhash=cdhash, bundle=BUNDLE.name), indent=2) + '\n')
    print('bundle', BUNDLE, 'CDHash', cdhash)
    return cdhash


def trust(cdhash):
    # The owner module of make-owner-trust-bundle.py with this bundle's CDHash, then the two original modules byte for byte.
    original = ORIGINAL_TRUST.read_bytes()
    if hashlib.sha256(original).hexdigest() != '7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7':
        sys.exit(f'{ORIGINAL_TRUST} is not the original static/System trust bundle')
    offsets = struct.unpack_from('<2I', original, 4)
    originals = [original[offsets[0]:offsets[1]], original[offsets[1]:]]
    hashes = [bytes.fromhex(cdhash)]
    identifier = uuid.uuid5(uuid.NAMESPACE_URL, IDENTIFIER + ':' + cdhash)
    own = struct.pack('<I16sI', 2, identifier.bytes, len(hashes)) + b''.join(h + bytes([2, 0, 0, 0]) for h in hashes)
    modules = [own, *originals]
    starts, cursor = [], 16
    for module in modules:
        starts.append(cursor)
        cursor += len(module)
    out = OUT / 'owner-trust.bundle'
    out.write_bytes(struct.pack('<4I', 3, *starts) + b''.join(modules))
    print('owner trust cache', out, hashlib.sha256(out.read_bytes()).hexdigest())
    return out


def seed():
    sys.path.insert(0, str(ROOT / 'drivers/DarwinBoot/Test'))
    import packed_ramdisk
    output = OUT / 'owner-data-1g-metal.dbr'
    output.unlink(missing_ok=True)
    logical, extents = packed_ramdisk.regions(SEED.read_bytes())
    with tempfile.TemporaryDirectory(prefix='metal-seed-', dir=OUT) as work:
        raw = Path(work) / 'seed.raw'
        with raw.open('wb') as f:
            f.truncate(logical)
            for offset, data in extents:
                f.seek(offset)
                f.write(data)
        mount = Path(work) / 'mount'
        mount.mkdir()
        attached = plistlib.loads(subprocess.check_output(['hdiutil', 'attach', '-imagekey', 'diskimage-class=CRawDiskImage', '-nobrowse',
                                                           '-owners', 'off', '-mountpoint', str(mount), '-plist', str(raw)]))
        try:
            if not (mount / 'mobile').is_dir():
                sys.exit('The Data seed has no /mobile')
            target = mount / 'ngpu/addons' / BUNDLE.name
            shutil.rmtree(target, ignore_errors=True)
            target.parent.mkdir(parents=True, exist_ok=True)
            for directory in (mount / 'ngpu', mount / 'ngpu/addons'):
                os.chmod(directory, 0o755)
            shutil.copytree(BUNDLE, target, symlinks=False)
            for path in [target, *target.rglob('*')]:
                os.chmod(path, 0o755 if path.is_dir() or path.name == 'QemuMetal' else 0o644)
            # The render server picks a 10-bit "deep" and a compressed framebuffer unless told not to, and reads that when it starts,
            # before the plugin runs: the preferences of its user (mobile) and root, its own domain and the global one, as on sheng.
            for home, name in [(h, n) for h in ('mobile', 'root') for n in ('backboardd.plist', '.GlobalPreferences.plist')]:
                directory = mount / home / 'Library/Preferences'
                directory.mkdir(parents=True, exist_ok=True)
                file = directory / name
                values = plistlib.loads(file.read_bytes()) if file.exists() else {}
                values.update(CAEnableDeepFramebuffer=False, CAEnableFramebufferCompression=False, CAEnableIOSurfaceCompression=False)
                file.write_bytes(plistlib.dumps(values, fmt=plistlib.FMT_BINARY))
                os.chmod(file, 0o644)
        finally:
            subprocess.run(['hdiutil', 'detach', attached['system-entities'][0]['dev-entry']], check=True)
        run([sys.executable, ROOT / 'drivers/DarwinBoot/Test/packed_ramdisk.py', raw, output])
    print('Data seed', output)
    return output


def guest():
    OUT.mkdir(parents=True, exist_ok=True)
    trust(bundle())
    seed()


if __name__ == '__main__':
    action = sys.argv[1] if len(sys.argv) > 1 else 'check'
    {'host': host, 'instruments': instruments, 'check': check, 'guest': guest}[action]()
