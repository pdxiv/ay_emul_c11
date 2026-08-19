# `.lfm` specs

Byte-identical copies of all 15 `.lfm` (Lazarus form) files from the
`ay_emul` read-only reference submodule, checked into this repo directly
so that building `gui/` (specifically `tools/lfm_gen/lfm_gen.py`'s
`--widgets-only` build-time widget-construction generator, see
`gui/Makefile` and `migration_debt.yaml`'s MIG-0132 entry) does not
require the `ay_emul` submodule to be checked out at build time.

Copied from `ay_emul` at commit `fff60fec384a9480253ac8c368eefd0b54a64df3`
(2026-08-19).

## Keeping these in sync

`ay_emul` is a frozen historical reference (the real Pascal codebase this
project is porting from) - its own `.lfm` files are not expected to
change. If they ever do (e.g. the submodule pointer is bumped to a newer
`ay_emul` commit), re-sync by hand:

```sh
cp ay_emul/*.lfm lfm_specs/
```

then re-run any build-time generator that consumes them
(`cd gui && make`) and re-verify against `tools/lfm_analyze/
lfm_analyze.py`'s own ground-truth dump for whatever changed, same as
any other porting-accuracy check in this project.

## Files

| File | Real Pascal form |
|---|---|
| `About.lfm` | `TFrmAbout` |
| `encoptsedit.lfm` | encoder options editor |
| `FindPLItem.lfm` | playlist search |
| `HeadEdit.lfm` | header editor |
| `ItemEdit.lfm` | playlist item editor |
| `JmpTime.lfm` | jump-to-time dialog |
| `MainWin.lfm` | `TFrmMain` (hand-painted skin, not used by the generator - see `PORTING_TO_C11_LINUX.md` §5.1) |
| `Mixer.lfm` | `TFrmMixer` - the Mixer window (gui/src/mixer_win.c's own generator input) |
| `mxhelper.lfm` | Mixer "Presets" helper dialog |
| `PlayList.lfm` | `TPlayList` (custom-drawn grid, not used by the generator - see `PORTING_TO_C11_LINUX.md` §5.1) |
| `ProgBox.lfm` | progress dialog |
| `seldir.lfm` | directory picker |
| `SelectCDs.lfm` | CD selection dialog |
| `SelVolCtrl.lfm` | volume-control picker |
| `Tools.lfm` | `TFrmTools` - the Tools window |
