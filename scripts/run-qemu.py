#!/usr/bin/env python3
"""Boot the qemu board's payload in QEMU virt (GICv3, Cortex-A710, 6 GiB, ramfb).

Run in the build tree (out/qemu/tree) after "unixnu build package kernel payload". The script stages an ESP from
UniXNU (as BOOTAA64.EFI), the files of the payload volume and DarwinBoot (scripts/build-darwinboot-efi.py), boots it,
keeps the serial log and takes a screenshot over QMP at a fixed interval. Output goes to out/qemu/run/<time>/.
"""
import argparse
import datetime
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT.parent
MEMORY_MIB = 6144                    # [payload] dram_size of board.toml

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--seconds', type=int, default=300, help='stop QEMU after this long')
parser.add_argument('--every', type=int, default=20, help='seconds between screenshots')
parser.add_argument('--display', default='none', help='none, or cocoa for a window on this Mac')
parser.add_argument('--native-user', action='store_true',
                    help="boot UniXNU's static test process (build-native-user-fixture.py) as launchd from mockfs on md0 "
                         'and read its results before QEMU stops')
parser.add_argument('--os-root', action='store_true',
                    help='attach the OS disk image of the IPSW read-only as a VirtIO block device, the root of a kernel '
                         'built with qemu_root_defconfig (scripts/build-os-root.py)')
parser.add_argument('--accel', choices=('tcg', 'hvf'), default='tcg',
                    help='tcg emulates a Cortex-A710; hvf runs the guest on this Mac\'s CPU (Hypervisor.framework). The '
                         'payload must be built for the same accelerator: UNIXNU_QEMU_ACCEL=hvf for hvf')
args = parser.parse_args()
if args.native_user and args.os_root:
    parser.error('--native-user boots from md0; leave out --os-root')

qemu = shutil.which('qemu-system-aarch64')
if not qemu:
    raise SystemExit('qemu-system-aarch64 not found (brew install qemu)')
code = Path(qemu).resolve().parents[1] / 'share/qemu/edk2-aarch64-code.fd'
payload = WORK / 'payload/payload.img'
unixnu = WORK / 'package/Build/Out/AARCH64-DEBUG/UniXNU.efi'
for path, step in ((code, 'brew install qemu'), (payload, 'unixnu build payload'), (unixnu, 'unixnu build package')):
    if not path.is_file():
        raise SystemExit(f'{path} missing ({step})')
darwinboot = ROOT / 'build/darwinboot-efi/DarwinBoot.efi'
if not darwinboot.is_file():
    subprocess.run(['python3', ROOT / 'scripts/build-darwinboot-efi.py'], check=True)

out = WORK / 'run' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
esp = out / 'esp'
esp.mkdir(parents=True)
subprocess.run(['mcopy', '-s', '-n', '-i', payload, '::/EFI', esp], check=True, env=dict(os.environ, MTOOLS_SKIP_CHECK='1'))
(esp / 'EFI/BOOT').mkdir(parents=True, exist_ok=True)
shutil.copyfile(unixnu, esp / 'EFI/BOOT/BOOTAA64.EFI')
config = (esp / 'EFI/UniXNU/unixnu.toml').read_text()
if 'Payloads/DarwinBoot.efi' not in config:
    raise SystemExit('the payload does not load DarwinBoot as a driver; board.toml [payload] loader must not be "firmware"')
shutil.copyfile(darwinboot, esp / 'EFI/UniXNU/Payloads/DarwinBoot.efi')

spec = importlib.util.spec_from_file_location('make_qemu_adt', ROOT / 'scripts/make-qemu-adt.py')
adt_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adt_tool)
adt = esp / 'EFI/UniXNU/Payloads/kernel.macho.adt'
tree, _ = adt_tool.parse(adt.read_bytes())


def node(entry, name):
    if entry[0] == name:
        return entry
    return next((found for child in entry[2] if (found := node(child, name))), None)


# The kernel's timer runs at the device tree's timebase: TCG's counter is 62.5 MHz, under HVF it is the host's.
timebase = struct.unpack('<I', dict(node(tree, 'cpu0')[1])['timebase-frequency'])[0]
expected = 62500000 if args.accel == 'tcg' else int(subprocess.check_output(['sysctl', '-n', 'hw.tbfrequency'], text=True))
if timebase != expected:
    raise SystemExit(f'the payload\'s device tree has a {timebase} Hz timebase, {args.accel} needs {expected} Hz: rebuild '
                     f'the payload with UNIXNU_QEMU_ACCEL={args.accel}')

