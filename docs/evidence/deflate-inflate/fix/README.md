# Pre-publication review follow-up

The final bounded prefix cache has 32 entries (five bits). Code-length and fixed
trees skip construction, and zero-cache reads use the canonical walk directly.
Both complete and resumable paths require 16 pending bytes and transitions before
selecting bulk copying; the resumable path also requires 16 output slots. Ring
and checksum boundaries may shorten an admitted run. Acceptance, exact work,
output and trusted-boundary contracts remain unchanged.

## Final verification and runtime

The complete `bash scripts/verify.sh` run verified **1,824 obligations,
0 errors, 134 negative checks**, exit zero, in
4028.14 seconds. `verification-summary.json` binds all 126 core
sources and both unchanged trusted-boundary source sets. `verify.log` is a
sanitized complete log; the six prefix/bulk mutation logs retain their intended
semantic failures. The source audit is included separately.

The full test script passed 73 Rust tests and all its Python suites
in 411.32 seconds. Ordinary C passed in
72.89 seconds; GCC C ASan/UBSan passed in
150.98 seconds. The smoke run completed 20,000
deterministic mutations. These are tests, not an end-to-end C proof or a
coverage-guided fuzz campaign. Rust internals are not sanitizer-instrumented;
LeakSanitizer was disabled as in the preceding delivery.

The fresh original-corpus script accepted **31/31 unchanged originals**, first
main image only. Its independent libtiff oracle comparison retains the documented
JPEG tolerance. This differs from the adopted manifest's 22 originals plus nine
transcodes. See `original-corpus.json.gz` and `corpus.log`.

The direct baseline/candidate check completed **97 successful comparisons across
69 distinct input hashes**, with exact bytes including **eight JPEG comparisons**.
The groups are 11 repository Deflate TIFF fixtures, five raw zlib fixtures wrapped
as Gray8 TIFFs, 31 originals, and 26 native / 24 RGBA README workloads. CLI
comparisons use the first image. Raw payloads also match Python zlib independently.
Three 130,001-byte raw streams use explicit `--max-dimension 130001` for one-row
wrappers; decoder limits were not changed. `outputs.json.gz` records flags, source,
library and executable hashes, input/output hashes, byte counts and exit codes.

## Controlled performance results

The final small sweep uses 31 randomized serial 100 ms samples per engine/case;
the large sweep uses nine. Both pin one allowed CPU, open fresh handles against
warm memory, check exact outputs before timing, and check every sample checksum.
All validation jobs completed before timing began. Baseline, prefix, bulk and
original labels refer to `9a64643`, `b676d56`, `65cc492` and `283ac23`.
`small.json.gz` binds each built source set and library, and `large.json.gz` also binds
whole-crate positive proof receipts. The coordinator separately required the
complete final gate to pass. Timing is evidence for this host and these inputs,
not a general libtiff-parity claim or a proof of performance.

Small-case medians in milliseconds:

| Case | Baseline | Prefix | Bulk | Original | Final | Final / baseline |
|---|---:|---:|---:|---:|---:|---:|
| w16-strips1 | 0.021214 | 0.032325 | 0.032232 | 0.032406 | 0.021152 | 0.997060 |
| w16-strips64 | 0.896298 | 1.568694 | 1.544471 | 1.562934 | 0.887372 | 0.990042 |
| w64-strips1 | 0.029330 | 0.033978 | 0.034815 | 0.034765 | 0.028311 | 0.965240 |
| w64-strips64 | 1.339429 | 1.715299 | 1.731785 | 1.734416 | 1.336894 | 0.998107 |
| w256-strips1 | 0.033577 | 0.039074 | 0.040826 | 0.041298 | 0.033450 | 0.996219 |
| w256-strips64 | 1.659512 | 1.971618 | 2.097672 | 2.091491 | 1.620500 | 0.976492 |
| w1024-strips1 | 0.055936 | 0.058239 | 0.066085 | 0.065726 | 0.056033 | 1.001741 |
| w1024-strips64 | 3.037201 | 3.089423 | 3.659750 | 3.694986 | 3.111720 | 1.024535 |
| w4096-strips1 | 0.151994 | 0.128184 | 0.160574 | 0.157883 | 0.146157 | 0.961601 |
| w4096-strips64 | 9.198709 | 7.611424 | 9.350087 | 9.386574 | 8.828059 | 0.959706 |
| empty-fixed-0 | 0.020928 | 0.032320 | 0.032139 | 0.032167 | 0.020569 | 0.982859 |
| empty-fixed-8 | 0.103861 | 0.200325 | 0.201485 | 0.198740 | 0.100238 | 0.965113 |
| empty-fixed-64 | 0.669454 | 1.363627 | 1.354484 | 1.373909 | 0.642195 | 0.959282 |
| empty-dynamic-8 | 0.173111 | 0.306023 | 0.307136 | 0.305529 | 0.166451 | 0.961530 |
| empty-dynamic-64 | 1.193521 | 2.178854 | 2.187797 | 2.190852 | 1.167038 | 0.977811 |

