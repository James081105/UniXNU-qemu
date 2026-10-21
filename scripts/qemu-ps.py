#!/usr/bin/env python3
"""List the processes of the kernel that a run-qemu.py run is executing, through its QEMU monitor.

  qemu-ps.py [RUN_DIR]

Walks the kernel's allproc list with QEMU's "x" command (virtual addresses through the current translation; the kernel
half is mapped in every context) and prints PID, state and name. RUN_DIR defaults to the newest out/qemu/run/<time>;
the run must still be going. The struct proc offsets are the defaults of UniXNU's scripts/qemu-entry/gdb-stop.py.
"""
import json
from pathlib import Path
import re
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT.parent
LIST, PID, STAT, COMM = 0, 96, 100, 1468          # p_list.le_next, p_pid, p_stat, p_comm
STATES = {1: 'idle', 2: 'run', 3: 'stop', 4: 'zombie', 5: 'reaped'}

run = Path(sys.argv[1]) if len(sys.argv) > 1 else max((WORK / 'run').iterdir())
match = re.search(r'-qmp unix:([^,]+),', (run / 'command.txt').read_text())
if not match:
    raise SystemExit(f'no QMP socket in {run}/command.txt')
sock = match[1]


def hmp(line):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(10)
        s.connect(sock)
        stream = s.makefile('rwb', buffering=0)
        stream.readline()
        for request in ({'execute': 'qmp_capabilities'},
                        {'execute': 'human-monitor-command', 'arguments': {'command-line': line}}):
            stream.write((json.dumps(request) + '\n').encode())
            while True:
                reply = json.loads(stream.readline())
                if 'return' in reply or 'error' in reply:
                    break
    if 'error' in reply:
        raise SystemExit(f'{line}: {reply["error"]}')
    return reply['return']


def read(address, count):
    text = hmp(f'x /{count}bx {address:#x}')
    values = re.findall(r'0x([0-9a-f]{2})\b', text.split(':', 1)[1] if ':' in text else '')
    if len(values) != count:
        raise SystemExit(f'cannot read {address:#x}: {text.strip()}')
    return bytes(int(value, 16) for value in values)


symbols = subprocess.check_output(['xcrun', 'nm', WORK / 'kernel/kernel.macho'], text=True)
allproc = int(re.search(r'^([0-9a-f]{16}) \w _allproc$', symbols, re.M)[1], 16)
proc = int.from_bytes(read(allproc, 8), 'little')
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
