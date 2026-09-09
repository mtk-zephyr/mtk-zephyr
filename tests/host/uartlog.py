#!/usr/bin/env python3
"""Timestamping UART logger.

Stamps every line with host epoch time on arrival, so intervals can be measured
against a wall clock that is independent of the target's own timer. That matters
for the k_sleep test: k_uptime_get() derives from the very timer under test and
cannot detect a misconfigured one.

Only one process may hold the serial port, so this and the interactive host test
drivers cannot run at the same time.
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default='/dev/ttyUSB0')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--log', default='/tmp/uart1.log')
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)
    buf = b''
    with open(args.log, 'a', buffering=1) as f:
        f.write(f"[{time.time():.3f}] --- logger attached to {args.port} ---\n")
        while True:
            d = ser.read(4096)
            if not d:
                continue
            buf += d
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                f.write(f"[{time.time():.3f}] {line.decode('utf-8', 'replace').rstrip()}\n")


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        pass