fixture = ROOT / 'build/native-user-fixture'
if args.native_user:
    # The test process is the whole md0 image (a single Mach-O, which mockfs serves as /sbin/launchd); its own trust
    # cache admits it under the platform-only AMFI policy.
    if not (fixture / 'launchd.macho').is_file():
        subprocess.run(['python3', ROOT / 'scripts/build-native-user-fixture.py'], check=True)
    payloads = esp / 'EFI/UniXNU/Payloads'
    shutil.copyfile(fixture / 'launchd.macho', payloads / 'launchd.macho')
    shutil.copyfile(fixture / 'trustcache.bundle', payloads / 'kernel.macho.trustcache')
    if 'initrd = ' in config:
        raise SystemExit('the payload already has an initrd; build it without --data-ramdisk for --native-user')
    (esp / 'EFI/UniXNU/unixnu.toml').write_text(
        config.replace('args = "', 'initrd = "Payloads/launchd.macho"\nargs = "rd=md0 ', 1))
    chosen = adt_tool.child(tree, 'chosen')
    chosen[1] = [item for item in chosen[1] if item[0] != 'root-matching']   # a memory root, not a matched volume
    memory_map = adt_tool.child(chosen, 'memory-map')
    for key in ('RAMDisk', 'TrustCache'):                                      # DarwinBoot fills these placeholders
        adt_tool.put(memory_map, key, bytes(16))
    adt.write_bytes(adt_tool.serialize(tree))

work = Path(tempfile.mkdtemp(prefix='unixnu-qemu-'))   # short path: a QMP socket name is limited to 104 bytes
variables = work / 'vars.fd'
with variables.open('wb') as f:
    f.truncate(code.stat().st_size)
sock = work / 'qmp.sock'
command = [qemu, '-M', 'virt,virtualization=off,secure=off,gic-version=3', '-accel', args.accel,
           '-cpu', 'cortex-a710' if args.accel == 'tcg' else 'host',
           '-m', str(MEMORY_MIB), '-smp', '1',
           '-drive', f'if=pflash,format=raw,readonly=on,file={code}', '-drive', f'if=pflash,format=raw,file={variables}',
           '-drive', f'if=none,id=esp,format=raw,readonly=on,file=fat:{esp}', '-device', 'virtio-blk-pci,drive=esp',
           '-device', 'ramfb', '-device', 'qemu-xhci', '-device', 'usb-kbd', '-nic', 'none',
           # A cocoa window scales the 800x600 frame buffer to its size (drag its corner, or View > Zoom To Fit).
           '-display', args.display + (',zoom-to-fit=on' if args.display == 'cocoa' else ''), '-no-reboot',
           '-serial', f'file:{out / "serial.log"}', '-qmp', f'unix:{sock},server=on,wait=off']
if args.os_root:
    # The whole disk image as the host sees it, read through its read-only attachment; the policy that
    # build-owner-os-policy.py signed covers exactly these bytes.
    image = ROOT / '.cache/firmware/target-volumes/044-76040-038.dmg'
    if not image.is_file():
        raise SystemExit(f'{image} missing (scripts/build-os-root.py)')
    attached = [i for i in plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))['images']
                if Path(i['image-path']).resolve() == image.resolve() and i.get('writeable') is False]
    if not attached:
        attached = [plistlib.loads(subprocess.check_output(['hdiutil', 'attach', '-nomount', '-readonly', '-noverify',
                                                            '-noautofsck', '-plist', image]))]
    disk = next(e['dev-entry'] for e in attached[0]['system-entities'] if e.get('content-hint') == 'GUID_partition_scheme')
    # The SystemOS member is a plain APFS container; its policy (build-owner-systemos-policy.py) covers the file.
    systemos = ROOT / '.cache/firmware/target-volumes/044-76034-038.dmg'
    if not systemos.is_file():
        raise SystemExit(f'{systemos} missing (scripts/build-os-root.py)')
    command += ['-global', 'virtio-mmio.force-legacy=false', '-blockdev', json.dumps({
                    'driver': 'raw', 'node-name': 'os', 'read-only': True,
                    'file': {'driver': 'host_device', 'filename': '/dev/r' + Path(disk).name, 'read-only': True,
                             'locking': 'off'}}),
                '-device', 'virtio-blk-device,drive=os',
                '-blockdev', json.dumps({'driver': 'raw', 'node-name': 'systemos', 'read-only': True,
                                         'file': {'driver': 'file', 'filename': str(systemos), 'read-only': True,
                                                  'locking': 'off'}}),
                '-device', 'virtio-blk-device,drive=systemos',
                # The pointer and keyboard of the QEMU window for package/VirtioInput.cpp: a click and drag is a touch,
                # Home or F1 is the Home button. QEMU sends its input to them once the driver has started them.
                '-device', 'virtio-tablet-device', '-device', 'virtio-keyboard-device']
