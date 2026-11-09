#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The engine tests (tests/eng_test.c) built natively with the Braids units, then run, then the golden renders.
#   tools/eng_tests.sh             (GOLDEN_NEW=1: write build/test/golden.txt afresh)
cd "$(dirname "$0")/.."
B=build/test/obj
mkdir -p "$B" build/test/out
for f in engine/mi/braids_voice.cc engine/mi/braids/*.cc engine/mi/stmlib/utils/random.cc; do
    c++ -std=c++11 -O2 -ffp-contract=off -Iengine/mi -Iengine/mi/shim -Iengine -Icore -I../base/include \
        -c "$f" -o "$B/$(basename "$f" .cc).o" 2>&1 | grep -E 'error'
done
rm -f build/test/eng_test
cc -std=gnu99 -O2 -ffp-contract=off -Icore -Iengine -Iengine/mi -I../base/include tests/eng_test.c core/*.c engine/*.c \
    "$B"/*.o -lc++ -o build/test/eng_test -lm 2>&1 | grep -E 'error'
build/test/eng_test build/test/out 2>&1 | grep -E 'FAIL|all passed'
[ -n "$GOLDEN_NEW" ] && rm -f build/test/golden.txt && GOLDEN=build/test/golden.txt build/test/eng_test build/test/out golden >/dev/null
GOLDEN=build/test/golden.txt build/test/eng_test build/test/out golden | grep -E 'DIFFERS|PASS|FAIL'
