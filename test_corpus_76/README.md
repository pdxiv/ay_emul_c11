# test_corpus_76

76 real music files, copied from a larger real-world corpus scan (37,136
files), selected as one representative file per distinct
`identify_ay_file` output-field combination observed across that corpus
(see `tools/identify_ay_file/identify_ay_file.md`'s permutation-count
discussion). Not synthetic test fixtures - these are real, previously
untested files pulled in specifically to exercise every format/property
combination `identify_ay_file` distinguishes, including the less common
ones (Turbosound `PT3` files, both `VTX` header variants, all three
observed `YM` sub-variants, etc).

There are 78 distinct output combinations in the source corpus, but only
76 files here: two combinations are only ever produced by non-song text
files, not by any real song file in the corpus, so they are intentionally
left unfilled rather than backfilled with something artificial:
  - `/tmp/tunes/Authors/Icengreen/readme` - plain text whose prose happens
    to quote another file's `PSC` header text at exactly the offsets
    `identify_ay_file`'s Tier C fallback checks, producing a coincidental
    `format=PSC` false positive (see `migration_debt.yaml` `MIG-0024`).
  - `/tmp/tunes/Demos/ZX-STAG/readme` - plain text, representing the
    `format=unknown` combination itself, which by definition can never be
    filled by a valid song file (a real, recognizable song would not be
    `unknown`). Confirmed via `file`(1) that every corpus file sharing
    this combination is text/data, none audio.

This set was used to differentially verify `identify_ay_file` against a
real compiled `ay_emul` binary via `ay_emul/OracleHarness.pas`'s
`identify_file` scenario. Result: 70/78 exact matches on the `format=`
field (across the original 78-combination set, including the two
non-song files, since that comparison predates this cleanup); the other 8
are each explained by a specific, already-documented approximation
(LHA-wrapped `.ym` sub-variant resolution, Tier C fallback imprecision,
the missing final `IntegrityCheck` step) rather than an unexplained gap -
see `migration_debt.yaml` `MIG-0024` for the full per-file breakdown and
root-cause analysis, and `tools/identify_ay_file/identify_ay_file.md`'s
"Oracle-diff verification" section for a summary.

Three of these 76 files are dotfiles with no extension (`.pt2`, `.pt3`,
`.stp` - literal filenames, not a typo) and won't show up in a plain
`ls`; use `ls -A`.
