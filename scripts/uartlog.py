"""Timestamping UART logger.

Each line is stamped with host epoch time on arrival, so elapsed intervals can
be measured against a wall clock independent of the target's own timer.
"""
import time

import serial

LOG = '/home/lab/claude/uart1.log'

ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
buf = b''
with open(LOG, 'a', buffering=1) as f:
    f.write(f"[{time.time():.3f}] --- logger attached ---\n")
    while True:
        d = ser.read(4096)
        if not d:
            continue
        buf += d
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            text = line.decode('utf-8', 'replace').rstrip()
            f.write(f"[{time.time():.3f}] {text}\n")
