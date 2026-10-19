#!/usr/bin/env bash
# Build the C-layer libFuzzer target and replay runner against the compat capi.
# Rust coverage/CI instrumentation only (ASan/UBSan stay C-side, as in the
# repo's sanitizer runs); outputs land in protected target/fuzz-c/.
set -euo pipefail
source "$(dirname "$0")/../scripts/env.sh"
cd "$TIFF_ROOT"
out="$TIFF_ROOT/target/fuzz-c"
mkdir -p "$out"
compiler="${CC:-clang}"
common=(-std=c11 -O1 -g -fno-omit-frame-pointer -Icompat -Icompat/include)
export RUSTFLAGS=""
RUSTFLAGS="--cfg fuzzing -Cpasses=sancov-module -Cllvm-args=-sanitizer-coverage-level=4 -Cllvm-args=-sanitizer-coverage-inline-8bit-counters -Cllvm-args=-sanitizer-coverage-pc-table -Cllvm-args=-sanitizer-coverage-trace-compares -Cdebug-assertions -Ccodegen-units=1" \
  cargo +nightly build --locked --release -p verified-tiff-capi --target x86_64-unknown-linux-gnu
unset RUSTFLAGS
lib=target/x86_64-unknown-linux-gnu/release/libverified_tiff_capi.a
bash scripts/build-compat.sh >"$out/build-compat.log" 2>&1
"$compiler" "${common[@]}" -fsanitize=fuzzer,address,undefined -c compat/tiff_compat.c -o "$out/tiff_compat.o"
"$compiler" "${common[@]}" -fsanitize=fuzzer,address,undefined -c target/compat/stubs.c -o "$out/stubs.o"
"$compiler" "${common[@]}" -fsanitize=fuzzer,address,undefined fuzz-c/harness.c "$out/tiff_compat.o" "$out/stubs.o" "$lib" -ldl -lpthread -lm -o "$out/inspect_decode_c"
"$compiler" "${common[@]}" -fsanitize=address,undefined -DREPLAY_MAIN fuzz-c/harness.c "$out/tiff_compat.o" "$out/stubs.o" "$lib" -ldl -lpthread -lm -o "$out/replay_c"
reflib=.tools/tiff-4.7.1/libtiff/.libs/libtiff.a
"$compiler" "${common[@]}" -fsanitize=fuzzer,address,undefined fuzz-c/diffharness.c "$reflib" \
  .tools/webp/lib/libwebp.a .tools/webp/lib/libsharpyuv.a .tools/jpeg/lib/libjpeg.a \
  -ldl -lpthread -lz -lm -o "$out/diff_decode"
echo "C fuzz targets: $out/inspect_decode_c $out/replay_c $out/diff_decode"
