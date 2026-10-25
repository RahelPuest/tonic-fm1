#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# An update in fm1-emu: a project made and edited under OLD (pattern B01, three trigs, back to HOME), then NEW booted
# on that project. fm1-live's --flash restores the whole chip, the app area too, so the project's areas (the data
# area 0x97000..0xE4FFF and the settings pair 0xFC000..0xFDFFF) are moved onto a flash that NEW was installed on.
#   tools/upgrade_test.sh OLD.fwsc NEW.fwsc     (screens in build/emu-tests/upgrade_*.ppm)
set -e
cd "$(dirname "$0")/.."
LIVE=${FM1_EMU:-../ref/fm1-emu}/target/release/fm1-live
OUT=build/emu-tests
mkdir -p "$OUT"
OLD=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
NEW=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
rm -f "$OUT/up_old.bin" "$OUT/up_new.bin"
K=""; i=0
while [ $i -lt 16 ]; do K="$K,$((2000 + i * 40)):e6:1"; i=$((i + 1)); done
"$LIVE" --fwsc "$OLD" --bench 4000 --flash "$OUT/up_old.bin" >/dev/null
FM1_BENCH_KEYS="${K#,},2900:REC,3000:REC^,3300:n0,3400:n0^,3700:n4,3800:n4^,4500:REC,4600:REC^,5000:HOME,5100:HOME^" \
    FM1_BENCH_PANEL=$OUT/upgrade_old.ppm "$LIVE" --fwsc "$OLD" --bench 10000 --flash "$OUT/up_old.bin" >/dev/null
"$LIVE" --fwsc "$NEW" --bench 1500 --flash "$OUT/up_new.bin" >/dev/null
python3 - "$OUT" <<'PY'
import sys
o = sys.argv[1]
a = bytearray(open(f"{o}/up_new.bin", "rb").read())
b = open(f"{o}/up_old.bin", "rb").read()
a[0x97000:0xE5000] = b[0x97000:0xE5000]
a[0xFC000:0xFE000] = b[0xFC000:0xFE000]
open(f"{o}/up_new.bin", "wb").write(a)
PY
FM1_BENCH_KEYS='100:e0:0' FM1_BENCH_PANEL=$OUT/upgrade_new.ppm "$LIVE" --fwsc "$NEW" --bench 3000 --flash "$OUT/up_new.bin" | grep -E "flash:"
echo "compare $OUT/upgrade_old.ppm (OLD) and $OUT/upgrade_new.ppm (NEW): the same project (B01, its trigs)"
# the sound banks (pool, library: version 2 under 0.2..0.4.1, version 3 from 0.5): EDIT, the PRESETS list, a turn
# past the factory sounds into the library
FM1_BENCH_KEYS='100:e0:0,600:EDIT,700:EDIT^,1200:e6:1,1400:e6:-1' FM1_BENCH_PANEL=$OUT/upgrade_list.ppm "$LIVE" --fwsc "$NEW" --bench 2500 --flash "$OUT/up_new.bin" >/dev/null
echo "the PRESETS list on the updated project: $OUT/upgrade_list.ppm"
