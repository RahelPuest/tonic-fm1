#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The CPU benchmark firmware: boots into 8 heavy voices with every effect on; the home screen shows the render's
# load (CPU %) and any late renders (XR). Output: build/bench/tonic-bench.fwsc. Then the normal build again.
set -e
cd "$(dirname "$0")/.."
. tools/env.sh
echo '#define TONIC_BENCH 1' > app/tonic_bench.h
python3 ../base/tools/build.py . >/dev/null || { rm -f app/tonic_bench.h; exit 1; }
mkdir -p build/bench
cp build/tonic.fwsc build/bench/tonic-bench.fwsc
rm -f app/tonic_bench.h
python3 ../base/tools/build.py . >/dev/null
echo "bench: build/bench/tonic-bench.fwsc"
