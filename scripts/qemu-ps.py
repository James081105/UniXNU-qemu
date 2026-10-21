#!/usr/bin/env python3
"""List the processes of the kernel that a run-qemu.py run is executing, through its QEMU monitor.

  qemu-ps.py [--run RUN_DIR] [--value SYMBOL[:BYTES] ...] [--log FILE]

Walks the kernel's allproc list with QEMU's "x" command (virtual addresses through the current translation; the kernel
half is mapped in every context) and prints PID, state and name. --value prints kernel globals instead, as unsigned
little-endian numbers of BYTES bytes (default 4), for example the display's native_legacy_stage. --log writes the
kernel log, which this board shows neither on the screen nor on the serial port: the message buffer (msgbufp) saved
with QMP memsave, in order. RUN_DIR defaults to the newest out/qemu/run/<time>; the run must still be going. The
struct proc offsets are the defaults of UniXNU's scripts/qemu-entry/gdb-stop.py.
"""
import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT.parent
LIST, PID, STAT, COMM = 0, 96, 100, 1468          # p_list.le_next, p_pid, p_stat, p_comm
STATES = {1: 'idle', 2: 'run', 3: 'stop', 4: 'zombie', 5: 'reaped'}

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--run', type=Path, help='run directory (default: the newest)')
parser.add_argument('--value', action='append', default=[], metavar='SYMBOL[:BYTES]', help='print a kernel global')
parser.add_argument('--log', type=Path, metavar='FILE', help='write the kernel log')
args = parser.parse_args()
run = args.run or max((WORK / 'run').iterdir())
match = re.search(r'-qmp unix:([^,]+),', (run / 'command.txt').read_text())
if not match:
    raise SystemExit(f'no QMP socket in {run}/command.txt')
sock = match[1]


def qmp(name, **arguments):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(60)
        for attempt in range(50):          # QEMU takes one QMP client at a time; run-qemu.py's screenshots use it too
            try:
                s.connect(sock)
                break
            except ConnectionRefusedError:
                if attempt == 49:
                    raise
                time.sleep(0.2)
        stream = s.makefile('rwb', buffering=0)
        stream.readline()
        for request in ({'execute': 'qmp_capabilities'}, {'execute': name, 'arguments': arguments}):
            stream.write((json.dumps(request) + '\n').encode())
            while True:
                reply = json.loads(stream.readline())
                if 'return' in reply or 'error' in reply:
                    break
    if 'error' in reply:
        raise SystemExit(f'{name} {arguments}: {reply["error"]}')
    return reply['return']


def hmp(line):
    return qmp('human-monitor-command', **{'command-line': line})


def read(address, count):
    text = hmp(f'x /{count}bx {address:#x}')
    values = re.findall(r'0x([0-9a-f]{2})\b', text.split(':', 1)[1] if ':' in text else '')
    if len(values) != count:
        raise SystemExit(f'cannot read {address:#x}: {text.strip()}')
    return bytes(int(value, 16) for value in values)


symbols = subprocess.check_output(['xcrun', 'nm', WORK / 'kernel/kernel.macho'], text=True)


def symbol(name):
    found = re.search(rf'^([0-9a-f]{{16}}) \w _{re.escape(name)}$', symbols, re.M)
    if not found:
        raise SystemExit(f'{name} is not a kernel symbol')
    return int(found[1], 16)


if args.value:
    for request in args.value:
        name, _, size = request.partition(':')
        value = int.from_bytes(read(symbol(name), int(size or 4)), 'little')
        print(f'{name} = {value} ({value:#x})')
    raise SystemExit(0)
if args.log:
    # struct msgbuf: magic, size, write index, read index (ints), then the buffer pointer. The buffer is a ring.
    buffer = int.from_bytes(read(symbol('msgbufp'), 8), 'little')
    header = read(buffer, 24)
    size, write = (int.from_bytes(header[at:at + 4], 'little') for at in (4, 8))
    ring = args.log.resolve().with_suffix('.ring')
    qmp('memsave', val=int.from_bytes(header[16:24], 'little'), size=size, filename=str(ring))
    data = ring.read_bytes()
    ring.unlink()
    args.log.write_bytes((data[write:] + data[:write]).replace(b'\0', b''))
    print(f'kernel log: {args.log}')
    raise SystemExit(0)
proc = int.from_bytes(read(symbol('allproc'), 8), 'little')
rows = []
while proc and len(rows) < 1024:
    pid = int.from_bytes(read(proc + PID, 4), 'little', signed=True)
    state = int.from_bytes(read(proc + STAT, 1), 'little')
    name = read(proc + COMM, 17).split(b'\0')[0].decode(errors='replace')
    rows.append((pid, STATES.get(state, str(state)), name))
    proc = int.from_bytes(read(proc + LIST, 8), 'little')
for pid, state, name in sorted(rows):
    print(f'{pid:5d}  {state:6s}  {name}')
print(f'{len(rows)} processes')
