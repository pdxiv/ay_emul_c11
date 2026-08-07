# Phase 5 (GUI) implementation progress

Living document tracking the C11/GTK2 port of `ay_emul`'s GUI
(`MainWin.pas` + `PlayList.pas` + ~14 standard dialogs + tray/DnD/
visualizer/IPC). Updated as work lands, not written once at the end —
see `README.md` and `PORTING_TO_C11_LINUX.md` §8 item 5 for the
one-paragraph summary this document expands on.

Every row's "Debt" column links a `MIG-####` id in `migration_debt.yaml`
(open) or `migration_debt_validated.yaml` (closed) — this table is a
map of the work, the ledger is the source of truth for what's actually
verified. Per this workspace's standing invariant, nothing here may be
marked done without a corresponding validated ledger entry.

## How to read the status column

- **not started** — no C11 code exists for this yet.
- **skeleton** — `tools/lfm_gen/lfm_gen.py` output exists (widget tree +
  stub handlers), not wired to real logic.
- **in progress** — real logic partially wired.
- **done** — wired to real logic, built, and verified (see the linked
  MIG entry's `verification:` field for exactly how).

## Main window (`MainWin.pas`, 5,422 lines → `gui/src/mainwin.c` etc.)

| Feature | Status | Debt |
|---|---|---|
| Skin load/decompress/decode (`.ays`) | done | MIG-0064 |
| Window silhouette (`rgn.inc`) | done | MIG-0064 |
| Button/LED/slider hit-testing infra | done | MIG-0064 |
| Play/Pause/Stop/Open, titlebar drag, min/close | done | MIG-0065 |
| Volume slider (live) | done | MIG-0065 |
| Progress slider (real position + drag-to-seek) | done for AY/YM/VTX (the only formats with a known fixed duration); cosmetic sweep unchanged for the other 14 formats | MIG-0079 |
| Led_Stereo | done (always on) | MIG-0064 |
| Led_AY / Led_YM (real chip-type wiring) | done | MIG-0068 |
| Song title/author metadata display | done for `.ay`, YM5/YM6, GTR, FTC, PT1, PT2, PT3, PSC, ASC, ASC0, STP, PSM (12/17 formats); only STC (compressor-tag detection) remains extractable-but-unattempted; FLS/SQT/VTX/SNDH/FXM have no metadata in the original at all | MIG-0072, MIG-0075, MIG-0082, MIG-0083 |
| ButNext/ButPrev (playlist navigation — corrected from MIG-0068's subsong stand-in) | done | MIG-0071 |
| ButLoop | done | MIG-0068 |
| ButAbout | done (simplified GtkAboutDialog, not the real skinned AboutBox) | MIG-0068 |
| Seeking (progress-slider drag) | done for AY/YM/VTX, ported using the same decode-and-discard algorithm as `RerollMusic` | MIG-0079 |
| Seeking (`JmpTime` minutes:seconds dialog, 'J' key) | done for AY/YM/VTX | MIG-0080 |
| Visualizer (spectrum/amplitude, `TSensZone`) | done — full tick-accurate `FillVis`/`VisPoints` sampling ported into the synthesis loop (not a live-state approximation); SensTime's oscilloscope/time-display-mode toggle not ported (separate, not-yet-built time-digit-display feature) | MIG-0094 |
| Tray icon | done (generic icon, not the real embedded ICON00-ICON99 resources; left-click toggles minimize, tooltip shows current track) | MIG-0073 |
| Drag-and-drop file open | done (now routes through the playlist, like Open) | MIG-0070, MIG-0071 |
| CLI/single-instance IPC | done — plain file-path forwarding via a Unix domain socket, not the full `CommandLineInterpreter` flag set | MIG-0074 |
| ButList (playlist window toggle) | done | MIG-0071 |
| ButMixer (Mixer window) | done — scoped to audio-affecting controls only (channel amplitude/pan, chip-type override) | MIG-0078 |
| ButTools (sub-window launch) | done — opens the new Tools window (`gui/src/tools_win.c`), 'P' keyboard shortcut wired too | MIG-0095 |
| Keyboard shortcuts (E/G/R/X/V/C/B/Z/L, Up/Down, Left/Right, Escape) | done — 'P' (ButTools) and numpad duplicates not ported, see MIG-0081 | MIG-0081 |

## Playlist (`PlayList.pas`, 4,255 lines)

| Feature | Status | Debt |
|---|---|---|
| Playlist data model (`gui/src/playlist.c`) | done — file-probe-based add, multi-song `.ay` expansion, recursive directory scan | MIG-0071 |
| Playlist window (`gui/src/playlist_win.c`) | done — idiomatic `GtkTreeView`/`GtkListStore`, not a `PlayList.lfm` port | MIG-0071 |
| Add files / Add folder (with `ProgBox` progress + working Abort) | done | MIG-0071 |
| Double-click to play, Next/Prev, Clear, Remove Selected (button + Delete key) | done | MIG-0071, MIG-0076 |
| Per-item metadata (Author/Title/Comment) | done for `.ay`/YM/GTR/FTC/PT1/PT2/PT3/PSC/ASC/ASC0/STP/PSM — display strings use `FormatScrollString`'s real "Author - Title" convention; Tracker/Computer/Date not extracted for any format | MIG-0072, MIG-0075, MIG-0082, MIG-0083 |
| Per-item overrides (chip-type, channel amplitude/pan, AY-frequency, interrupt-frequency, Title/Author/Program/Tracker/Computer/Date/Comment) | done — full `ItemEdit.pas` (Tiers 1-3), "Adjust..." button in the playlist window, gated by 4 new Mixer "Get from list" checkboxes; channel-count override not ported (this port's output is always fixed stereo16) | MIG-0087, MIG-0088 |
| `.ayl`/`.m3u` playlist-file load/save | done — `.m3u` full fidelity; `.ayl` a real-syntax-compatible subset (metadata + chip-type/channel-mode/frequency overrides; no PLDef header block, "ts" subitems, or multi-song beyond song_index 0) | MIG-0091 |
| `.cue` playlist-file load | **out of scope, confirmed by full trace** — CUE-track playback is fundamentally a BASS streaming-position seek (`StreamPlayFrom`), already excluded by `README.md` §Scope | MIG-0093 |
| Sorting (by author/title/file name/file type, random shuffle) | done — "Sort..." button/menu in the playlist window, all 5 of `PlayList.pas`'s original sort commands | MIG-0089 |
| Deduplication, "find item" | both done — "find item" (`FindPLItem.pas`, MIG-0077); deduplication via a new "Dedup" button | MIG-0077, MIG-0092 |
| Conversion-menu commands (VTX/YM6/PSG/ZXAY/MP3/OGG/FLAC/OPUS export) | **out of scope** — conversion beyond WAV is excluded from this port entirely (see `README.md` §Scope); WAV conversion itself stays a CLI-only feature (`tools/ay_player`), not exposed from the playlist | — |

## Standard dialogs (15 total; all now done/superseded/confirmed out of scope)

| Dialog | `.lfm` lines | Status | Debt |
|---|---|---|---|
| `JmpTime.lfm` | 55 | done — hand-completed with real time-based seeking (`gui/dialogs/jmptime.c`), reached via the 'J' keyboard shortcut; kept the generator's `GtkFixed` layout | MIG-0080 |
| `ProgBox.lfm` | 51 | done — hand-completed with real progress/Abort wiring (`gui/dialogs/progbox.c`), used by the playlist's Add Folder scan; GtkVBox layout, not the generator's GtkFixed | MIG-0071 |
| `About.lfm` | 27 | done — the real skinned `AboutBox` (`gui/dialogs/about.c`), not generated from this `.lfm` (hand-built, same rationale as every other hand-built window in this port); embedded `About.bmp` sprite sheet + `rgn2.inc` window shape both ported | MIG-0090 |
| `SelectCDs.lfm` | 55 | **out of scope** — physical Audio CD playback is excluded from this port entirely (see `README.md` §Scope); this dialog and any other CD-playback UI are omitted, not deferred | — |
| `SelVolCtrl.lfm` | 70 | **out of scope** — picks a Windows system mixer control line (winmm mixer API); no ALSA/Linux equivalent, and this port already opens ALSA output directly with its own independent volume field | — |
| `FindPLItem.lfm` | 79 | superseded — hand-built idiomatic GTK2 dialog in `gui/src/playlist_win.c`, not generated from this `.lfm` | MIG-0077 |
| `mxhelper.lfm` | 141 | done — 13-preset stereo channel-mode picker, launched from the Mixer window's "Presets..." button, using real `CalcModeCoefs` arithmetic | MIG-0084 |
| `seldir.lfm` | 188 | superseded — the one applicable option (Recurse into subdirectories) added as a checkbox to the playlist's existing Add Folder dialog, not as a separate generic dialog; the other three options (auto-detect, playlist-file inclusion, path-to-name) have no backing feature in this port | MIG-0085 |
| `HeadEdit.lfm` | 418 | **out of scope, confirmed by full trace** — its only real caller, VTX/YM6 export-header customization, is format conversion beyond WAV, already excluded by `README.md` §Scope; not connected to playback, playlist metadata, or WAV export in any way | MIG-0086 |
| `encoptsedit.lfm` | 521 | **out of scope, confirmed by full trace** — `uses basscode` directly, only reachable via `BASS_Converter` (MP3/OGG/FLAC/OPUS export), already excluded by `README.md` §Scope | MIG-0086b |
| `ItemEdit.lfm` | 1,167 | done — full Tier 1-3 port (`gui/dialogs/itemedit.c`), two-tab hand-built `GtkDialog` (not generated from this `.lfm`); FileType/Offset/Length/Address/Time/Loop/FormatSpec read-only diagnostic fields not reproduced (outside the approved scope); named frequency presets (Speccy/Atari/Amstrad/Other) collapsed to a single raw-Hz entry each | MIG-0088 |
| `Tools.lfm` | 1,612 | done — the genuinely portable subset (default folder, visualizer sample period, runtime skin load/revert) hand-built in `gui/src/tools_win.c`, not generated from this `.lfm`; the large majority (BASS/proxy/network/Windows-registration/desktop-integration/TSMode-only/window-rescaling/redundant-search-tool) confirmed out of scope or superseded by full trace | MIG-0095 |
| `Mixer.lfm` | 2,938 | superseded — the audio-affecting subset (channel amplitude/pan, chip-type override) hand-built in `gui/src/mixer_win.c`, not generated from this `.lfm`; the rest (BASS/proxy/network/Atari MFP/chip-frequency/digidrum/filtering settings) out of scope or blocked on further engine plumbing | MIG-0078 |
| `MainWin.lfm` | 137 | superseded — `MainWin.pas` hand-ported directly (see above), generator not used for it (per the recorded decision) | — |
| `PlayList.lfm` | 353 | superseded — `PlayList.pas` hand-ported directly with idiomatic GTK2 widgets, not from this `.lfm` (see Playlist section above) | MIG-0071 |

## Session log

- **2026-08-06**: Kickoff milestone landed (MIG-0063–MIG-0067): skinned
  playback window, `.lfm` generator built and proven against
  `JmpTime.lfm`/`ProgBox.lfm` (skeletons only, not wired). This document
  created; full Phase 5 implementation begins now.
- **2026-08-06** (continued): `SelectCDs.lfm`/CD-playback dialogs marked
  out of scope per the project's existing CD-playback exclusion.
  MIG-0068 landed: `player_chip_type()` added to the engine layer;
  Led_AY/Led_YM now reflect real per-format chip type; ButPrev/ButNext
  wired to real subsong navigation (multi-song `.ay` files); ButLoop
  wired to a real restart-on-finish toggle; ButAbout opens a simplified
  (non-skinned) GtkAboutDialog with the real transcribed program text.
  MIG-0069 opened to track ButMixer/ButTools/ButList, still blocked on
  their destination windows. Full regression suite re-confirmed clean
  (60/60, 75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further): MIG-0071 landed a scoped port of
  `PlayList.pas` — `gui/src/playlist.c` (data model: file-probe-based
  add with multi-song `.ay` expansion, recursive directory scan) and
  `gui/src/playlist_win.c` (idiomatic GTK2 window: `GtkTreeView`/
  `GtkListStore`, `GtkVBox`/`GtkHBox` layout, standard file choosers,
  hide-on-delete) per the user's explicit GTK2-best-practices request.
  `gui/dialogs/progbox.c` (previously a MIG-0066 stub) was hand-
  completed as the real folder-scan progress dialog with a working
  Abort button. Re-reading MainWin.pas's actual ButNextClick/
  ButPrevClick during this work showed they navigate the PLAYLIST, not
  subsongs - MIG-0068's original subsong-based wiring was corrected to
  match. ButOpen and drag-and-drop now route through the playlist
  (clear + add + play first entry) instead of loading a single file
  directly. ButList now toggles the real playlist window. MIG-0069
  narrowed to just ButMixer/ButTools. Verified via standalone data-
  model tests, a standalone GTK test exercising the exact functions the
  UI's signal handlers call, and a running-app screenshot; full
  regression suite re-confirmed clean (60/60, 75/0/1/0, full smoke
  pass) after this batch.
- **2026-08-06** (continued further still): MIG-0072 landed real
  Author/Title/Comment metadata extraction for `.ay` files
  (`engine/src/ay_file.c`'s new `read_relative_pstring()`, porting
  Players.pas's OpenAYFile relative-pointer string walk - previously
  explicitly out of scope, now revised), promoted to `player.h` as
  `player_get_metadata_raw()`. Raw CP1251 bytes are converted to UTF-8
  at the GUI layer via `g_convert` (hardcoded to CP1251, not the
  original's configurable codepage - a documented simplification). The
  main window's title bar now shows "Author - Title" when known, and
  the playlist's display strings use `FormatScrollString`'s real
  convention with real per-subsong titles (confirmed against
  `MetalMania.ay`: "Ozzyoss - Church", "Ozzyoss - Select", "Ozzyoss -
  Purple" for songs 1-3). Every non-`.ay` format still extracts no
  metadata. One non-reproduced SIGSEGV was investigated (AddressSanitizer
  + 30 repeat runs, no corruption found) and documented as a probable
  environmental fluke rather than silently dismissed - see MIG-0072's
  verification for the full investigation. Full regression suite
  re-confirmed clean (60/60, 75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0073 landed a system
  tray icon via `GtkStatusIcon` - left-click toggles minimize/restore
  (matching `TrayIcon1MouseUp`'s real logic), tooltip shows the current
  track (same string as the title bar). Uses a generic icon-theme name,
  not the real embedded `ICON00`-`ICON99` Windows resources (not
  ported - documented simplification). No tray host was running in
  this sandbox to visually confirm on-screen appearance, so verified
  via 15 repeat runs (0 crashes, 0 GTK warnings on stderr) plus a
  main-window screenshot showing no regression. Full regression suite
  re-confirmed clean (60/60, 75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0074 landed single-
  instance CLI/IPC (`gui/src/ipc.c`) via a Unix domain socket at
  `$XDG_RUNTIME_DIR/ay_emul_c11.sock` - a second launch with a file
  argument hands it to the already-running instance (which loads it,
  raises its window) and exits immediately rather than opening a
  second window. Only a single plain file path is forwarded, not the
  original's full `CommandLineInterpreter` flag set (sample
  rate/bit-depth/clock overrides etc. - no settings/config surface
  exists in this port yet). Verified end-to-end against the real built
  binary: second-instance exit confirmed immediate, first instance's
  window title changed to reflect the forwarded file, screenshot
  confirms correct rendering, 10/10 repeat fresh-instance runs clean.
  Full regression suite re-confirmed clean (60/60, 75/0/1/0, full
  smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0075 extended real
  metadata extraction to YM5/YM6 files - `engine/src/ym_file.c` already
  scanned past three NUL-terminated Title/Author/Comment strings to
  find the register-data offset, discarding their bytes; changed it to
  also capture them (no other change to the scan itself, so this can't
  affect which files load). Confirmed against real files
  (`Block_Us.ym` → "Ultrasyd"/"Block us", `Batman_Journey.ym` →
  "Kangaroo"/"Batman's Journey", embedded apostrophe handled
  correctly). Zero `gui/` code changes were needed - `playback.c`/
  `playlist.c` already call the generic `player_get_metadata_raw()`
  dispatch, so YM titles now show up in the title bar and playlist
  automatically. Full regression suite re-confirmed clean (60/60,
  75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0076 added playlist
  item removal (`gui_playlist_remove()` + a "Remove" button and
  Delete/BackSpace key handling in the playlist window), the one core
  CRUD operation missing since MIG-0071. Standalone test confirmed
  correct `current`-index adjustment for all three cases (removed
  entry before/at/after the current index). Full regression suite
  re-confirmed clean (60/60, 75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0077 ported
  `FindPLItem.pas` — a "Find" button on the playlist window opens a
  dialog (search text + Anywhere/Author/Title/Filename radio buttons +
  Find Next/Find All/Close), hand-built with idiomatic GTK2 widgets.
  Find Next wraps around the list matching the original's exact
  two-pass logic; Find All counts and selects matches (only the last
  stays visually highlighted, a documented narrowing of GTK's
  single-selection-mode vs. the original's true multi-highlight). Caught
  and fixed one real bug during this work: `gtk_container_set_border_
  width()` was mistakenly called on a `GtkEntry` (not a container),
  reproduced and confirmed fixed via a standalone test. Full regression
  suite re-confirmed clean (60/60, 75/0/1/0, full smoke pass) after
  this batch.
- **2026-08-06** (continued further still): investigated the two
  largest remaining items. The visualizer (`TSensZone`/`DoVisualisation`)
  turns out to be driven by BASS's own FFT (`BASS_ChannelGetData` with
  `BASS_DATA_FFT*` flags) compositing pre-baked sprite bitmaps from the
  skin - BASS is already out of scope entirely (README.md §Scope), and
  a from-scratch equivalent would need a real DFT/FFT plus reverse-
  engineered skin sprite geometry; deferred, not attempted partially.
  Ran `tools/lfm_gen/lfm_gen.py` against all 8 still-unstarted dialogs
  (`SelVolCtrl`/`mxhelper`/`seldir`/`HeadEdit`/`encoptsedit`/`ItemEdit`/
  `Tools`/`Mixer`) as a scaling check (not checked into the repo - see
  below): all 8 generated and compiled cleanly, including `TPageControl`/
  `TTabSheet` nesting, and every unmapped widget type (`TGroupBox`/
  `TRadioButton`/`TTrackBar`/etc. - the initial generator only mapped
  `TLabel`/`TEdit`/`TButton`/`TProgressBar`) was visibly flagged as
  "TODO: unmapped type", never silently guessed - confirms the
  generator scales to the harder cases, not just the 2 proof-of-concept
  dialogs. Not checked in: `Mixer.lfm`'s skeleton alone is 1,592 lines,
  over the 600-line convention as an unwired starting point (the
  convention's generated-data exemption is for genuinely-final
  generated artifacts like `default_skin.c`, not an unfinished dialog
  someone still needs to hand-complete), and `Mixer.pas`/`Tools.pas`
  are overwhelmingly BASS/proxy/network/Windows-registration/desktop-
  integration settings already out of this port's scope - a future
  milestone should hand-select which specific controls (e.g. Mixer's
  AY-relevant per-channel amplitude/chip-type/frequency tab) are worth
  porting, not mechanically complete the whole megadialog.
- **2026-08-06** (continued further still): asked the user which large
  remaining area to prioritize; chose "Mixer's AY-relevant tab only".
  MIG-0078 landed it - investigation found the engine already fully
  supports live per-channel pan/level (`index_al`/`ar`/`bl`/`br`/`cl`/
  `cr`) and chip-type changes via the existing
  `ay_engine_calculate_level_tables()`; only a new generic accessor
  (`player_ay_engine()`) was needed to reach it from `gui/`. Built
  `gui/src/mixer_win.c` (6 `GtkVScale` channel sliders + AY/YM radio
  buttons, idiomatic GTK2 widgets, not generated from `Mixer.lfm`).
  Verified with an engine-level differential test (muting a channel or
  overriding chip type measurably changes the PCM output) and a GUI-
  level live test firing real "value-changed"/"toggled" signals,
  confirming the full slider/radio-to-engine-mutation path. Caught and
  fixed a real `-Wpedantic` issue (GLib's `g_signal_handlers_block_by_
  func` casts a function pointer to `gpointer`, non-conforming strict
  ISO C11) by using a plain bool re-entrancy guard instead. ButMixer
  now toggles the real window; MIG-0069 narrowed to just ButTools. Full
  regression suite re-confirmed clean (60/60, 75/0/1/0, full smoke
  pass) after this batch.
- **2026-08-06** (continued further still): user asked to investigate
  how the original Pascal codebase performs seeking and replicate that
  exact approach. Found Players.pas's RerollMusic (14099-14283):
  seeking = decode-and-discard forward tick-by-tick until
  `Global_Tick_Counter` reaches the target, or reset-and-redecode-from-
  zero if seeking backward - MIG-0079 ports this algorithm exactly (the
  original's register-only fast-decode optimization during the discard
  loop isn't replicated, since `player_make_buffer` has no such mode -
  a documented CPU-optimization gap, not a correctness one). Turned out
  only AY/YM/VTX structs actually carry a `global_tick_max` field in
  this engine; the other tick-tracked formats (PT1/PT2/PT3/GTR/FLS/
  STC/STP/FXM) each have a real declared song length but computing it
  was already flagged as skipped in an earlier phase (`pt3_file.h`'s
  pre-existing `GetTimePT3`/"UI-only" note); PSM/ASC/ASC0/FTC/PSC/SQT
  loop indefinitely with no fixed length at all. The progress slider is
  now draggable (live-scrub, same UX as the volume slider) for AY/YM/
  VTX, showing real position instead of the old cosmetic sweep. Caught
  and fixed a real concurrency bug during testing: a second seek
  requested while the first was still executing (a real scenario - a
  full-song seek takes over a second of wall-clock decode time) was
  being silently dropped by an unconditional flag-clear after the first
  finished; fixed with `atomic_exchange`. `JmpTime`'s minutes:seconds
  dialog is NOT wired yet (needs a per-format ticks-to-seconds
  conversion this port doesn't have uniformly) - tracked as a separate,
  explicit follow-up. Full regression suite re-confirmed clean (60/60,
  75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0080 landed that
  ticks-to-seconds follow-up and wired `JmpTime` for real. Added
  `player_get_seconds_per_tick()` (MaxTStates/FrqZ80 for AY, 1000/
  Interrupt_Freq for YM/VTX). Caught and fixed a real bug while
  verifying it: `vtx_file`/`ym_file`'s `interrupt_freq` is pre-scaled
  by 1000 (`InterFrq*1000`, matching the original), and the first
  version of the formula used `1/Interrupt_Freq` instead of `1000/
  Interrupt_Freq` — durations for YM/VTX came out ~1000x too short
  (a real 50Hz tune measuring as 50000Hz) until corrected; AY was
  unaffected (different formula, no such scaling). Also added a stored
  `interrupt_freq` field to `vtx_file` (it was already computed
  locally, just never saved). `gui/dialogs/jmptime.c` (a MIG-0066
  skeleton, hand-completed, keeping its `GtkFixed` layout) now shows
  real "Track length: M:SS", prefills the entry with the current
  position, and seeks on Jump; wired to the real 'J' keyboard shortcut.
  Verified against real files (correct 2m3s/3m25s/3s durations after
  the fix) and a real dialog screenshot. Full regression suite
  re-confirmed clean (60/60, 75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0081 ported the rest of
  `MainWin.pas`'s keyboard shortcuts (E/G/R/X/V/C/B/Z/L toggle/trigger
  the same buttons; Up/Down nudge volume by 1 slider-pixel; Left/Right
  seek ±5s; Escape minimizes) - every action dispatches to a primitive
  already independently verified in an earlier entry, so this was
  purely new input-event wiring, not new engine/playback logic. 'P'
  (ButTools, no destination window yet) and the redundant numpad key
  duplicates were not ported. Verified the one genuinely new piece of
  arithmetic (relative ±5s seek) via a standalone test; 10/10 repeat
  full-app runs clean. Full regression suite re-confirmed clean (60/60,
  75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0082 extended metadata
  extraction to GTR, FTC, PT1, PT2, PT3, and PSC (fixed-offset/fixed-
  width space-padded fields, the same "easy tier" as AY/YM - unlike STC,
  which needs legacy compressor-tag detection and was deliberately not
  attempted). FLS/SQT confirmed to have no title extraction anywhere in
  the original at all. Verified against real corpus files (readable
  titles/authors for RE-TRIGG.ftc, mus08.pt1, DEMON.pt1, NOR.MUS..pt2,
  ZAGON_07_remixDJ_EchoMAKROSS.pt3, WannaGS.psc; correct empty results
  for files genuinely lacking metadata). Caught and fixed a real bug:
  the shared `copy_fixed_field()` helper only trimmed trailing padding,
  leaving leading spaces in titles like "    IR0NMAN..."; fixed to trim
  both ends. Zero `gui/` changes needed (same generic-dispatch payoff
  as MIG-0075). Full regression suite re-confirmed clean (60/60,
  75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0083 extended metadata
  to ASC, ASC0, STP, and PSM - each needs one conditional check before
  reading (a magic-constant/signature/sentinel check), one tier past
  MIG-0082's purely fixed-offset formats. Notably, ASC0's title/author
  turned out to need NO special-casing at all: this port's existing
  ASC0->ASC1 normalization (done for unrelated reasons, to reuse ASC1's
  decode path) already produces the exact same check/offset values
  ASC1's own branch uses, confirmed by both arithmetic and a real
  `MISTERS_BOX.as0` file extracting "F.F.F." / "MISTER'S BOX" correctly.
  This completes the format survey from Players.pas's title-extraction
  section - only STC (computed-offset compressor-tag detection) remains
  unattempted; FLS/SQT confirmed to have no metadata logic in the
  original at all. Metadata coverage is now 12 of 17 formats. Full
  regression suite re-confirmed clean (60/60, 75/0/1/0, full smoke
  pass) after this batch.
- **2026-08-06** (continued further still): investigated STC's own
  title-detection more closely before deciding whether it was tractable
  after all. Its "reject known dummy titles" step (`IsMatch(FT.STC, 7,
  ...)`) turns out to depend on `formatParams[FT.STC].matches`, a table
  populated by a whole separate `load_formats` config/detection
  subsystem (`filetypes.pas`) - not a fixed signature constant like
  STP's `KsaId`. That's a materially different, open-ended dependency
  (an entire format-detection database, not a bounded string constant),
  so STC metadata is confirmed to stay deferred rather than attempting
  a partial or guessed port of just the compressor-tag half. No code
  changed this pass.
- **2026-08-06** (continued further still): asked the user which of the
  remaining large-scope items to prioritize; chose a small standalone
  dialog instead. MIG-0084 ported `mxhelper.pas` - a 13-preset stereo
  channel-mode picker (AY/YM x ABC/ACB/BAC/BCA/CAB/CBA + Mono), using
  `CalcModeCoefs`'s real arithmetic and plugging directly into the
  already-built Mixer primitives (`player_ay_engine()` + `ay_engine_
  calculate_level_tables()`, same as MIG-0078's own sliders). A new
  "Presets..." button on the Mixer window opens it. Verified via a
  standalone unit test reaching the dialog's static preset-computation
  functions directly (confirming exact-match table values for two
  presets plus the trickiest item-index-to-preset mapping cases) and a
  real dialog screenshot. Full regression suite re-confirmed clean
  (60/60, 75/0/1/0, full smoke pass) after this batch.
- **2026-08-06** (continued further still): MIG-0085 landed the second
  small dialog - `seldir.pas`'s one applicable option (Recurse into
  subdirectories) added as a checkbox on the playlist's existing Add
  Folder dialog, rather than porting the whole generic 4-option
  `ChooseDirectory` dialog (the other three options have no backing
  feature in this port). `gui_playlist_add_directory()` gained a
  `recurse` parameter (previously always recursed unconditionally).
  Verified with a real nested test directory: non-recursive added only
  the root-level file, recursive added all three files at every depth.
  Full regression suite re-confirmed clean (60/60, 75/0/1/0, full smoke
  pass) after this batch.
- **2026-08-06** (continued further still): user explicitly asked NOT
  to defer `HeadEdit.pas` on apparent complexity alone, and to trace
  its real end-to-end Pascal usage first. MIG-0086 records that trace:
  `THeaderEditor` is `uses`d by exactly one unit in the entire codebase
  (Convs.pas), feeding exactly two call sites (VTX_Header_Editor/
  YM6_Header_Editor), both reached only from VTX_Converter/YM6_Converter
  - i.e. HeadEdit exists solely to customize a NEW VTX/YM6 file's header
  before writing it during format conversion, reached only via the
  playlist's "Convert to VTX"/"Convert to YM6" commands. It has zero
  connection to playback engine state, persistent playlist metadata, or
  WAV export. Format conversion beyond WAV has been out of this port's
  scope since `README.md`'s original Scope section (predating Phase 5
  entirely) and was independently already excluded when the playlist's
  conversion-menu commands were scoped out in MIG-0071 - so this isn't
  a new complexity-based deferral, it's the same pre-existing scope
  boundary confirmed by evidence rather than assumed. No code changed;
  the dialog's actual field list (which suggested per-item settings
  editing at first glance) turned out not to represent what the dialog
  is really for.
- **2026-08-06** (continued further still): applying the same full-
  trace-first methodology to `encoptsedit.pas` before moving on -
  MIG-0086b confirms it's BASS-encoder-only (`uses basscode`, reachable
  only via `BASS_Converter`), the same out-of-scope disposition as
  HeadEdit, so the original scope assumption holds. Then traced
  `ItemEdit.pas` end-to-end per the user's request; unlike HeadEdit/
  encoptsedit this one turned out to be real and substantial (edits
  `PlayListItems[LastSelected]^`'s persistent fields, applied at
  playback time by `PlayItem`, each gated behind its own Mixer "Get
  from list" checkbox) - asked the user to confirm scope via a Tier
  1/2/3 breakdown; answer was "Tiers 1-3, full ItemEdit" (the complete
  feature, not a subset).

  Landed the complete feature across MIG-0087 (engine plumbing) and
  MIG-0088 (GUI):
   - MIG-0087: `player_set_chip_freq()`/`player_set_player_freq()`
     (`engine/src/player.c`), generalizing `MainWin.pas`'s `Set_Chip_
     Frq`/`Set_Player_Frq` across every format with the relevant
     concept (AY/YM/VTX for chip frequency; YM/VTX only for interrupt
     frequency - AY has no such concept, `MaxTStates`/`FrqZ80` govern
     its duration instead). `vtx_file.h`/`.c` gained a persistent
     `ay_freq` field (mirroring `ym_file.h`'s existing one) since VTX
     previously only computed this transiently at load time. Verified
     via a standalone differential test confirming real audible PCM
     changes on AY/YM/VTX (chip freq) and YM/VTX (player freq), and the
     correct AY no-op for player-freq.
   - MIG-0088: `gui_playlist_overrides` (new struct in `gui/include/
     gui/playlist.h`, embedded in every playlist entry) holds all
     seven override categories with the same sentinel conventions the
     original uses. `gui_playlist_entry_refresh_display()` recomputes
     the display string giving Title/Author overrides precedence when
     set - `gui_playlist_add_file`'s own inline display logic was
     refactored to call this same function (verified byte-identical
     output on a real 12-song `MetalMania.ay` before/after). Four new
     "Get from list" checkboxes added to the Mixer window, default
     checked. `gui/src/mainwin.c`'s new `apply_item_overrides()`
     (called from `do_load_song` after every successful load) applies
     chip-type/channel-mode overrides via the same `player_ay_engine()`
     + `ay_engine_calculate_level_tables()` primitives Mixer's own
     sliders use (channel-mode presets reuse `mxhelper.c`'s
     `CalcModeCoefs` arithmetic, now exposed as
     `mxhelper_calc_mode_coefs()` rather than duplicated), and
     frequency overrides via MIG-0087's new functions; overrides
     persist across a same-song `ButLoop` restart. `gui_playlist_win`'s
     `on_play` callback gained an overrides parameter (one call site,
     mechanical change) so `do_load_song` always receives the selected
     entry's overrides. New `gui/dialogs/itemedit.c` (`gui_itemedit_
     show`) is a two-tab `GtkDialog` covering all three tiers, wired
     into the playlist window via a new "Adjust..." button.

  Verified: both `engine` and `gui` build clean (`-std=c11 -Wall
  -Wextra -Wpedantic`, zero new warnings). Full regression suite
  unchanged and green (60/60, 75/0/1-known/0, full smoke pass).
  Standalone tests confirmed the refactored display-string path is
  byte-identical to the pre-refactor logic, and `mxhelper_calc_mode_
  coefs()` matches the exact expected arithmetic. 10/10 repeat full-app
  runs clean under `timeout` with a real X display, no crashes or new
  stderr warnings. Not independently screenshot-verified this session
  (a root-window capture attempt was aborted on realizing it would
  expose the live desktop rather than an isolated app window, not
  repeated) - the dialog's visual layout should get a real screenshot
  check in a future session, though the underlying data flow (load →
  edit → apply → persist → redisplay) is verified end-to-end via the
  checks above.
- **2026-08-06** (continued further still): MIG-0089 landed playlist
  sorting - `gui_playlist_sort()` covering all five of `PlayList.pas`'s
  original commands (by author/title/file name/file type, plus a
  random shuffle), wired into the playlist window via a new "Sort..."
  button/menu. Added `player_format format` to each playlist entry
  (captured from the real `player_load_song` probe at add-time) so
  "by file type" has real data to sort on, and a per-entry `uint64_t
  id` so the currently-playing entry stays correctly tracked through a
  reorder - the explicit C11 equivalent of the original array holding
  *pointers* (which stay valid identities across a swap-based sort with
  no extra bookkeeping needed there). Verified with a standalone driver
  building a synthetic 3-entry playlist and confirming correct
  ordering plus correct `current`-tracking for the author/filename/
  file-type modes. Full regression suite re-confirmed clean (60/60,
  75/0/1-known/0, full smoke pass); 10/10 repeat full-app runs clean
  under `timeout`.
- **2026-08-06** (continued further still): user asked for the real
  skinned About dialog, `.ayl`/`.cue`/`.m3u` playlist load/save, and
  deduplication - all three landed.

  MIG-0090: the real `AboutBox` (About.pas), replacing MIG-0068's
  plain `GtkAboutDialog` placeholder. `About.bmp` (the source image the
  original's LZH-compressed "ABOUTSCREEN" resource is built from) is
  already an uncompressed BMP, so it's embedded directly as a byte
  array (`gui/src/about_bitmap.c`, same technique as `gui/src/
  default_skin.c`) with no LZH-decode step needed - only BMP decode via
  `GdkPixbufLoader`. The window's own irregular shape (`rgn2.inc`, 300
  spans) was transcribed into `gui/src/regions.c` alongside the
  existing main-window `rgn.inc` table (that file's own comment had
  already flagged this as future work). OK button, logo/"Help" zone,
  window-drag, Escape-to-close, and the centered "3.0" version text are
  all wired. Verified via a standalone bitmap-bounds check plus a real
  single-window screenshot (via `xwininfo` + `import -window <id>` -
  explicitly NOT a root-window capture, to avoid the earlier session's
  privacy incident) confirming correct rendering.

  MIG-0091: `.ayl` (a real-syntax-compatible SUBSET - metadata +
  chip-type/channel-mode/frequency overrides, matching what
  `gui_playlist_overrides` already models, in the real token names/
  version-string/`<`...`>` block syntax) and `.m3u` (full fidelity - the
  real writer is just one bare file path per line, no header at all)
  load/save, wired into a new "Save..." button plus extension-based
  dispatch in the existing Add-Files/drag-drop paths. Traced SaveAYL/
  LoadAYL in full before scoping the subset (a byte-for-byte port would
  have been substantially larger than the rest of this session
  combined - a PLDef global-defaults header block, "ts" Next-linked
  subitem chains of undetermined purpose, and per-format Offset/Length/
  Address/FormatSpec fields this port's data model doesn't carry).
  Verified via a standalone round-trip driver: saved a real 12-subsong
  file plus a full set of overrides (including a multi-line comment,
  confirming the escape/unescape round-trips correctly), reloaded both
  formats, confirmed every override value came back correctly and the
  documented multi-song-collapses-to-song-0 (.ayl) / one-line-per-item
  including-subsong-repeats (.m3u, inherited from the real writer's own
  behavior) quirks both matched their documented shape exactly.

  MIG-0092: playlist deduplication (`Deduplicate1Click`) - a new
  "Dedup" button removes entries sharing a (path, song_index) key
  (this port's exact equivalent of the original's (FileName,
  FormatSpec, Offset) triple, since Offset is always 0 for every entry
  this port can produce).

  MIG-0093: `.cue` load traced end-to-end (per the user's explicit
  mention, so not assumed out of scope without evidence) and confirmed
  genuinely blocked, not deferred on complexity - CUE-track playback
  is fundamentally a BASS streaming-decoder position-seek
  (`StreamPlayFrom`), a dependency category already excluded from this
  port's entire scope since before Phase 5 began.

  Full regression suite re-confirmed clean throughout (60/60,
  75/0/1-known/0, full smoke pass); 10/10 repeat full-app runs clean
  under `timeout` after each of the three landed items.
- **2026-08-06** (continued further still): user asked for the
  visualizer, explicitly requesting a from-scratch FFT/spectrum
  implementation. Traced `RedrawVisSpectrum`/`AYVisualisation` first,
  per the standing full-trace-first methodology, and found the
  original doesn't actually use a real signal-domain FFT for AY-chip
  playback at all - it buckets each channel's own tone-period register
  into a log-scale bar and uses the amplitude register as the bar
  height. Checked in with the user before proceeding given this
  materially changed the scope; the user chose the faithful port (no
  DSP code needed) over building a real FFT as a deliberate deviation.
  A second scope question followed once the sampling mechanism itself
  was traced (a tick-driven historical ring buffer hooked into the
  synthesis loop, not a live-register read) - the user chose full
  fidelity, matching the original exactly, over a simpler live-state
  approximation.

  MIG-0094 landed the complete feature: engine-side `ay_vis_point`/
  `ay_engine_init_vis`/`ay_engine_get_vis_point` (`engine/include/
  ay_engine/ay.h`, `engine/src/ay.c`) with a real `FillVis`-equivalent
  hook inserted into `ay_synthesizer_stereo16`'s inner sample-writing
  loop (gated to a no-op unless a caller explicitly enables vis
  sampling, so every existing test stays byte-for-byte unaffected);
  GUI-side `gui_visualizer` (`gui/include/gui/visualizer.h`, `gui/src/
  visualizer.c`) reproducing the bucketing, the bar-vs-decaying-peak-
  marker distinction, and the click-to-toggle zones, driven by a new
  separate 30ms timer.

  A real regression was caught and fixed during this entry's own
  verification, before being reported as done: editing `ay.h` (a
  struct embedded in all 18 format structs) without a full `make
  clean` left three format `.o` files stale, compiled against the old
  struct layout while `ay.o` itself used the new one - an ABI mismatch
  that corrupted PCM output for exactly those three formats. Bisection
  (reverting the loop hook alone didn't fix it; a full clean rebuild
  did, with zero code changes) proved the actual port logic was
  correct all along and pinpointed the real cause: `engine/Makefile`'s
  generic `%.o: %.c` rule has no header-dependency tracking.

  Verified: full regression suite green after the fix (60/60,
  75/0/1-known/0, full smoke pass); a standalone engine-level driver
  confirmed real, varying tone/amplitude data flows through the whole
  pipeline during actual playback of a real corpus file; a second
  driver confirmed the GUI-side bucketing/peak-decay/click-toggle
  logic directly; a real single-window screenshot of the running app
  playing a real song shows the amplitude bars visibly rendering and
  changing between captures. 10/10 repeat full-app runs clean under
  `timeout`.
- **2026-08-06** (continued further still): user asked for a Makefile
  for the GUI application - one already existed (`gui/Makefile`, used
  throughout this session) but shared the same real defect just found
  in `engine/Makefile`: no header-dependency tracking, the exact class
  of bug that caused MIG-0094's own stale-build regression. Added
  `-MMD -MP` to both Makefiles' generic `%.o: %.c` rules plus an
  `-include $(OBJ:.o=.d)` line, verified by touching a shared header
  and confirming every affected `.c` file (across both `engine/` and
  `gui/`) now correctly rebuilds without a manual `make clean`.

  Then continued Phase 5 per the user's follow-up request: `Tools.pas`
  and "a handful of minor standard dialogs". MIG-0095 landed the
  genuinely portable subset of Tools.pas (default folder, visualizer
  sample period, runtime skin load/revert - the latter finally wiring
  up `gui_skin_load_file`, implemented since MIG-0064 but never
  actually called until now) via a new `gui/src/tools_win.c`, plus the
  previously-never-added `ButTools` button itself and its 'P'
  shortcut. Checking the full standard-dialogs table afterward showed
  this was in fact the LAST of all 15 `.lfm` dialogs without a
  resolution - every other one already had a done/superseded/out-of-
  scope status from earlier session work, so no separate "minor
  dialogs" work remained to do.

  A real correctness bug (an undefined-behavior signal-callback
  signature mismatch on "focus-out-event") was caught and fixed during
  this entry's own review, before being reported as done. Full
  regression suite re-confirmed clean (60/60, 75/0/1-known/0, full
  smoke pass - engine untouched by this entry); 10/10 repeat full-app
  runs clean under `timeout`; a real single-window screenshot of the
  running Tools window confirms correct rendering, including the real
  loaded skin's actual author/comment text.
- **2026-08-06** (continued further still): user pointed out a large
  batch of build warnings from `make`. All traced to sources outside
  this project's own code, fixed without touching vendored/generated
  files: (1) `third_party/musashi/`'s own header (`m68kcpu.h`) and its
  `m68kmake`-generated `m68kops.c` produce a handful of genuinely-
  unused-variable warnings from the third-party source/generator
  itself - `engine/Makefile` now builds just those 4 files without
  `-Wall -Wextra` via more-specific pattern rules, leaving this
  project's own 27 source files under full `-Wall -Wextra` unchanged;
  (2) GTK2/glib's own system headers reference their own deprecated
  APIs internally (`GTypeDebugFlags`, `GTimeVal`) - every GUI file
  transitively includes `<gtk/gtk.h>` and hit this, so
  `-Wno-deprecated-declarations` was added to `gui/Makefile`'s CFLAGS;
  (3) a real (if harmless) bug: `gui/src/playlist.c` and `gui/src/
  playlist_win.c` defined `_POSIX_C_SOURCE` AFTER their own first
  `#include`, which transitively locks in glibc's own lower default
  first - moved the define to precede every include, matching the
  already-correct pattern in `gui/src/skin.c`/`playback.c`. Verified:
  both `engine/` and `gui/` now build completely clean from scratch -
  zero warnings, zero errors - full regression suite still green,
  10/10 stability runs.

  Then continued with the "minor polish, not new features" work
  mentioned in the previous status summary: closed two outstanding
  "not independently screenshot-verified" debt items from earlier in
  Phase 5. MIG-0088's `ItemEdit` dialog now has a real screenshot
  (loaded against a real corpus file, confirming actual extracted
  Title/Author pre-fill and correct layout) - debt item resolved.
  MIG-0089's Sort menu screenshot was attempted but left honestly
  unresolved: unlike the modal dialogs elsewhere in this port (which
  block and can be timer-captured), a `GtkMenu` popup is asynchronous
  and needs a real click to appear, and this sandbox has no `xdotool`
  for a synthetic one, nor does the Sort button expose an external
  handle to trigger it in-process - a real but low-risk gap (a plain
  unstyled 5-item text menu, unlike the custom-drawn skinned dialogs
  this technique has actually caught real issues in), documented as
  such rather than worked around with test-only scaffolding in
  production code. No other outstanding debt items in `migration_debt.
  yaml` were closable without either reversing an already-approved
  scope decision or taking on a separate, larger project (window
  rescaling, Windows-registry integration, etc. - all already
  correctly categorized as out of scope, not gaps).
- **2026-08-06** (continued further still): user reported two real
  bugs from actually using the app - the main window couldn't be
  dragged at all, and the visualizer bars looked "very thin". MIG-0096
  traced both against `MainWin.pas` and fixed them:
   - The window-drag bug was a real regression, not a fidelity gap:
     `gdk_window_begin_move_drag` was being called on the drawing
     area's own child `GdkWindow` instead of the actual toplevel
     window, so the window-manager move request silently no-op'd. The
     original's own drag zone (a specific title-strip region, not
     "anywhere" - confirmed by re-reading `FormMouseDown`/
     `WndCallback`) was already correctly ported; only the target
     window was wrong. Verified with a REAL synthetic X11 drag
     (`libXtst`) - the window measurably moved. The exact same bug
     existed in the About dialog's own background-drag fallback,
     fixed the same way.
   - The "thin bars" turned out to be an antialiasing mismatch: GDI's
     `MoveTo`/`LineTo` draws hard, non-antialiased 1px lines; Cairo's
     default stroke rendering softens a nominally-identical 1px line.
     Disabling antialiasing for the visualizer's bar/marker drawing
     reproduced GDI's crisp look - verified with a real cropped/
     enlarged screenshot during actual playback.

  A follow-up sweep (explicitly requested) re-read `FormMouseMove`/
  `FormMouseUp`/`FormMouseWheelDown`/`Up` in full rather than trusting
  the already-ported subset was complete, and found two more real,
  previously-unnoticed gaps in the same area:
   - Press-and-drag-off-to-cancel: every button in this port fired its
     action unconditionally on release regardless of where the cursor
     ended up, and never visually un-pushed while dragging off a held
     button - unlike the original, which continuously tracks this via
     `FormMouseMove` and only fires on release if still over the zone.
     Fixed with a `pressed_button` tracking field reusing the existing
     `gui_button_hit_test` continuously during motion.
   - Mouse wheel volume control was entirely unimplemented - the
     original adjusts volume on any wheel event anywhere on the
     window. Added, reusing the same logic the Up/Down keyboard
     shortcuts already had (now shared, not duplicated).

  Full regression suite re-confirmed clean throughout (60/60,
  75/0/1-known/0, full smoke pass - engine untouched); 10/10 repeat
  full-app runs clean under `timeout` after each fix.
- **2026-08-06** (continued further still): user reported three more
  issues after actually using the app further. MIG-0097 traced and
  fixed all three:
   - Play/Pause buttons "don't stay depressed sometimes": traced
     `PlayCurrent`/`ButPauseClick`/`RestoreControls` and found these
     two buttons are NOT purely momentary in the original - `ButPlay.
     Switch_On` keeps Play visibly pushed for the entire play session
     (only released on an actual Stop), and `ButPause` separately
     tracks the paused sub-state the same way. This port treated both
     as momentary (briefly pushed during the click only), never
     reflecting ongoing state. Fixed by deriving `is_on` for both
     fresh from live playback state every timer tick, rather than
     trying to hook every individual call site (the same "miss a
     call site" bug class already fixed once this session).
   - Visualizer bars "still only about 1 pixel wide" where the
     original shows ~3px: a closer re-read of `FormCreate` (not just
     the drawing functions themselves) found `BMP_Vis.Canvas.Pen.
     Width := 3; Pen.Color := $464646;` set once at startup, missed
     entirely by both the original MIG-0094 port and the MIG-0096
     antialiasing fix. Fixed the width (1 to 3) and color (black to
     the real dark gray) together.
   - No song title anywhere in the actual window: this port's window
     is undecorated (required for the custom skin shape), so the OS
     window title this port already sets correctly has zero visible
     effect - the original's real display lives INSIDE the skin, in a
     dedicated scroll-text area with its own smooth ticker-scroll
     animation, never ported at all. Added a static (non-scrolling)
     version showing the real "Author - Title" text in the correct
     area, font, and color - a documented simplification (no scroll
     animation for titles wider than the display area) rather than
     porting the full animation state machine.

  Verified with a single real screenshot during actual playback
  showing all three fixes simultaneously: song title displayed, Play
  button visibly pushed while playing (distinctly different from its
  neighbors), and visibly thicker/correctly-colored visualizer bars.
  Full regression suite re-confirmed clean (60/60, 75/0/1-known/0,
  full smoke pass - engine untouched); 10/10 repeat full-app runs
  clean under `timeout`.
- **2026-08-06** (continued further still): user explicitly asked for
  full parity with the Pascal ticker, superseding MIG-0097's static
  stand-in ("Can you please make sure that the C11 port works exactly
  as the pas codebase for this?"), and confirmed via AskUserQuestion
  that both the AND-mask/horizontal-scroll system and the vertical
  multi-line-transition system were in scope. MIG-0098 traced the
  full system in `MainWin.pas` and implemented a new `gui_ticker`
  module (`gui/include/gui/ticker.h`, `gui/src/ticker.c`):
   - AND-mask text rendering: the original ANDs a white-background
     text mask against the skin's own background pixels (`CopyMode :=
     cmSrcAnd`) rather than drawing opaque text over a solid box -
     ANDing with white is the identity, so the skin's own art shows
     through everywhere except where letters darken it. Cairo has no
     bitwise-AND raster operator, so this was hand-implemented: render
     text to an off-screen surface, read back its raw pixels and the
     skin's raw pixels, AND them per-channel in a C loop, composite
     the result.
   - Horizontal ticker-scroll (1px/~30ms tick, ~1.5s pause at each
     end) and vertical multi-line slide transition on song change,
     the latter deliberately narrowed to a single-step (Prev/Next-
     adjacent) animation rather than the original's fuller N-line/
     ">16 away" jump-then-catch-up logic - a disclosed simplification,
     not a silent one (see MIG-0098's debt list).
   - Double-click to pause/resume the scroll, and click-drag to
     manually scrub it - both ported directly from the original's
     `FormMouseDown`/`MoveScr` handlers.

  Verified via clean rebuild (zero warnings), full regression suite
  green, 10/10 stability runs, and real single-window screenshots
  (driven by synthetic X11 input via a small libXtst harness) showing
  each piece working: the skin's bezel visible unaltered around the
  ticker with dark text over its own light backdrop (not an opaque
  box); horizontal scroll advancing over several seconds; a genuine
  mid-animation frame of the vertical slide transition after clicking
  Next; and the double-click pause/resume toggle holding then
  resuming the scroll. This also resolves MIG-0097's static-title
  debt item. All screenshots and the test harness were removed from
  the scratchpad after verification.
- **2026-08-06** (continued further still): user reported the volume
  and progress-bar slider "handles" don't look like the Pascal
  version, and that timeline scrubbing doesn't work. MIG-0099 traced
  both:
   - The handle appearance gap was already known/documented (zones.h's
     own file comment) - `gui_hslider_draw` drew a plain flat gray
     rectangle instead of the real handle bitmap TMoveZone.AddBitmaps
     pulls from the skin (a triangular wedge for volume, a rounded
     pill for progress), color-keyed via the source rect's own
     top-left pixel as the transparent color. Cairo has no color-key
     compositing primitive, so this was hand-implemented the same way
     as the ticker's AND-mask rendering (MIG-0098): build a small
     ARGB32 surface with alpha=0 on key-color pixels, composite that.
   - Re-traced FormMouseDown/FormMouseMove's actual slider-drag
     algorithm and found this port's old click-to-fraction mapping
     wasn't how the original works at all: clicking the handle itself
     starts a relative/delta drag; clicking elsewhere in the track
     immediately jumps the handle to be centered under the click
     point, THEN starts the same delta drag - and the travel range is
     the real track-width-minus-handle-width, not an assumed fixed
     thumb size. Replaced with `gui_hslider_press`/`gui_hslider_drag`
     matching this exactly.
   - Found and fixed a real, independent bug while investigating:
     `on_motion`'s progress-slider-drag branch was missing the
     `gtk_widget_queue_draw` call its volume-slider sibling already
     had, so a progress-bar drag's value/seek updated correctly but
     the screen only caught up on the next ~30ms visualizer tick
     rather than immediately.
   - Empirically confirmed (synthetic X11 drags) that the underlying
     drag-to-seek engine logic was already correct before this entry -
     the "doesn't work" report is best explained by the flat, easy-to-
     miss gray thumb (fixed by (1)) and the redraw lag (fixed by (3)).

  Verified via clean rebuild (zero warnings, both engine and gui),
  full regression suite green, 10/10 stability runs, and real
  screenshots confirming both handles now render as the real skin art
  with clean edges, dragging tracks the mouse correctly and clamps at
  the real travel range, and a bare click on empty track jumps the
  handle to be centered under it exactly as the original.
- **2026-08-07**: user reported progress-bar scrubbing doesn't work on
  SNDH files (Temple_of_Asherah.sndh), and pressing Pause twice while
  stopped starts the song, unlike the Pascal version. MIG-0100 traced
  and fixed both, plus one more found auditing the same code:
   - SNDH seeking: sndh_file.h's own file comment had incorrectly
     written off the TIME tag as "UI-only" - re-tracing Players.pas's
     RerollMusic/atari.pas's Atari_SeekTo showed SNDH seeking is real
     in the original and driven by the same Global_Tick_Max mechanism
     AY/YM/VTX already use, just populated from the TIME tag (seconds
     * PlayFreq, or a 5-minute fallback) rather than a file-format
     duration field. Wired this through at the engine level
     (sndh_file.c now retains what it previously scanned-past-and-
     discarded, sets the real atari.tick_count_max instead of an
     effectively-unbounded placeholder) and exposed it via
     player_get_tick_position/player_get_seconds_per_tick - no
     gui/src changes needed, the GUI's seek logic was already generic.
     Confirmed a large forward seek is genuinely slow (tens of seconds
     of CPU) - matches the original's own lack of a seek fast-path for
     this format, not a regression.
   - Pause-while-stopped starting playback: ButPauseClick's `if not
     IsPlaying then exit` guard (MainWin.pas:971-975) was missing
     entirely in this port - pressing Pause while stopped set an
     internal paused flag with nothing running to observe it, then the
     NEXT Pause press saw that stale flag and started the song fresh.
     Fixed by guarding on the existing "is a play session active"
     proxy this port already uses elsewhere.
   - Play-while-paused resuming (not user-reported, found while
     auditing): PlayClick's own `if IsPlaying then Exit` means Play is
     ALSO a no-op while paused in the original (only Pause resumes) -
     this port let Play silently resume playback during a pause too.
     Fixed with the same guard pattern.

  Verified via clean rebuild, full regression suite green (including
  re-confirming the SNDH WAV-export comparison still matches its
  known baseline, and a direct re-check of Temple_of_Asherah.sndh
  itself against the oracle at the same bound the corpus sweep uses),
  10/10 stability runs, and real screenshots showing a seek visibly
  progressing over several seconds and both button fixes holding
  correct state through Stop/Pause/Pause and Play/Pause/Play sequences.
- **2026-08-07** (continued): user asked to verify seeking works
  identically to Pascal across ALL formats, not just the ones already
  wired up. MIG-0101 audited every format's RerollMusic/Atari_SeekTo
  branch and cross-checked against the original's own embedded
  FILETYPES resource (extracted from Ay_Emul.res) - finding that ALL
  14 remaining tracker formats (PT1/PT2/PT3/STC/ASC/ASC0/ST1/STP/PSC/
  FLS/FTC/SQT/GTR/FXM/PSM) are registered `type=AY` in the original
  and so get real seeking through the exact same mechanism true .ay
  files use, via each format's own GetTimeXXX duration precompute -
  directly contradicting this port's own earlier "no Global_Tick_Max,
  UI-only" framing, which was correct for the earlier engine-
  correctness phase but never revisited for this phase's actual seek-
  parity needs. It also means these formats never naturally end
  during real playback in this port (they just loop the pattern data
  forever) - a real playback-correctness gap on top of the seeking one.

  Given the true size (13-14 separate, substantial pattern-opcode
  interpreters), the user chose to pilot PT3 first. Fully ported:
  GetTimePT3 itself (a faithful line-by-line C transcription, cross-
  checked opcode-by-opcode against the existing, oracle-validated
  PatternInterpreter to confirm byte-consumption amounts match),
  CheckLoopAndStop wiring (real natural end-of-song), and player.c's
  tick-position/seconds-per-tick cases. Discovered along the way that
  wiring up PT3's real duration broke two existing regression tests
  that had - without anyone realizing it - been relying on PT3 never
  stopping on its own: the Pascal oracle's own RunPT3FileTest harness
  runs PT3 with a documented sentinel Global_Tick_Max specifically
  because GetTimePT3 wasn't ported on either side when that harness
  was written. Since the oracle is never edited, added a matching
  `--ignore-end` flag to ay_player (and an equivalent direct fix in
  dump_engine_state.c) so the regression tests keep comparing
  equivalent content, without weakening real playback's own default
  natural-end behavior at all.

  Verified via clean rebuild (engine/gui/ay_player, zero warnings),
  full regression suite green (75/75, including 5 corpus PT3 files
  that turned out to have real durations shorter than the fixed test
  window - all pass again with --ignore-end), 10/10 stability runs,
  a 40-file random spot-check against the much larger all_tunes/
  corpus (crash/hang check only), and real screenshots + a temporary
  debug trace confirming forward seeking, backward seeking (full
  reload-from-scratch), and natural end-of-song (a 3.84s PT3 file
  correctly stopping instead of looping forever) all work exactly as
  traced from the Pascal source. The other 13 tracker formats are
  explicitly tracked, open, user-approved next-up work (MIG-0101's
  own debt list), not a settled scope decision.
