#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The device binary's load and output in fm1-emu, for comparing builds: the demo (first boot, PLAY at 3 s) and the
# three CPU benchmarks (FM, VA, DRUM: TONIC_BENCH 1..3). Prints each run's share of idle cycles, its CPU figure
# (from the HOME screen) and a hash of its audio: a build that only optimizes must keep every hash.
#   tools/perf_probe.sh [TAG]      (builds the benchmark variants itself; the normal build again at the end)
set -e
cd "$(dirname "$0")/.."
. tools/env.sh >/dev/null 2>&1
LIVE=${FM1_EMU:-../ref/fm1-emu}/target/release/fm1-live
OUT=build/perf/${1:-now}
mkdir -p "$OUT"
for b in 1 2 3; do
    echo "#define TONIC_BENCH $b" > app/tonic_bench.h
    python3 ../base/tools/build.py . >/dev/null || { rm -f app/tonic_bench.h; exit 1; }
    cp build/tonic.fwsc "$OUT/bench$b.fwsc"
done
rm -f app/tonic_bench.h
python3 ../base/tools/build.py . | grep -E "image"
cp build/tonic.fwsc "$OUT/tonic.fwsc"
rm -f "$OUT/flash.bin"
FM1_BENCH_KEYS='3000:PLAY,3100:PLAY^' FM1_BENCH_WAV=$OUT/demo.raw FM1_BENCH_PANEL=$OUT/demo.ppm \
    "$LIVE" --fwsc "$OUT/tonic.fwsc" --bench 9000 --flash "$OUT/flash.bin" | sed -n 's/.*, \([0-9]*% of cycles skipped as idle\)/demo:   \1/p'
for b in 1 2 3; do
    FM1_BENCH_KEYS='100:e0:0' FM1_BENCH_WAV=$OUT/bench$b.raw FM1_BENCH_PANEL=$OUT/bench$b.ppm \
        "$LIVE" --fwsc "$OUT/bench$b.fwsc" --bench 8000 | sed -n "s/.*, \([0-9]*% of cycles skipped as idle\)/bench$b: \1/p"
done
python3 - "$OUT" <<'PY'
import hashlib, sys
o = sys.argv[1]
# the first 7 s of each (a faster build can capture one block more in the same emulated time)
for n in ["demo", "bench1", "bench2", "bench3"]:
    print(f"{n:7s} audio {hashlib.sha256(open(f'{o}/{n}.raw', 'rb').read()[:7 * 44100 * 4]).hexdigest()[:16]}")
PY
python3 - "$OUT" <<'PY'
import sys
from PIL import Image
o = sys.argv[1]
im = Image.new("RGB", (240, 4 * 30))
for i, n in enumerate(["demo", "bench1", "bench2", "bench3"]):
    im.paste(Image.open(f"{o}/{n}.ppm").convert("RGB").crop((640, 90, 880, 120)), (0, 30 * i))
im.resize((480, 240)).save(f"{o}/cpu.png")
print(f"the CPU figures: {o}/cpu.png (demo, FM, VA, DRUM)")
PY