(out / 'command.txt').write_text(' '.join(str(part) for part in command) + '\n')
process = subprocess.Popen([str(part) for part in command], stdout=open(out / 'qemu.log', 'w'), stderr=subprocess.STDOUT)


def png(ppm, target):
    data = ppm.read_bytes()
    fields, at = [], 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[at:end])
        at = end
    width, height = int(fields[1]), int(fields[2])
    pixels = data[at + 1:]
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(tag, body):
        return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body) & 0xffffffff)
    target.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                       chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def qmp(name, **arguments):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(10)
        s.connect(str(sock))
        stream = s.makefile('rwb', buffering=0)
        stream.readline()
        for request in ({'execute': 'qmp_capabilities'}, {'execute': name, 'arguments': arguments}):
            stream.write((json.dumps(request) + '\n').encode())
            while True:
                reply = json.loads(stream.readline())
                if 'return' in reply or 'error' in reply:
                    break
        return reply


start, shots = time.time(), []
while time.time() - start < args.seconds and process.poll() is None:
    time.sleep(args.every)
    if process.poll() is not None:
        break
    ppm = work / 'screen.ppm'
    try:
        if 'error' not in qmp('screendump', filename=str(ppm)):
            shots.append(out / f'screen-{int(time.time() - start):03d}s.png')
            png(ppm, shots[-1])
    except OSError as error:
        print('screendump failed:', error)
if args.native_user and process.poll() is None:
    # Where the CPU is and what the test process stored: getpid() of the first process is 1, and hw.pagesize is
    # 4096 on the native 4 KiB user ABI. QEMU's "x" reads virtual addresses through the current translation.
    addresses = json.loads((fixture / 'addresses.json').read_text())

    def hmp(line):
        reply = qmp('human-monitor-command', **{'command-line': line})
        return reply.get('return', json.dumps(reply.get('error')))

    registers = hmp('info registers')
    results = hmp(f'x /4gx {addresses["_results"]:#x}')
    page = hmp(f'x /1gx {addresses["_user_page_size"]:#x}')
    (out / 'native-user.txt').write_text(registers + '\n' + results + '\n' + page)
    pc = re.search(r'\bPC=([0-9a-f]+)', registers)
    level = re.search(r'\bEL(\d)[th]\b', registers)
    words = [int(value, 16) for value in re.findall(r'0x([0-9a-f]{16})', results)]
    size = re.findall(r'0x([0-9a-f]{16})', page)
    print(f'native user: PC {pc.group(1) if pc else "?"} (checkpoint {addresses["_checkpoint"]:x}), '
          f'EL{level.group(1) if level else "?"}, getpid {words[0] if words else "?"}, '
          f'sysctl status {words[1] if len(words) > 1 else "?"}, hw.pagesize {int(size[0], 16) if size else "?"}')
if process.poll() is None:
    try:
        qmp('quit')
    except OSError:
        process.kill()
process.wait(timeout=30)
shutil.rmtree(work, ignore_errors=True)
log = (out / 'serial.log').read_bytes().decode('utf-8', 'replace')
for line in [line for line in log.splitlines() if line.startswith(('UNIXNU:', 'DARWINBOOT:'))][-4:]:
    print(line)
print(f'qemu exit {process.returncode}; {len(shots)} screenshots in {out}')
if shots:
    print(f'last screen: {shots[-1]}')
