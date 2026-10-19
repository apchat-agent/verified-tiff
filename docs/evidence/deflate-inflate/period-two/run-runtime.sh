#!/bin/bash
export TMPDIR="$PWD/work/tmp" CARGO_BUILD_JOBS=1
trap 'echo "$?" > work/run3-fixed-runtime.exit' EXIT
{ time -p bash scripts/test.sh; } > work/run3-fixed-test.log 2>&1
status=$?; echo "$status" > work/run3-fixed-test.exit
if [ "$status" -ne 0 ]; then exit "$status"; fi
{ time -p bash scripts/test-compat.sh; } > work/run3-fixed-compat.log 2>&1
status=$?; echo "$status" > work/run3-fixed-compat.exit
if [ "$status" -ne 0 ]; then exit "$status"; fi
(export VTIFF_SANITIZE=1 VTIFF_COMPAT_OUTPUT="$PWD/target/compat-sanitize" CC=cc ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1; { time -p bash scripts/test-compat.sh; }) > work/run3-fixed-sanitize.log 2>&1
status=$?; echo "$status" > work/run3-fixed-sanitize.exit
if [ "$status" -ne 0 ]; then exit "$status"; fi
{ time -p bash scripts/fuzz-smoke.sh 20000; } > work/run3-fixed-fuzz.log 2>&1
status=$?; echo "$status" > work/run3-fixed-fuzz.exit
if [ "$status" -ne 0 ]; then exit "$status"; fi
python3 work/fixture-hashes.py fixed-fixtures > work/run3-fixed-fixtures.log 2>&1
status=$?; echo "$status" > work/run3-fixed-fixtures.exit
exit "$status"