Small gate exit: 0; cases over 1.05: [].
The original eight-bit delivery fails the same measured gate (`small-red.log`).

| Large workload | Baseline ms | Final ms | Speedup | Reference ms |
|---|---:|---:|---:|---:|
| gray-deflate native | 11.601309 | 2.719211 | 4.266× | 1.127708 |
| rgb-deflate native | 23.440097 | 6.701171 | 3.498× | 3.158369 |
| rgb-deflate crop | 5.750482 | 1.740770 | 3.303× | 0.471359 |
| noise-deflate native | 17.139588 | 2.396284 | 7.153× | 0.920666 |
| gray16-deflate-p2 native | 50.264104 | 40.216142 | 1.250× | 6.789721 |
| large-strip-deflate firstrow | 107.913756 | 24.116026 | 4.475× | 0.056301 |
| large-strip-deflate crop | 164.626912 | 37.419007 | 4.400× | 10.892112 |

Gray16 gate exit: 0. The two phase views share the same
interleaved reference samples; they are not separate timing phases.

The empty-block cases keep output at one byte: fixed 0/8/64 empty blocks build
2/18/130 trees, and dynamic 8/64 build 26/194 trees. The following differences
include all block parsing and tree building, not just `Huff::build` instructions:

| Amortized added-tree cost | Baseline µs | Original µs | Final µs |
|---|---:|---:|---:|
| Fixed | 5.067 | 10.482 | 4.856 |
| Dynamic | 6.074 | 11.222 | 5.956 |

## Attempts, reproduction and raw provenance

`small-initial-red.json.gz` preserves the initial nine-sample diagnosis. Each pair
used identical inputs, but that exploratory driver shared pixel and timing RNG
state; later inputs varied with rounds/engine counts. Those runs do not establish
controlled comparisons between candidate variants. The final driver separates
those random streams. Raw unsuccessful lazy/four-/six-bit and sparse-cache
experiments remain archived and hashed; no success is inferred from them.

The controlled bounded-cache run before the complete-path threshold passed the
full proof but failed two 1,024-byte strip cases by about 7–8%. Small C segments
route through `native::decode` to `deflate::process`; short bulk runs there caused
the remaining overhead. That run and its sources are retained separately. Adding
the complete-path threshold then passed all 15 cases in a 31-sample two-engine
check (`small-process-check.json.gz`) before the final validation and five-engine
run above. Both successful and unsuccessful measurements are retained.

Use [the standalone recipe](../../../../benchmarks/deflate-reproduce.md). It
prepares pinned inputs/binaries with finite measurement subprocess timeouts and
needs no historical private receipts. Original scripts remain historical records.

Pass-line files are selected extracts, not full raw logs. JSON may be compacted
without changing values. `raw-artifacts.json` records original/published SHA-256
hashes and transformations. Raw archives are outside the publication tree under
lane `evidence-fix/`: `exploratory.tar.gz`, `runtime.tar.gz`, `pre-process.tar.gz`
and `final.tar.gz`. Earlier successful receipts are not relabeled as final-source
runs. The first interrupted remote attempt is not counted: sync removed its
untracked logs, snapshots were restored from Git, and validation restarted.
