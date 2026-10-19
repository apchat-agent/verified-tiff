# Interpretation of the original proof-first commits

`7523784` introduced `symbol_prefix` with an empty proof body; `062817a`
introduced `batch_run` with an empty proof body. Their archived failures are
postcondition failures. Both properties already hold for the old mathematical
models: filling in the proofs alone would make the obligations pass without
implementing prefix lookup or bulk execution. They are proof-first obligations,
not executable optimization regression tests. The semantic runtime tests establish
behavior preservation, not detection of an absent performance optimization.

The distance-two red commit `2e3a259` separately introduced a measured threshold.
Its semantic test also passes before the optimization; its archived timing gate
fails before and passes after. The small-strip review follow-up likewise records
actual measurements rather than treating a proof failure as a timing regression.

The historical bulk flat profile reports 25.75% self CPU in `Inflater::pull`
and 21.95% in `Cursor::copy_run`: **47.70%** combined. The earlier 44.6% prose
figure was an arithmetic error. See `period-two/profile/bulk/flat.txt`.

The original seven-workload benchmark validates exact bytes for those seven
workloads only. The adopted 31-fixture suite contains 22 unchanged originals
and nine transcodes; its count is not an original-corpus comparison. The separate
review follow-up records fresh original-corpus and README-workload comparisons,
including exact before/after JPEG bytes. Original-corpus JPEG oracle tolerance
and direct before/after equality are distinct checks.

Published evidence with path redactions is explicitly sanitized; see
`REDACTIONS.md` and its original/published SHA-256 manifest. Historical scripts
are execution records. The parameterized clean-checkout reproduction is
[deflate-reproduce.md](../../../benchmarks/deflate-reproduce.md).
