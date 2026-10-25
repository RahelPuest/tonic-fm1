#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""fm1-live's PC samples summed by function, from the ELF's symbols (nm -n).
   FM1_BENCH_PCS=all FM1_BENCH_KEYS='100:e0:0' fm1-live --fwsc build/tonic.fwsc --bench 6000 > PCS.txt
   tools/pc_profile.py build/tonic.elf PCS.txt [N]
Stock fm1-live prints only its 25 hottest PCs, as percentages; a whole profile needs two small changes in
crates/fm1-live/src/main.rs (the sample report): `take(25)` -> all of them when FM1_BENCH_PCS=all, and the count
after the percentage (`println!("pc {pc:#010x} {:5.1}% {n}", ...)`). Build that copy apart
(CARGO_TARGET_DIR=... cargo build --release -p fm1-live) and leave the checkout as it is."""
import bisect, re, subprocess, sys
elf, pcs = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
syms = []
for line in subprocess.run(["nm", "-n", elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in "tTwW" and not p[2].startswith("."):
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
tot, by = 0.0, {}
for line in open(pcs):
    m = re.match(r"pc (0x[0-9a-f]+)\s+[\d.]+% (\d+)", line)
    if m:
        pc, w = int(m.group(1), 16), float(m.group(2))
        i = bisect.bisect_right(addrs, pc) - 1
        name = syms[i][1] if i >= 0 else "?"
        by[name] = by.get(name, 0) + w
        tot += w
for n, w in sorted(by.items(), key=lambda x: -x[1])[:top]:
    print(f"{100 * w / tot:6.2f}%  {n}")
