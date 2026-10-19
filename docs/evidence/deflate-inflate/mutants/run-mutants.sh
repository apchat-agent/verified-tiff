#!/bin/bash
source scripts/env.sh
set +e
export TMPDIR="$PWD/work/tmp"
export LANE_REAL_CARGO="$(command -v cargo)"
export CARGO="$PWD/work/mutants-cargo-wrapper.py"
export CARGO_BUILD_JOBS=4
# Invoke the installed executable directly so Cargo does not replace the shim.
# timeout is a lane process cap, not a per-mutant test timeout.
timeout 3600 "$CARGO_HOME/bin/cargo-mutants" mutants --in-place --no-shuffle \
    --output work/mutants-final -f src/deflate_bits.rs -f src/deflate.rs \
    -f src/native_stream.rs -f tests/stream_batch.rs --cargo-arg=--locked > work/mutants-final.log 2>&1
status=$?
echo "$status" > work/mutants-final.exit
exit "$status"
