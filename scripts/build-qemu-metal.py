#!/usr/bin/env python3
"""Build the Metal device whose work runs on this Mac (package/QemuMetal), for the run-qemu.py --metal guest.

  build-qemu-metal.py host         qemu-metal-host, the Mac side (QemuMetalHost.mm)
  build-qemu-metal.py instruments  the guest side built for this Mac, against qemu-metal-host: NativeMetal's frontend over
                                   QemuMetalRemote.cpp, and UniXNU's CARenderer instrument (bridge/carenderer_host.mm)
  build-qemu-metal.py check        both, then the instrument against a host on a scratch window file: this Mac's QuartzCore
                                   renders a layer tree through the frontend, the window and the host, and the pixels are checked

Output stays under build/qemu-metal. The frontend is UniXNU's (drivers/NativeMetal/NativeBridgeMetalFrontend.mm), unchanged; only
the bridge under it (bridge/NativeMetalObjects.h) is this board's.
"""
import os
import subprocess
import sys
import time
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
         OUT / 'QemuMetalRemote-mac.o', '-framework', 'Foundation', '-framework', 'Metal', '-framework', 'QuartzCore',
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


if __name__ == '__main__':
    action = sys.argv[1] if len(sys.argv) > 1 else 'check'
    {'host': host, 'instruments': instruments, 'check': check}[action]()
