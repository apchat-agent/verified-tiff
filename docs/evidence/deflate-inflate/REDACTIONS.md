# Publication redactions and compact evidence

Published evidence is sanitized, not verbatim raw receipts. Original raw hashes
remain in `redactions.json` and `fix/raw-artifacts.json`; original lane archives
remain outside Git. FIX2 additionally preserves every pre-packaging file in
`evidence-fix/archive-full/deflate-inflate/` outside the repository.

Large runtime/build logs are labeled outcome-line extracts, including failed
historical attempts. Proof diagnostics and final proof/source receipts remain.
Trailing whitespace and extra EOF blank lines are removed from publication text.
Large JSON files are losslessly gzip-compressed with deterministic headers;
`gzip -dc FILE.json.gz` restores their JSON values and schema.
No timing samples or source hashes have been dropped. Historical scripts refer
to uncompressed working copies and are execution records, not new run claims.

`redactions.json` lists existing published paths only. Removed housekeeping
entries are retained in the external archived original manifest. Each entry's
published hash covers current bytes, including compression or whitespace edits.
`fix/raw-artifacts.json` preserves original raw provenance and updates published
filenames and hashes. `publication-sha256.json` covers every evidence file except
itself (its hash is recorded in the external report), and includes pre-packaging
hashes where available. No manifest is a claim of fresh execution.

Historical full proof logs and larger receipts also use gzip; the final
`fix/verify.log` and `fix/verification-summary.json` remain plain text.
Other JSON is compacted without changing values. Compressed JSON may contain
compacted whitespace; its parsed values equal the pre-packaging archive.
