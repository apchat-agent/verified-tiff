# Distance-two Deflate follow-up (run 3)

The fresh complete `bash scripts/verify.sh` passed: 1,824 verified obligations,
zero errors, all 134 negative checks, exit 0, 4,162.98 seconds. The receipt binds
all 126 core source files and both unchanged trusted boundaries. `audit.log`
records a fresh source audit and exact final hash match after mutation testing.
All runtime checks in `runtime.json` passed: complete `scripts/test.sh` (73 Rust
tests plus all Python suites), ordinary C, GCC C ASan/UBSan, 20,000 smoke-fuzz
iterations and all 11 pre-existing repository Deflate fixture outputs. Fixture hashes match
`../baseline-fixture-hashes.json` exactly.

The first final gate attempt is preserved in `attempt1/`: it passed the positive
proof but stopped after 70 negative checks because scalar `batch-work` exceeded
the solver resource limit instead of reporting its expected invariant failure.
The check now pins solver seed 1 at unchanged rlimit 100. The same mutation and
required semantic diagnostic passed in preflight and in the complete restarted
gate. Neither a resource failure nor a partial gate is counted as success.

## Regression and measurements

Red commit `2e3a259` added the standalone measured regression threshold and the
independently zlib-checked period-two fixture before the production fix. The
threshold failed at 1.483165 after/before on the fresh pre-fix nine-sample pair.
The semantic test passed on the old bulk implementation, preserving that
behavior across the optimization. The additional wrong-second-byte proof
mutation is rejected for its assertion, independently and in the complete gate.

`profile/token-shape.py` independently parses each Deflate strip and validates
its bytes with Python zlib. Distance-two matches account for 1,849,824 of
2,097,152 decoded bytes (88.2%). The initial bulk path split them into roughly
925,000 tiny runs. Baseline/prefix/bulk static gprof readers show approximately
13.30/13.21/29.00 ms sampled self CPU per operation in the Deflate streaming/copy
path, while native conversion plus its reads stays around 30–31 ms. The final
profile puts only 10.95% of sampled self CPU in `Inflater::pull` + `copy_run`
(4.30 seconds over 2,019 operations), versus approximately 47.70% for initial bulk.
All four profile output hashes are `4605ee1b6a5e2825`.

Profiles use `cc -std=c11 -O3 -g -pg -no-pie -DVERIFIED_ADAPTER -Icompat/include
benchmarks/reader.c target/compat/tiff_compat.o target/compat/stubs.o
target/release/libverified_tiff_capi.a -ldl -lpthread -lm -o work/profile/reader`,
then `taskset -c 0 ./reader INPUT native 30000 memory` from the profile directory,
and `gprof -p ./reader gmon.out`. The final directory is `work/profile-fixed`.
The old three builds are in their preserved baseline/prefix/bulk snapshot roots.
Flat profiles have 10 ms sampling, no Rust call counts and no shared-libc samples.
Absolute CPU costs across the earlier and final profile phases are not controlled
speedup measurements: host load changed. Uninstrumented paired timings below
establish the final performance result.

The unchanged benchmark driver and reader harness use CPU 0, nine randomized
serial samples per engine/workload and a 100 ms minimum. For the seven workloads below, verified/reference and
before/after bytes match exactly. Driver, harness, input/output, proof, source
and binary hashes are in the raw JSON. Reference and encoder binaries are
identical across phases, using the pinned Deflate-only libtiff 4.7.1 build.
Reference medians changed by less than 4% between the final phases.

Exact final commands, through `./dxc` in `run-final-measurements.sh`:

```sh
(cd work/baseline-repo; source scripts/env.sh; python3 work/deflate-bench.py baseline)
source scripts/env.sh
python3 work/deflate-bench.py after-run3-final
python3 benchmarks/check-deflate-regression.py work/run3-final-before.json work/after-run3-final/results.json
```

`benchmark/before.json.gz` and `after.json.gz` are the final pair. All seven medians
improved; gray16 Predictor 2 changed from 23.5573 to 20.0303 ms (14.97% faster),
passing the <=1.05 threshold at 0.850281. `pre-fix-before.json.gz` and
`pre-fix-after.json.gz` retain the earlier loaded-host diagnostic pair. The separate
`../benchmark/` evidence and the lane's historical report retain all run-2 numbers.

## Mutation scope

`mutants/run-mutants.sh` applies the requested 60-minute cap and selected changed
files. Its Cargo shim adds `python3 tests/deflate.py` after successful
`cargo test --locked`, rebuilding the CLI from the tested tree first.
The actual output is:

```text
Found 0 mutants to test
 WARN No mutants found under the active filters
```

No caught/missed/unviable/timeout summary was emitted. The historical run recorded four empty result lists (zero results in each);
those housekeeping lists are omitted from publication. `mutants.json` is empty. The test command
was therefore not invoked by cargo-mutants. This provides no mutation coverage
inside `verus!` macros. The six explicit new semantic proof mutations (two prefix,
three original bulk, one period-two) are separate evidence, not cargo-mutants
caught counts. There are no missed mutants requiring further tests.

Profile arithmetic: `profile/bulk/flat.txt` reports 25.75% in `Inflater::pull`
and 21.95% in `Cursor::copy_run`; their sum is 47.70%.

For a clean checkout, use the [parameterized reproduction](../../../../benchmarks/deflate-reproduce.md).
The commands above and archived scripts are historical execution records.
