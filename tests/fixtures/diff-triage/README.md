One fixture per one-sided outcome class of `fuzz-c/diffharness.c` (counter
units in `fuzz/README.md`). Each reproduces its class with
`target/fuzz-c/diff_decode tests/fixtures/diff-triage/<name>.tif -runs=1`.

- `ours-failed-damaged-g3.tif` — ours_failed: the adapter refuses a damaged
  Group 3 stream that libtiff partially repairs; the stricter profile fills
  no damaged rows (docs/fax.md, docs/profile.md).
- `ref-failed-lzw-legacy.tif` — ref_failed: pinned libtiff refuses the
  legacy-LZW late-change stream ("No space for LZW code table") that the
  adapter decodes (documented legacy selection, docs/profile.md).
- `both-failed-damaged-deflate.tif` — both_failed: a damaged Deflate stream
  that both engines refuse (libtiff "invalid literal/lengths set", the
  adapter "invalid TIFF input").
