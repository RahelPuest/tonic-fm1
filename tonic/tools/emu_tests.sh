#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The device binary in fm1-emu (joakimeriksson/fm1-emu, built in ../ref/fm1-emu): boot, play, the CPU benchmark,
# and a save / power-cycle / recall check on the real firmware. Screens and audio in build/emu-tests/.
#   tools/emu_tests.sh            (after python3 ../base/tools/build.py . and tools/bench_build.sh)
set -e
cd "$(dirname "$0")/.."
EMU=${FM1_EMU:-../ref/fm1-emu}
LIVE=$EMU/target/release/fm1-live
OUT=build/emu-tests
mkdir -p "$OUT"
[ -x "$LIVE" ] || { echo "no $LIVE: cargo build --release -p fm1-live in $EMU"; exit 1; }
FW=build/tonic.fwsc
rm -f "$OUT/flash.bin"
echo "1. first boot (the demo project is written), PLAY at 3 s"
FM1_BENCH_KEYS='3000:PLAY,3100:PLAY^' FM1_BENCH_PANEL=$OUT/boot_play.ppm FM1_BENCH_WAV=$OUT/boot_play.raw \
    "$LIVE" --fwsc "$FW" --bench 8000 --flash "$OUT/flash.bin" | grep -E "flash:|emulated"
echo "2. pattern B01, three trigs in grid recording, stopped: the flush"
K=""; i=0
while [ $i -lt 16 ]; do K="$K,$((2000 + i * 40)):e6:1"; i=$((i + 1)); done
FM1_BENCH_KEYS="${K#,},2900:REC,3000:REC^,3300:n0,3400:n0^,3700:n4,3800:n4^,4100:n9,4200:n9^,4500:REC,4600:REC^,5000:HOME,5100:HOME^" \
    FM1_BENCH_PANEL=$OUT/edit.ppm "$LIVE" --fwsc "$FW" --bench 10000 --flash "$OUT/flash.bin" | grep -E "flash:"
echo "3. power cycle: B01 and its trigs must be back"
FM1_BENCH_KEYS='100:e0:0' FM1_BENCH_PANEL=$OUT/recall.ppm "$LIVE" --fwsc "$FW" --bench 3000 --flash "$OUT/flash.bin" | grep -E "flash:"
python3 - "$OUT" <<'PY'
import sys
from PIL import Image
out = sys.argv[1]
a = Image.open(f"{out}/edit.ppm").convert("RGB").crop((400, 40, 880, 520))
b = Image.open(f"{out}/recall.ppm").convert("RGB").crop((400, 40, 880, 520))
diff = sum(1 for p, q in zip(a.getdata(), b.getdata()) if p != q)
print(f"recall vs. edit: {diff} pixels differ in the screen area (the CPU figure may differ)")
sys.exit(0 if diff < 2000 else 1)
PY
if [ -f build/bench/tonic-bench.fwsc ]; then
    echo "4. CPU benchmark: 8 heavy voices, every effect"
    FM1_BENCH_KEYS='100:e0:0' FM1_BENCH_PANEL=$OUT/bench.ppm "$LIVE" --fwsc build/bench/tonic-bench.fwsc --bench 6000 | grep -E "emulated"
    echo "   (the load is on the HOME screen: $OUT/bench.ppm)"
fi
