"""Drive Zephyr's tests/drivers/uart/uart_basic_api on the Genio board.

The suite is declared `harness: keyboard`, so twister cannot run it — it waits for
a human to type at the console. This supplies the keystrokes over the same serial
line and collects the ztest verdicts.
"""
import os
import re
import subprocess
import sys
import time

import serial

PORT = '/dev/ttyUSB0'
# Board-specific bits, overridable so the same driver serves both EVKs.
SETUP = os.environ.get('SETUP', '/root/claude_aary/setup-g700.sh')
IMAGE = os.environ.get('IMAGE', 'zephyr-g700-uartapi.bin')
PROMPT = b'Please send characters to serial console'


def restart_cell():
    subprocess.run(
        ['adb', 'shell', f'{SETUP} {IMAGE}'],
        capture_output=True, timeout=60,
    )


def main():
    restart_cell()
    ser = serial.Serial(PORT, 115200, timeout=0.2)
    time.sleep(0.5)
    ser.reset_input_buffer()

    buf = b''
    answered = 0
    last_len = 0
    idle_since = time.time()
    deadline = time.time() + 90

    while time.time() < deadline:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk
            idle_since = time.time()

        # Answer each prompt once, as it appears.
        while buf.count(PROMPT) > answered:
            answered += 1
            time.sleep(0.3)
            # a spread of printable characters, then a newline
            ser.write(bytes(range(0x41, 0x5b)) + b'\r\n')
            ser.flush()
            time.sleep(0.5)

        if b'PROJECT EXECUTION' in buf:
            time.sleep(0.5)
            buf += ser.read(4096)
            break

        # stop if the target has gone quiet with nothing pending
        if time.time() - idle_since > 12 and len(buf) == last_len:
            break
        last_len = len(buf)

    ser.close()

    text = buf.decode('utf-8', 'replace')
    print(text)
    print("=" * 70)
    print(f"prompts answered: {answered}")

    passes = re.findall(r'PASS - (\S+)', text)
    fails = re.findall(r'FAIL - (\S+)', text)
    skips = re.findall(r'SKIP - (\S+)', text)
    print(f"PASS: {len(passes)}  {passes}")
    print(f"FAIL: {len(fails)}  {fails}")
    print(f"SKIP: {len(skips)}  {skips}")
    summary = [l.strip() for l in text.splitlines() if 'PROJECT EXECUTION' in l or 'TESTSUITE' in l]
    for s in summary:
        print(f"  {s}")
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
