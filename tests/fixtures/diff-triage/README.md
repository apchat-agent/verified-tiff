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
- `libtiff-crash-second.tif` — a second, independent libtiff 4.7.1 crasher:
  a plain libtiff consumer (open, TIFFGetField, TIFFReadRGBAImage,
  TIFFReadEncodedStrip on the supported codec 1, TIFFReadDirectory) SEGVs in
  TIFFReadDirectoryFindFieldInfo (tif_dirread.c:5256) on the next directory;
  repro `target/fuzz-c/diff_decode tests/fixtures/diff-triage/libtiff-crash-second.tif -runs=1 -detect_leaks=0`.
  The differential campaign therefore runs in libFuzzer fork mode with
  -ignore_crashes=1 and triages every recorded artifact.
- `mismatch-diverging-striles.tif` — mismatches: a tiled directory whose
  TileByteCounts value array is stored at offset 1. libtiff recovers with
  estimated counts and partially zero offsets (tif_dirread.c:5048-5064,
  :5598-5604) while the adapter reads the declared arrays as stored
  (docs/metadata-contracts.md), so the same tiles decode different bytes.
  The harness compares the public strile accessors and skips the probes.
- `libtiff-crash-unsupported-codec.tif` — a compression tag (65281) whose
  strips crash pinned libtiff 4.7.1 itself: after failed strip reads of the
  unsupported codec, the next TIFFReadDirectory SEGVs in
  TIFFReadDirectoryFindFieldInfo (tif_dirread.c:5256; repro
  `target/fuzz-c/diff_decode tests/fixtures/diff-triage/libtiff-crash-unsupported-codec.tif -runs=1 -detect_leaks=0`,
  exit 139). libtiff is the test-only oracle; the harness counts such
  directories in skipped_unsupported.
