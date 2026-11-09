#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Two perf_probe packages (build/perf/OLD and NEW: tonic.fwsc, bench1..6.fwsc) run in fm1-emu at 720 MHz (no overload,
# no voice shedding), their audio compared sample for sample from 1 s on (the base's volume ramp after boot is timed by
# the main loop).   tools/fastcmp.sh OLD NEW
cd "$(dirname "$0")/.."
L=${FM1_EMU:-../ref/fm1-emu}/target/release/fm1-live
for O in "build/perf/$1" "build/perf/$2"; do
    for b in 1 2 3 4 5 6; do FM1_BENCH_KEYS='100:e0:0' FM1_BENCH_WAV=$O/fast$b.raw $L --mhz 720 --fwsc $O/bench$b.fwsc --bench 8000 >/dev/null; done
    rm -f $O/fflash.bin
    FM1_BENCH_KEYS='3000:PLAY,3100:PLAY^' FM1_BENCH_WAV=$O/fastdemo.raw $L --mhz 720 --fwsc $O/tonic.fwsc --bench 9000 --flash $O/fflash.bin >/dev/null
done
python3 - "build/perf/$1" "build/perf/$2" <<'PY'
import sys, numpy as np
for n in ["fastdemo", "fast1", "fast2", "fast3", "fast4", "fast5", "fast6"]:
    a = np.fromfile(f"{sys.argv[1]}/{n}.raw", dtype=np.int16); b = np.fromfile(f"{sys.argv[2]}/{n}.raw", dtype=np.int16)
    m = min(len(a), len(b)); d = np.nonzero(a[88200:m] != b[88200:m])[0] + 88200
    print(f"{n:9s} {'identical' if not len(d) else 'DIFFERS at frame %d' % (d[0] // 2)} ({m // 2} frames)")
PY
