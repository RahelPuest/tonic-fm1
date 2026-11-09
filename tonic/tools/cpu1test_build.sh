#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The CPU1 test firmware: TONIC, which at its start tests the second core on the device and shows the results
# (app/cpu1test.c), then goes on as TONIC (HOME). Output: build/cpu1test/tonic-cpu1test.fwsc. Then the normal build.
set -e
cd "$(dirname "$0")/.."
. tools/env.sh
echo '#define TONIC_CPU1TEST 1' > app/tonic_cpu1test.h
python3 ../base/tools/build.py . ${1:+--release "$1"} | grep -E "image|c1|ram_text|ram_dsp|error|overflow" || true
mkdir -p build/cpu1test
if [ -n "$1" ]; then cp "build/tonic-$1.fwsc" build/cpu1test/tonic-cpu1test.fwsc; else cp build/tonic.fwsc build/cpu1test/tonic-cpu1test.fwsc; fi
cp build/tonic.elf build/cpu1test/tonic-cpu1test.elf
rm -f app/tonic_cpu1test.h
python3 ../base/tools/build.py . >/dev/null
echo "cpu1 test: build/cpu1test/tonic-cpu1test.fwsc"
