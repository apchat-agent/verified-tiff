# Deflate comparison from clean checkouts

Requirements: Linux x86-64, Git, Python 3 with zlib, C compiler, and the build
prerequisites listed in the repository README. Use the pinned bootstrap and
upstream helpers; no private lane receipts or historical `work/` files are needed.
Keep both trees on the same machine and stop other CPU-heavy work before timing.
All subprocesses in the new measurement drivers have finite timeouts; missing
executables, inputs, or successful proof receipts fail immediately.

From a checkout of this delivery (choose an unused sibling directory):

```sh
set -eu
git worktree add --detach ../deflate-baseline 9a64643
# Build the same reader consumer in both trees. The baseline already contains it.
for tree in ../deflate-baseline .; do
  (cd "$tree"
   python3 scripts/bootstrap.py
   python3 scripts/upstream.py
   bash scripts/verify.sh
   bash scripts/test.sh
   bash scripts/build-compat.sh
   python3 benchmarks/prepare-reader.py
   bash benchmarks/build-reader.sh)
done
python3 benchmarks/deflate-pair.py --baseline-root ../deflate-baseline \
  --candidate-root . --output target/deflate-pair
python3 benchmarks/check-deflate-regression.py target/deflate-pair/before.json \
  target/deflate-pair/after.json
python3 benchmarks/deflate-small.py \
  --reader baseline=../deflate-baseline/target/reader-benchmark/verified \
  --reader final=target/reader-benchmark/verified --output target/deflate-small
python3 benchmarks/check-deflate-small.py target/deflate-small/results.json
```

The seven-workload driver generates its own TIFFs using the baseline's pinned
libtiff encoder, checks exact baseline/candidate/reference bytes, and runs nine
randomized serial 100 ms samples per engine/workload on one allowed CPU. It
records proof, core-source, executable, adapter-library, input and output hashes.
The small-strip sweep constructs Classic TIFFs with Python zlib streams: widths
16, 64, 256, 1024 and 4096, each with one or 64 one-row strips. It checks decoded
bytes against the independent input pixels and records the first Deflate block
type (1 = fixed, 2 = dynamic). Five additional workloads insert 0/8/64 empty
fixed blocks or 8/64 empty dynamic blocks before a single final pixel. Their
explicit construction counts (2/18/130 and 26/194 trees) make setup dominate
without adding output work; Python zlib independently checks each stream.
Supply further `--reader LABEL=PATH` arguments for
intermediate snapshots. Its threshold checker reports every median above 1.05
of baseline and exits nonzero; timing is not a correctness test.

For broader exact output evidence, prepare the README's generated corpus and
compare every accepted original, all tracked Deflate TIFF fixtures, and all
26 native / 24 RGBA README workloads (including JPEG):

```sh
bash scripts/build-benchmarks.sh
python3 benchmarks/run.py --generate-only
python3 tests/corpus.py
python3 benchmarks/deflate-output-check.py --baseline-root ../deflate-baseline \
  --output target/deflate-output-check
```

Original-corpus oracle comparisons use the repository's documented JPEG error
bound; direct baseline/candidate comparisons require exact bytes even for JPEG.
Unsupported original CLI inputs are recorded separately by exit code, without
claiming a successful pixel comparison. Historical scripts under `docs/evidence`
are execution records; this document and these parameterized drivers are the
clean-checkout reproduction path.
