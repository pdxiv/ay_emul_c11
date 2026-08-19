# GUI smoke tests

The first tests in this project to exercise `gui/` code directly (previously
only `tests/engine_smoke/` existed, covering `engine/` alone) - added for
MIG-0112's playlist-level Turbosound pairing work, once real `.ayl` test
material became available.

**`test_playlist_ayl.c`** links `gui/src/playlist.c` directly (plus
`engine/libayengine.a` for the actual playback path) against two real
`test_corpus_76/*.ayl` files:

- `Cmnd.ayl` - a genuine, user-supplied playlist file (not synthesized),
  exercising a `"v1.6"` version tag and 19 Windows-style (`\`-separated)
  relative paths pointing at files outside this repo's corpus. Confirms the
  loader doesn't crash on an unfamiliar-but-compatible version string, that
  `ayl_check_path`'s backslash-to-forward-slash fallback (`Players.pas`'s
  `CheckPath`, found and ported specifically because of this file) is
  exercised without crashing, and that missing referenced files are skipped
  gracefully rather than failing the whole load.
- `ts_pair_test.ayl` - a small, hand-authored-but-grammar-verified fixture
  (real corpus file `AWAY.stc`, with two consecutive `<...>` override
  blocks - no real-world example of the "ts" Turbosound-pairing feature was
  available) confirming `has_ts_pair`/`ts_pair_overrides` parse correctly
  and that loading it through the real `gui_playlist_load_ayl` ->
  `player_pair_load_song` -> `player_pair_make_buffer` path produces
  genuinely audible, genuinely dual-chip-mixed output.

The deeper byte-for-byte proof that this same dual-chip mechanism matches
the real Pascal codebase exactly lives in `tests/oracle_diff`'s own
`stc_pair` gate (`run_diff.sh`), not here - this directory proves the real
`.ayl` file, loaded through the real GUI-layer parser, correctly drives
that already-oracle-validated mechanism end to end.

Build and run:

```sh
make
./test_playlist_ayl
```
