"""Stage a read-only ESP from the payload for a QEMU observation with UniXNU's scripts/qemu-entry/gdb-stop.py --esp.

The ESP is the one run-qemu.py stages: the payload volume's files, UniXNU as BOOTAA64.EFI and DarwinBoot as a driver.
The kernel is replaced by the given one (the payload's copy carries the panic screen patch, so its digest differs from
the kernel step's), and extra kernel arguments go in front of the payload's.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT.parent


def stage(esp, kernel, arguments):
    payload = WORK / 'payload/payload.img'
    unixnu = WORK / 'package/Build/Out/AARCH64-DEBUG/UniXNU.efi'
    for path, step in ((payload, 'unixnu build payload'), (unixnu, 'unixnu build package')):
        if not path.is_file():
            raise SystemExit(f'{path} missing ({step})')
    darwinboot = ROOT / 'build/darwinboot-efi/DarwinBoot.efi'
    if not darwinboot.is_file():
        subprocess.run([sys.executable, ROOT / 'scripts/build-darwinboot-efi.py'], check=True)
    shutil.rmtree(esp, ignore_errors=True)
    esp.mkdir(parents=True)
    subprocess.run(['mcopy', '-s', '-n', '-i', payload, '::/EFI', esp], check=True,
                   env=dict(os.environ, MTOOLS_SKIP_CHECK='1'))
    (esp / 'EFI/BOOT').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(unixnu, esp / 'EFI/BOOT/BOOTAA64.EFI')
    payloads = esp / 'EFI/UniXNU/Payloads'
    shutil.copyfile(darwinboot, payloads / 'DarwinBoot.efi')
    shutil.copyfile(kernel, payloads / 'kernel.macho')
    config = esp / 'EFI/UniXNU/unixnu.toml'
    config.write_text(config.read_text().replace('args = "', f'args = "{arguments} ', 1))
