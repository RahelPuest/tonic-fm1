#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The CPU1 test firmware's result code (app/cpu1test.c, c1t_make_code) read back.
   tools/cpu1code.py XXXXX-XXXXX-... [build/cpu1test/tonic-cpu1test.elf]
   (dashes, spaces and case do not matter; I L O U read as 1 1 0 V). Code version 1 (35 characters) or 2 (45: 13
   tests, where a crash or a reset stopped it); with the test build's ELF a crash's address is named."""
import os
import subprocess
import sys

A = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
TESTS1 = ["START", "MAILBOX", "EXACT", "SPEED", "PARALLEL", "RAM RD", "FLASH RD", "RAM WR", "AUDIO", "STRESS", "RESTART"]
TESTS2 = TESTS1[:9] + ["STR DSP", "STR FLASH", "STR RAM", "RESTART"]
STATE = ["OK", "FAIL", "SKIP", "?"]
VALS = [("START", "us", 1), ("MAILBOX average", "ns", 2), ("MAILBOX longest", "us", 3), ("MAILBOX misread", "", 0),
        ("EXACT wrong (of 2000)", "", 0), ("SPEED core 1 (% of core 0)", "%", 0), ("PARALLEL (x/100)", "", 1),
        ("RAM RD wrong (of 500)", "", 0), ("FLASH RD wrong (of 500)", "", 0), ("RAM WR words wrong", "", 0),
        ("AUDIO bad blocks", "", 0), ("AUDIO longest wait", "us", 1), ("AUDIO blocks", "", 6),
        ("STRESS bad (all three)", "", 0), ("STRESS jobs", "", 8), ("misread jobs in all", "", 0)]


def where(elf, pc):
    for tool in ("llvm-nm", "nm"):
        try:
            out = subprocess.run([tool, "-n", "-C", elf], capture_output=True, text=True, check=True).stdout
            break
        except (OSError, subprocess.CalledProcessError):
            out = ""
    best = None
    for line in out.splitlines():
        f = line.split(None, 2)
        if len(f) == 3 and f[1] in "tTwW":
            a = int(f[0], 16)
            if a <= pc:
                best = (a, f[2])
    return f"{best[1]}+0x{pc - best[0]:x}" if best else "?"


def main():
    args = [a for a in sys.argv[1:] if not a.endswith(".elf")]
    elf = next((a for a in sys.argv[1:] if a.endswith(".elf")), None)
    s = "".join(args).upper().replace("-", "").replace(" ", "")
    s = s.replace("I", "1").replace("L", "1").replace("O", "0").replace("U", "V")
    bits = []
    for ch in s:
        if ch not in A:
            sys.exit(f"not a code character: {ch}")
        v = A.index(ch)
        bits += [(v >> k) & 1 for k in range(5)]
    pos = [0]

    def get(n):
        v = sum(bits[pos[0] + k] << k for k in range(n))
        pos[0] += n
        return v
    v2 = len(s) > 35
    TESTS = TESTS2 if v2 else TESTS1
    states = [get(2) for _ in TESTS]
    if v2:
        stop, step, vec, crashed, pc, on1, up = get(1), get(4), get(8), get(1), get(25) << 1, get(1), get(7)
    vals = [get(8) for _ in VALS]
    restart, version = get(4), get(4)
    body = pos[0]
    crc = 0xFFFF
    for b in bits[:body]:
        crc = ((crc << 1) ^ (0x1021 if ((crc >> 15) ^ b) & 1 else 0)) & 0xFFFF
    got = get(16)
    print("checksum", "OK" if got == crc else f"WRONG (a typing error?) {got:04x} != {crc:04x}", f"(code version {version})")
    for t, st in zip(TESTS, states):
        print(f"  {t:9s} {STATE[st]}")
    for (name, unit, sh), v in zip(VALS, vals):
        lo = v << sh
        print(f"  {name:28s} {'>=' if v == 255 else '  '}{lo}{('..' + str(lo + (1 << sh) - 1)) if sh and v != 255 else ''} {unit}")
    print(f"  {'RESTART ok (of 10)':28s}   {restart}")
    if v2:
        if stop:
            print(f"  stopped in {TESTS[step] if step < len(TESTS) else step} by a {'crash' if crashed else 'reset (a hang)'}")
        if crashed:
            region = 'flash' if pc >= 0x02000000 else 'RAM' if 0x01C00000 <= pc < 0x01C80000 else 'other'
            print(f"  crash: vector {vec}, pc 0x{pc:08x} ({region}"
                  f"{', ' + where(elf, pc) if elf else ''}), on core {1 if on1 else 0}, {up} s after the start")


if __name__ == "__main__":
    main()
