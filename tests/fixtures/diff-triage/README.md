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
- `mismatch-corrupted-photometric-type.tif` — mismatches: a photometric tag
  stored with type 13 (IFD8). libtiff rejects the wrongly typed known tag and
  its RGBA path falls back to MINISBLACK without gray inversion
  (tif_getimage.c:394-411, TIFFGetField unset), while the adapter honors the
  documented policy of reading known tags independently of the file's stored
  type (docs/metadata-contracts.md) and inverts for MINISWHITE. The
  comparison domain is narrowed in the harness, not the adapter.
- `libtiff-crash-unsupported-codec.tif` — a compression tag (65281) whose
  strips crash pinned libtiff 4.7.1 itself: after failed strip reads of the
  unsupported codec, the next TIFFReadDirectory SEGVs in
  TIFFReadDirectoryFindFieldInfo (tif_dirread.c:5256; repro
  `target/fuzz-c/diff_decode tests/fixtures/diff-triage/libtiff-crash-unsupported-codec.tif -runs=1 -detect_leaks=0`,
  exit 139). libtiff is the test-only oracle; the harness counts such
  directories in skipped_unsupported.
