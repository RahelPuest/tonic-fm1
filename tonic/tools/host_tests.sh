#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Every host-simulator script in tests/host (the dbg* ones aside): prints each script's failures, then a summary.
#   tools/host_tests.sh            (after python3 ../base/tools/build.py . --host)
cd "$(dirname "$0")/.."
fail=0
for t in tests/host/*.txt; do
    f=$(basename "$t" .txt)
    case $f in dbg*) continue ;; esac
    mkdir -p "build/shots/$f"
    out=$(build/host/tonic_host "$t" "build/shots/$f" 2>&1)
    n=$(echo "$out" | grep -ciE 'fail|mismatch')
    if [ "$n" != 0 ]; then
        echo "== $f: $n"
        echo "$out" | grep -iE 'fail|mismatch' | head -8
        fail=$((fail + 1))
    fi
done
echo "host scripts: $fail failing"
[ "$fail" = 0 ]
