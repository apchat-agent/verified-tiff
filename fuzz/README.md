# Fuzzing infrastructure

Coverage-guided targets and regression replay for the verified TIFF reader.
They are test infrastructure, not part of the verified core: the decoder
contracts checked here are the published API promises (see
`docs/trusted-boundary.md`), re-checked dynamically.

## Prerequisites

- Linux x86-64 host. The C targets link ELF static libraries and build the
  Rust capi for `x86_64-unknown-linux-gnu`; `fuzz-c/build.sh` refuses other
  hosts.
- Repo bootstrap: `python3 scripts/bootstrap.py` (pinned default toolchain,
  cargo fetch, repo-local `.tools/`).
- Pinned nightly used by every fuzz build in this delivery:
  `rustup toolchain install nightly-2026-10-18 --profile minimal`, then
  `cargo +nightly-2026-10-18 install --locked cargo-fuzz` (cargo-fuzz 0.13.2
  was used). `fuzz-c/build.sh` calls the same pinned nightly.
- `python3 scripts/upstream.py` fetches the pinned libtiff 4.7.1 (and its
  pinned codec dependencies) needed to link the differential target.
- This directory is its own cargo workspace; `fuzz/Cargo.lock` is tracked, so
  the libfuzzer-sys dependency is pinned (0.4.x line locked at generation).

## Rust target `inspect_decode`

The target (`fuzz_targets/inspect_decode.rs`) runs every input through
`fuzz/oracle.rs`, which decodes each accepted page twice and checks decoded
length, determinism, sentinel framing of output and scratch, charged work,
and error-not-partial-success. Oracle limits: max_input 32768,
max_dimension 128, max_output 16384, max_directories 8, max_tags 128,
max_segments 128.

- Seed corpus: the files under `tests/fixtures/`.
- Campaign (4 workers, 30 minutes):
  `cargo +nightly-2026-10-18 fuzz run inspect_decode <corpus-dir> -- -jobs=4 -workers=4 -max_total_time=1800 -rss_limit_mb=2048 -timeout=10`
- Regression replay (ordinary gate): `cargo test --locked --test fuzz_regressions`.

## C targets (`fuzz-c/`)

Build with `bash fuzz-c/build.sh`; outputs land in `target/fuzz-c/`:

- `inspect_decode_c` — libFuzzer target over the compatibility adapter:
  every directory, every strip/tile, `TIFFReadRGBAImage`, 1 MiB input cap,
  `VTIFFOpenOptionsSetLimits(1MiB, 1MiB, work)`, 64 MiB strip/tile loop cap,
  bounded directory walk, and sentinel guard bytes around every buffer.
  Campaign: `target/fuzz-c/inspect_decode_c <corpus-dir> -jobs=4 -workers=4 -max_total_time=1800 -rss_limit_mb=2048 -timeout=10`.
- `replay_c` — ordinary (sanitizer-built) replay runner:
  `target/fuzz-c/replay_c tests/fixtures/fuzz-regressions-c`; every corpus
  file must run without crash or hang. Wired into `scripts/test-compat.sh`.
- `diff_decode` — differential target: decodes each input with the
  compatibility layer (dlopened) and statically linked pinned libtiff 4.7.1
  in one process, then compares `TIFFReadRGBAImage` output and every compared
  strip/tile segment byte-for-byte. JPEG and OJPEG are excluded from
  comparison: no universal numerical bound is documented for them (see
  `docs/trusted-boundary.md`). Three further classes are counted, not
  compared, because the documented policies diverge by design: decodes where
  either engine reported a message (malformed-input recovery, see
  `docs/fax.md`; warning-only and error messages are counted separately),
  complex 16/32/64-bit sample transport on byte-swapped files (the adapter
  reverses per component, `docs/reader-features.md`, while libtiff swaps the
  whole word; each engine's transport is pinned by
  `compat/tests/byteorder_regression.c`), the adapter's stricter profile
  refusing damaged streams libtiff partially repairs (`docs/profile.md`),
  and diverging metadata interpretation on malformed tags (libtiff rejects
  wrongly typed known tags and falls back, the adapter reads them
  independently of the file's stored type, `docs/metadata-contracts.md`;
  the RGBA probe is skipped unless both engines report the same photometric
  presence and value).
  One fixture per one-sided failure class lives in
  `tests/fixtures/diff-triage/`. Every input whose size is a multiple of 4
  additionally opens both engines with strip chopping ("rC") and counts the
  structural agreement separately.

  The adapter `.so` is resolved from `VTIFF_COMPAT_SO`, defaulting to
  `target/compat/libtiff.so.6` relative to the working directory.

  Counters print at exit; `DIFF-CODEC comp=V compared=N skipped=N` lines key
  compared and skipped decodes by compression tag value (unit: per segment
  or RGBA probe):

  | counter | unit | meaning |
  |---|---|---|
  | `inputs` | per libFuzzer input, including ones below 8 bytes or above 1 MiB | every call |
  | `both_opened` | per input | both engines opened the file |
  | `inputs_with_compare` | per input | at least one clean byte comparison ran |
  | `compared` | per clean comparison: one non-empty segment or one RGBA image | byte comparison performed |
  | `mismatches` | per differing comparison | the first difference aborts |
  | `skipped_warning` / `skipped_error` | per segment or RGBA probe | both engines returned data, but only warnings / at least one error was reported |
  | `ours_failed` / `ref_failed` / `both_failed` | per segment or RGBA probe | adapter failed / libtiff failed / both failed |
  | `skipped_complex` | per directory | documented complex-sample transport divergence |
  | `skipped_jpeg` | per directory | JPEG or OJPEG compression excluded |
  | `skipped_meta` | per directory (at most two counts) | structural divergence: compression, geometry, photometric presence or value, tiled flag, strip/tile size or count |
  | `dir_divergent` | per directory transition | `TIFFReadDirectory` results differ |
  | `chop_both_opened` | per chop-mode input | both engines opened in "rC" mode |
  | `chop_divergent` | per chop-mode input | chopped structure (tiled flag, strip/tile size or count) disagrees |

Instrumentation scope: the Rust capi is built with sancov instrumentation,
while ASan/UBSan stay C-side, as in the repo's own sanitizer runs. The
dlopened compatibility `.so` and the static libtiff used by `diff_decode`
are uninstrumented in this build, so libFuzzer coverage feedback for the
differential target covers only the harness; its value is the exhaustive
re-comparison of the accumulated corpus.

## Mutation oracle

The `fuzz-oracle` bin is mutation-testing infrastructure (cargo-mutants
examinability wrapper for `fuzz/oracle.rs`), not a usable oracle command. It
is gated behind a feature; build/run it only as
`cargo run --locked --features mutation-oracle --bin fuzz-oracle`.
