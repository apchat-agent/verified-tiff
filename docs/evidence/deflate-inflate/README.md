# Deflate prefix and bulk evidence

Full proof gates passed: baseline 1,809 obligations/128 negative checks;
prefix 1,815/130; bulk 1,823/133. All report zero proof errors. Each receipt
binds all 126 checked core source files. The core did not change between these
proof runs and the complete run-3 runtime suites; the isolated checked paths
were the Linux lane root, work/prefix-repo and work/bulk-repo.

The operator's 2026-10-18 amendment authorized the two pinned dependency helpers.
Both archive checksums passed. Complete baseline, prefix and bulk runtime suites
now pass, including Zstandard, LERC, extended VP8L and VP8. Complete ordinary C
and GCC ASan/UBSan suites also pass. Times and exit codes are in runtime-run3.json.
Passing per-phase extracts are test-run3.log; blocked run-2 test.log files
are labeled historical outcome extracts. Full logs are archived outside the repo.
C run-3 extracts have the -run3 suffix.

Both candidates matched all 11 baseline Deflate fixtures byte for byte. The
20,000-iteration smoke fuzzer passed. The trusted FP/SIMD boundary is unchanged.

benchmark/ retains run-2 raw samples, exact input/output/proof/source/binary
hashes, and the driver. Six workloads improved; gray16-deflate-p2 regressed
20.7%. The amendment requires profiling and correcting that regression when
its cause is within this lane; that follow-up is separate from these two steps.

mutants/ retains the actual cargo-mutants invocation and output. It found zero
sites inside the selected Verus source files, so it provides no mutation-test
coverage. Five explicit semantic proof mutations were rejected by the full gate.

The run-3 follow-up is complete in [period-two/](period-two/README.md): fresh
1,824-obligation/134-negative full gate, complete runtime/C/sanitizer checks,
source audit and exact fixture comparisons all pass. Profiling identified tiny
distance-two chunks as the regression cause; a proved alternating-byte fill fixes
it. The final nine-sample pair improves all seven workloads, including gray16
Predictor 2 from 23.5573 to 20.0303 ms. Original measurements above remain historical.
Cargo-mutants again discovers zero sites; the new period-two proof mutation is
rejected separately. The recorded failed first final-gate attempt is explicitly
superseded by the complete successful restart.

The pre-publication review is addressed in [fix/](fix/README.md), including
bounded setup, small-block per-step measurements, broader exact JPEG/corpus
comparisons and a standalone reproduction recipe. Earlier results above describe
their historical eight-bit implementations. See [review notes](REVIEW-NOTES.md)
for the proof-first interpretation of the first two red commits.

## Compact publication (FIX2)

Full pre-packaging files are preserved in the lane's
`evidence-fix/archive-full/deflate-inflate/`, with paths relative to this directory.
Large runtime/build logs now contain selected outcome lines, not complete logs.
Proof logs and source receipts remain published; no verification was rerun.
Large JSON records use lossless gzip (`gzip -dc FILE.json.gz`); schemas, all
timing samples and hashes are unchanged. References to historical `.json`
filenames in execution scripts denote the decompressed files. Decompress into
a separate working directory before using those historical scripts.
`publication-sha256.json` covers every published file except itself; its digest
is recorded in REPORT-FIX2.md outside this repository.
