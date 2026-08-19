# gui/include/gui/

Public headers for the GTK2 GUI layer sitting on top of the shared `engine/` library. Most of these are C11/GTK2/Cairo ports of specific `ay_emul/MainWin.pas` subsystems (the skinned main window's button/LED/slider hit-testing, skin loading, window shaping, scroll-text ticker, spectrum/amplitude visualizer) or of their own dedicated Pascal units (`Mixer.pas`, `PlayList.pas`, `Tools.pas`); `playback.h` is caller-driving-loop glue with no 1:1 Pascal source, since the original engine has no background-thread playback model to port from.

## ipc.h

Declares `gui_ipc_init`/`gui_ipc_set_callback`/`gui_ipc_shutdown` and the `gui_ipc_file_cb` callback type for the app's single-instance file-forwarding mechanism: a second invocation hands its file/directory argument off to the already-running instance (over a Unix domain socket) and exits instead of opening a second window.

Ported from: ay_emul/winversion.pas (`IPCServer`/`IPCClient`, backed by Lazarus's `TSimpleIPCServer`/`TSimpleIPCClient`), narrowed to forwarding a single plain path argument rather than `MainWin.pas`'s full `CommandLineInterpreter` (MainWin.pas:1118+) flag set (`-s`/`-b`/`-z`/`-y`/`-q`/`-t`/`-a`/etc. for sample rate, bit depth, Z80/AY/MFP clock overrides). This narrowing is tracked as `MIG-0074` and is `validated`, not open debt - the single-path use case is the real-world scenario ("open a file, app is already running") this port set out to cover.

## mainwin.h

Declares `gui_mainwin`, the top-level struct for the whole skinned main window: the `GtkWidget* window`/drawing area, the loaded `gui_skin`, embedded `gui_playback`/`gui_playlist_win`/`gui_mixer_win`/`gui_tools_win`/`gui_visualizer`/`gui_ticker` sub-windows and state, every `gui_button`/`gui_led`/`gui_hslider` zone, drag/press bookkeeping for buttons and sliders, and the two GLib timer ids (the ~200ms UI tick and the ~30ms visualizer tick). Exposes `gui_mainwin_create`/`gui_mainwin_destroy` and `gui_mainwin_set_vis_period`.

Ported from: ay_emul/MainWin.pas (`TFrmMain`), the largest single unit in the original app - this header is the aggregation point for essentially every other file in this directory. Several fields close specific, named migration-debt items directly in their own comments (`MIG-0071` playlist window, `MIG-0078` mixer window, `MIG-0079` real seeking, `MIG-0088` per-item overrides, `MIG-0094` visualizer, `MIG-0095` tools window, `MIG-0098` ticker) - see gui/src/mainwin.c's own file comment for the authoritative status of what's wired versus deferred.

## mixer_win.h

Declares `gui_mixer_win`: the six per-channel pan/level `GtkVScale` trackbars, the beeper/digidrum amplitude trackbar, the AY/YM chip-type and resampler-quality radio buttons, and the five "use playlist item's own override" checkboxes that gate `ItemEdit.pas`-style per-item overrides at load time. Built with idiomatic GTK2 widgets rather than generated from the original's `.lfm`, since `Mixer.pas` isn't skin-rendered.

Ported from: ay_emul/Mixer.pas (`TFrmMixer`), scoped to the controls that affect actual audio output. The beeper-amplitude and resampler-quality controls were originally excluded by `MIG-0078` and later brought in scope by `MIG-0106`. Everything else in the real `Mixer.pas` - BASS/proxy/network settings, Atari MFP/DMA/interrupt-frequency overrides, chip-frequency overrides, the Atari DMA-sound amplitude trackbar - is documented as out of scope in migration_debt.yaml (out of this port's scope entirely, Atari/SNDH-specific with SNDH still silent per `MIG-0021`, or needing engine plumbing this port doesn't add).

## playback.h

Declares `gui_playback`, the background-thread playback driver: the embedded `player`, loaded-file metadata (title/author/comment, converted from CP1251 to UTF-8), atomic pause/stop/finished/frames-played flags, the volume double, and seek-request fields (`seek_requested`/`seek_target_tick`). Exposes load/play/pause/stop/volume/position/seek functions plus `gui_playback_get_progress_fraction` and `gui_playback_duration_seconds` for formats with a known fixed duration.

This is C-idiomatic glue with no 1:1 Pascal source: it wraps `engine/include/ay_engine/player.h`'s pure pull-based `player_make_buffer` (which has no concept of pause or playback rate) plus the existing ALSA output wrapper in a caller-driving background pthread, implementing pause/resume/position/volume entirely at this level per the approved porting plan. Pause/resume/seek behavior corresponds to ay_emul/Players.pas's `RerollMusic` seeking model (`MIG-0079`/`MIG-0100`/`MIG-0101`/`MIG-0103`/`MIG-0104`) and ay_emul/MainWin.pas's `ButPlay`/`ButPause`/`ButStop` handlers, without being a line-for-line port of either.

## playlist.h

Declares the core playlist data model: `gui_playlist_overrides` (per-item chip-type/channel-mode/frequency/text overrides), `gui_playlist_defaults` (playlist-wide fallback overrides), `gui_playlist_entry`, and `gui_playlist` itself, plus add/remove/find/sort/dedup functions and `.m3u`/`.ayl` load/save.

Ported from: ay_emul/PlayList.pas (`TPlayListItem`/`PlayListItems`, `Add_File`'s directory-scan path, `DeletePlayListItem`, `FindPLItem.pas`'s `FindItem`/`FindItem2`, `MyQuickSort`'s five comparators plus `RandomSortClick`, `Deduplicate1Click`, `SBSaveClick`'s M3U writer, `SaveAYL`/`LoadAYL`) and ay_emul/ItemEdit.pas (the override fields themselves, `MIG-0088`). The `.ayl` reader/writer is a real-syntax-compatible SUBSET of the original grammar (`MIG-0091`, open in migration_debt.yaml): no PLDef global-defaults header on save, no "ts" Next-linked subitem chains, and multi-song `.ay` entries collapse to song_index 0 on save/reload. `.ayl`/`.cue`/`.m3u` conversion-menu export beyond WAV, and `.cue`-embedded-stream entries, are out of scope entirely (see migration_debt.yaml).

## playlist_win.h

Declares `gui_playlist_win`, the GTK2 playlist window (`GtkTreeView`/`GtkListStore`-backed): window/tree-view/model, the `gui_playlist_play_cb` callback fired when a specific entry should play, find-dialog state, and the session-lifetime `gui_playlist_defaults`. Exposes create/toggle-visible/add-path/add-files-dialog/add-folder-dialog/remove-selected/find-dialog/next/prev/destroy functions. Like `Mixer.pas`, hand-built with idiomatic GTK2 widgets rather than generated from `PlayList.lfm`.

Ported from: ay_emul/PlayList.pas (`TFrmPLst`) and ay_emul/FindPLItem.pas (`TFrmFndPLItm`'s Anywhere/Author/Title/Filename find dialog). "Find All" is a documented narrowing of the original's true multi-select highlight (GTK's single-selection tree view can only visibly select the last match, though every match is still counted).

## regions.h

Declares `gui_apply_main_window_shape` and `gui_apply_about_window_shape`, which build the app's two irregular (non-rectangular) window silhouettes from static span tables and apply them via `gdk_window_shape_combine_region`.

Ported from: ay_emul/MainWin.pas's `PrepareRgn` (main-window shape, from `rgn.inc`'s span table) and ay_emul/About.pas's own 343x346 shape (from `rgn2.inc`, a separate 300-span table). No DPI/`Scale` support yet - always applies the table at 1:1 pixel scale; window scaling (`CBDoubleSz`, `Tools.pas`) is documented open debt (see migration_debt.yaml under the Tools/`MIG-0095` entry) requiring rescaling the skin bitmap, every button/LED/slider rect, and these region tables together.

## skin.h

Declares `gui_skin` (the sprite-sheet `GdkPixbuf* bitmap` plus owned `author`/`comment` strings) and `gui_skin_load_default`/`gui_skin_load_file`/`gui_skin_free` for loading and decompressing the app's `.ays` skin file format (a 24-byte `SkinId` header, a little-endian uncompressed size, then a bare `-lh5-` compressed stream decoded via the engine's `lh5_decompress`, whose payload is an author string, a comment string, and a raw `.bmp`).

Ported from: ay_emul/MainWin.pas's `LoadSkin`/`SetMainBmp`. The `.ays` format itself was confirmed by direct decode of the real `ay_emul/Ay_Emul2.ays` default skin (not guessed), and the default skin is baked in as a generated C byte array (gui/src/default_skin.c) per `MIG-0073`'s tracked simplification; `gui_skin_load_file` for user-supplied external skins is implemented and later wired up for real by `Tools.pas`'s `GBSkin` (`MIG-0095`).

## ticker.h

Declares `gui_ticker`: the settled/transitioning display text, cached text width, vertical-slide transition state (old/new text, offset, direction), and horizontal-scroll state (offset, direction, pause counter, drag state). Exposes init/set-target/tick/draw/toggle-scroll/drag functions implementing the full AND-mask scroll-text rendering, horizontal ticker-scroll, vertical slide transition on song change, double-click pause, and click-drag scrub.

Ported from: ay_emul/MainWin.pas's scroll-text ticker (`RedrawScroll`'s AND-mask `CopyMode := cmSrcAnd` technique, `Scr_Left`/`HorScrl_Offset`/`Scr_Pause`'s horizontal state machine, `Item_Displayed`/`Scroll_Distination`/`Scroll_Offset`'s vertical transition, and `MoveScr`/`DoMovingScroll`'s drag). This is `MIG-0098` (validated), superseding an earlier static-text stand-in (`MIG-0097`). The vertical transition is simplified to the common single-step (Prev/Next-adjacent) case - an arbitrary jump or a second transition arriving mid-animation snaps immediately rather than replicating the original's full N-line/">16 away" jump-then-catch-up logic, a documented narrowing rather than a silent one.

## tools_win.h

Declares `gui_tools_win`: window, the `mw` back-pointer to the owning `gui_mainwin` (kept opaque here to avoid a circular include), and the three entry/label widgets for default directory, visualizer period, and skin info. Exposes create/toggle-visible/destroy.

Ported from: ay_emul/Tools.pas (`TFrmTools`), covering only the genuinely portable subset of its 5 tabs (`MIG-0095`, open in migration_debt.yaml for the untouched remainder): `EMFolder` (default Open-dialog directory), `EVisPeriod` (visualizer sample-timer interval), and `GBSkin` (load/revert skin file). Explicitly NOT ported: `FTypTools`/`FIDOTools` (Windows file-type/registry/shortcut integration, no Linux equivalent in scope), `CBStrPrescan` (BASS-only), `CBForceLoop` (TSMode-only, already excluded by `MIG-0007`), `CBDoubleSz` (window 1x/2x scaling, a separate rescaling project), and `SearchTool` (superseded by the playlist's own "Add Folder").

## visualizer.h

Declares `gui_visualizer`: spectrum/amplitude visibility toggles, the log-scale `spa_points` bucket-boundary table, the decaying `spa_prev` peak-marker state (persists across the session), per-frame `spa_bar`/`spa_marker`/`spa_has_marker` render state, and the three post-Calc channel amplitudes. Exposes init/tick/draw/handle-click.

Ported from: ay_emul/MainWin.pas's `SensSpa`/`SensAmp` display areas (`RedrawVisSpectrum`'s `CalculateSpectrumPoints`/`Spa_points` bucketing of each AY channel's tone-period register, `AYVisualisation`'s envelope-aware Calc block, `RedrawVisChannels`) and ay_emul/AY.pas's `FillVis`/`VisPoints` sampling ring buffer, ported for real into `engine/include/ay_engine/ay.h`'s `ay_vis_point`/`ay_engine_init_vis`/`ay_engine_get_vis_point` per explicit user request rather than approximated. This is `MIG-0094`. The oscilloscope/`SensTime` time-display-mode toggle is NOT ported (a separate time-digit-strip feature, documented open debt in migration_debt.yaml), and `spa_points` is computed once from a fixed reference chip clock rather than each loaded file's own actual clock (a documented cosmetic-only simplification). Turbosound's second AY chip is now shown too, matching `AYVisualisation`/`RedrawVisSpectrum`'s own per-channel max-across-chips combining when `TSMode` (`MIG-0110`) — `amp_a`/`amp_b`/`amp_c` above are that already-combined result.

## zones.h

Declares the generic hit-test/redraw primitives shared by every skinned control: `gui_button` (dest rect, normal/pushed source rects, on/pushed state), `gui_led` (draw-only, off/on source rects), and `gui_hslider` (track rect, thumb bitmap/size, 0.0-1.0 value, drag-anchor state) with their hit-test/press/drag/draw functions.

Ported from: ay_emul/MainWin.pas's `TButtZone`/`TLedZone`/`TMoveZone` (MainWin.pas:112-168). `TSensZone` (spectrum/amp/time-display click zones) is NOT ported here - out of scope for this milestone per migration_debt.yaml. Two documented simplifications: hit-testing is a plain axis-aligned bounding box rather than the original's true rounded-rectangle GDI region (`CreateRoundRectRgn`), and slider drag replicates the real two-mode press behavior (grab-the-thumb vs. click-to-center) but not the original's `OldX`/`Delt` edge-clamp quirk at the track boundary (`MIG-0099`).
