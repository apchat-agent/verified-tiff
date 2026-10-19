#!/bin/bash
set -euo pipefail
trap 'echo "$?" > work/run3-final-measurements.exit' EXIT
while test ! -f work/run3-fixed-verify.exit || test ! -f work/run3-fixed-runtime.exit; do sleep 5; done
test "$(cat work/run3-fixed-verify.exit)" = 0
test "$(cat work/run3-fixed-runtime.exit)" = 0
source scripts/env.sh
export TMPDIR="$PWD/work/tmp" CARGO_BUILD_JOBS=1
mkdir -p work/profile-fixed
cc -std=c11 -O3 -g -pg -no-pie -DVERIFIED_ADAPTER -Icompat/include benchmarks/reader.c target/compat/tiff_compat.o target/compat/stubs.o target/release/libverified_tiff_capi.a -ldl -lpthread -lm -o work/profile-fixed/reader
root="$PWD"
(cd work/profile-fixed; taskset -c 0 ./reader "$root/work/baseline/gray16-deflate-p2.tif" native 30000 memory > result.json; gprof -p ./reader gmon.out > flat.txt)
cc -std=c11 -O3 -g -Wall -Wextra -Werror -DVERIFIED_ADAPTER -Icompat/include benchmarks/reader.c -Ltarget/compat -Wl,-rpath,"$PWD/target/compat" -ltiff -o target/reader-benchmark/verified
echo 0 > work/run3-fixed-bench-prep.exit
python3 - <<'PREP'
from pathlib import Path
import shutil,json
root=Path.cwd();work=root/'work/baseline-repo/work'
assert json.loads((root/'work/profile-fixed/result.json').read_text())['hash']==json.loads((root/'work/baseline-repo/work/profile/result.json').read_text())['hash']
shutil.copy2(work/'baseline/results.json',root/'work/run3-pre-before.json')
for name,source in [('bulk-runtime-done.exit','run3-fixed-test.exit'),('bulk-compat.exit','run3-fixed-compat.exit'),('bulk-sanitize.exit','run3-fixed-sanitize.exit'),('bulk-fuzz.exit','run3-fixed-fuzz.exit'),('bulk-extra-checks.exit','run3-fixed-runtime.exit'),('bulk-full-gate.exit','run3-fixed-verify.exit'),('candidate-fixtures-preparation.exit','run3-fixed-fixtures.exit'),('bulk-bench-preparation.exit','run3-fixed-bench-prep.exit')]:
    p=work/name;assert p.is_symlink();p.unlink();p.symlink_to(root/'work'/source)
old=root/'work/baseline';assert old.is_dir() and not old.is_symlink()
old.rename(root/'work/baseline-run2');old.symlink_to(work/'baseline')
PREP
(cd work/baseline-repo; source scripts/env.sh; python3 work/deflate-bench.py baseline) > work/run3-final-before-bench.log 2>&1
cp work/baseline-repo/work/baseline/results.json work/run3-final-before.json
python3 work/deflate-bench.py after-run3-final > work/run3-final-after-bench.log 2>&1
python3 benchmarks/check-deflate-regression.py work/run3-final-before.json work/after-run3-final/results.json > work/run3-regression-green.log 2>&1
bash work/run3-mutants.sh
