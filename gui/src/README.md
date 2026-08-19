# gui/src/

Implementation of the GTK2 shell that hosts the shared `ay_engine`/`ay_player` core: the skinned main window, its child windows (playlist, mixer, tools), the playback thread, and single-instance IPC. Each file below corresponds to one Delphi form or unit from the original Windows `ay_emul` application; most files carry detailed inline comments citing the exact `ay_emul/*.pas` line ranges they port, which this README summarizes rather than duplicates.

## ipc.c

A POSIX AF_UNIX `SOCK_STREAM` socket at `$XDG_RUNTIME_DIR/ay_emul_c11.sock` providing single-instance behavior: `gui_ipc_init` first tries connecting as a client (if another instance is listening, it hands off the one `argv` file path and returns `false` so `main()` can exit before creating any window); otherwise it becomes the server, integrating `accept()` via `g_io_add_watch` so it never blocks the GTK main loop. Only a single plain file/directory path is forwarded, not the original's full flag set.

Ported from: ay_emul/WinVersion.pas (`IPCServer`/`IPCClient`, `IPCSendParams`/`StartIPC`; `TSimpleIPCServer`/`TSimpleIPCClient`, itself Unix-domain-socket-backed on Linux). `IPCServer.Global := True` (all users on the machine) is narrowed to the current user only, since `XDG_RUNTIME_DIR` is inherently per-user — a documented simplification (MIG-0074, validated), not a silent gap.

## main.c

Program entry point: initializes GTK, resolves the single optional command-line file argument, runs `gui_ipc_init` for single-instance hand-off, builds the main window, loads the initial file if any, and runs the GTK main loop.

Ported from: ay_emul/WinVersion.pas (`CommandLineInterpreter`'s single-file-argument path) and the project's own `.dpr` entry point; MainWin.pas's `TApplication.Run` equivalent.

## mainwin.c

The skinned main player window (`TFrmMain`) — its buttons, sliders, LEDs, drag handling, key bindings, tray icon, and the two GLib timers (`on_timer` at 200ms for playback/LED state, `on_vis_timer` at 30ms for the visualizer and ticker). Owns `do_load_song`, which loads a file/subsong, applies `ItemEdit.pas`-style per-item overrides (chip type, channel mode, frequencies, channel count — gated behind Mixer's "Get from list" checkboxes), and updates the window title/tray tooltip/ticker text from real or override-supplied Author/Title metadata. Several real bugs found and fixed during this port are called out inline: `gdk_window_begin_move_drag` previously targeted the child drawing-area's `GdkWindow` instead of the toplevel's (silently no-opping window dragging on some window managers), and Pause-while-stopped previously left `paused=true` with no running thread to observe it (MIG-0100).

Ported from: ay_emul/MainWin.pas (`TFrmMain`: `SetMainBmp`'s button/LED/slider zone table, `FormMouseDown`/`FormMouseMove`/`FormMouseUp`, `FormKeyDown`, `VisTimerEvent`/`DoVisualisation`, `PlayCurrent`/`ButPlayClick`/`ButPauseClick`/`ButStopClick`, `TrayIcon1MouseUp`, `PrepareRgn`'s use via `gui/src/regions.c`) plus PlayList.pas's `PlayItem` override-application logic (MIG-0088).

## mixer_win.c

The live Mixer window: six per-channel amplitude sliders (AL/AR/BL/BR/CL/CR) plus a Beeper slider, AY/YM chip-type radios, averager/FIR-filter radios, and five "Get from list" checkboxes gating whether playlist-item overrides apply on load. A background timer (`on_sync_timer`, 500ms) re-syncs every control to the currently-loaded file's actual `ay_engine` state (since a fresh load resets the engine to its own defaults), using a `syncing` flag rather than `g_signal_handlers_block_by_func` to suppress the change handlers during programmatic updates.

Ported from: ay_emul/Mixer.pas (`GBChAmp`/`GBChType`/`GBResamp`/`SBHelper`, `RBResamAvgClick`/`RBResamFIRClick`, `SBHelperClick`'s echo-value convention) and MainWin.pas's `Set_Mode_Manual`/`Set_Chip_Frq`. The surrounding `GBChFrq`/`GBIntFrq` current-value display panels are out of scope; only the "Get from list" gate checkboxes themselves are reproduced (read directly by `gui/src/mainwin.c`'s `do_load_song`).

## playback.c

The playback engine wrapper: loads a file into a `player`, opens ALSA output, and runs decoding on a dedicated pthread (`playback_thread_main`) so the GTK main loop is never blocked. Handles play/pause/stop, volume (applied as a per-sample multiply on the playback thread), and seeking — implemented as decode-forward-and-discard to the target tick, or a full reload-from-scratch when seeking backward (chip/register state is inherently sequential and can't be un-advanced). CP1251-to-UTF-8 metadata conversion matches the original's non-Windows default codepage. A real bug found and fixed during testing: a naive `atomic_store(false)` after a seek could clobber a second seek request issued while the first was still in flight; `atomic_exchange` is used instead so a new request during an in-progress seek is preserved.

Ported from: ay_emul/Players.pas (`RerollMusic`'s IsZ80EmuFileType/`FT.VTX`/`FT.YM` seek branches) and MainWin.pas's play/pause/stop/volume handlers. The original's lighter register-only "Converter" `OutProc` fast path for seeking is not reproduced (this port just discards synthesized audio) — correct but not as CPU-optimized.

## playlist.c

The playlist data model: add/remove/dedup/sort/find, `.ayl` (native format) and `.m3u`/`.m3u8` save/load, and directory scanning. `.ayl` parsing/writing round-trips per-item overrides (chip type, channel allocation, frequencies, metadata) via the same token vocabulary as the original; a handful of real tokens (Channels/Offset/Length/Address/Loop/Time/Original/Type/FormatSpec/ams_andsix) and the multi-song "ts" Next-linked-subitem construct are recognized-but-discarded or unsupported rather than causing a parse failure. Sorting uses ASCII-range case-insensitive comparison (a narrowing of the original's full-Unicode `UTF8CompareText`/`UTF8LowerCase`), documented rather than silent.

Ported from: ay_emul/PlayList.pas (`FormatScrollString`, `Deduplicate1Click`, `CompareFileNames`/`CompareTitles`/`CompareAuthors`/`CompareTypes`, `RandomSortClick`, `SaveAYL`/`LoadAYL` and their `ConvCR` comment-escaping) and FindPLItem.pas's search semantics.

## playlist_win.c

The Playlist window UI: a `GtkTreeView` list plus Add Files/Add Folder/Remove/Clear/Find/Adjust/Sort/Dedup/Save buttons, and the Find dialog (Anywhere/Author/Title/Filename radio group with Find Next's wrap-around search and Find All's select-and-count). Sorting is exposed via a popup menu from a "Sort" button rather than a right-click context menu — an idiomatic-GTK2 restructuring, not a literal form transcription, same as the other hand-built windows in this codebase.

Ported from: ay_emul/PlayList.pas (`TFrmPLst`: row activation/double-click-to-play, `SBSaveClick`'s AYL/M3U filter choice, `Deduplicate1Click`, `PopupMenu2`'s sort items, `MenuItemAdjustingClick` opening ItemEdit) and FindPLItem.pas (`Button1Click`/`Button2Click`, `RadioGroup1`'s four search-area options) and seldir.pas's `CBRecurse` option.

## regions.c

Applies the main window's and About window's non-rectangular click-through silhouettes via `gdk_window_shape_combine_region`, built by unioning static per-row span tables into a `GdkRegion`. The span tables (`RGN_SPANS`, `RGN2_SPANS`) were mechanically extracted (Python regex, spot-checked against the literal source) rather than hand-copied. The original's DPI "Scale" multiplier applied to every span is not ported — this port always applies the table at Scale=1 (see `migration_debt.yaml`).

Ported from: ay_emul/rgn.inc (via MainWin.pas's `PrepareRgn`, ~3288-3298) for the main window shape, and ay_emul/rgn2.inc (via About.pas's own window-shape setup) for the About window shape.

## skin.c

Loads the `.ays` skin file format: verifies the `"Ay_Emul 2.0 Skin File\r\n\x1a"` magic, LH5-decompresses the payload (via `ay_engine/util/lh5.h`), and parses the resulting null-terminated Author string, null-terminated Comment string, and trailing raw `.bmp` bytes (loaded through `GdkPixbufLoader`). `gui_skin_load_default` loads the embedded default skin (`gui/build/generated/default_skin.c`); `gui_skin_load_file` loads an arbitrary `.ays` from disk, used by the Tools window's "Load skin..." button.

Ported from: ay_emul/MainWin.pas (skin file ID constant at line ~84-85; the Author/Comment/bitmap payload layout at ~3696-3712/3732).

## ticker.c

The scrolling title-strip ("ticker") animation: vertical slide transitions when moving to an adjacent playlist item (Next/Prev), horizontal auto-scroll with end-pause for text too wide to fit, and manual click-drag scrubbing. Text is rendered into an off-screen mask surface and composited against the skin's background pixels using a hand-rolled per-channel bitwise AND (matching the original's `cmSrcAnd` raster op, which Cairo has no native equivalent for), not a solid-color box or alpha blend. This port's vertical transition only supports a single-line slide between two directly-adjacent items, not the original's full multi-line "still catching up" chase logic for rapid successive track changes.

Ported from: ay_emul/MainWin.pas (`RedrawScroll`/`DoVisualisation`'s `BMP_VScroll` canvas, `GetStringWnJ`'s centering logic, `Item_Displayed`/`Scroll_Distination`/`Do_Scroll`/`Scr_Pause` state, the horizontal scroll-and-pause loop at ~750-786, `MoveScr`'s drag handling).

## tools_win.c

The Tools window: a default-folder entry/browser (feeding the Open dialog's initial directory), a visualizer sample-period entry (10-100ms, invalid input snaps back to the last accepted value), and Load-skin/Default-skin buttons. A malformed or unreadable `.ays` file loaded via "Load skin..." leaves the currently-displayed skin untouched (loaded into a temporary `gui_skin` first) rather than risking a blank window on a failed in-place swap.

Ported from: ay_emul/Tools.pas (`EMFolderEditingDone`, `EVisPeriodEditingDone`, `GBSkin`'s `BChSkinClick`/`BStdSkinClick`) and MainWin.pas's `VisTimerPeriod`.

## visualizer.c

The spectrum/amplitude visualizer overlay: per-channel amplitude bars (columns 1/8/15 in the amp display box) and a log-scale frequency spectrum with decaying peak-hold markers, bucketing each AY channel's tone-period register into one of `GUI_VIS_SPA_NUM` bars via a fixed table computed once at init (`CalculateSpectrumPoints`). Both overlays are independently toggleable by clicking their respective screen regions. Uses a fixed default AY clock (`settings.pas`'s `AY_FreqDef`) for the spectrum bucket boundaries rather than each file's actual chip clock — a documented simplification, not per-file-accurate. A pen color/width (`$464646`, 3px, unantialiased to match GDI's crisp un-antialiased line style) set once at the original's `FormCreate` and easy to miss was found and applied here explicitly. `gui_visualizer_tick` reads the engine's `ts_mode` and, when a Turbosound file is playing, folds chip 2's amplitude bars and spectrum contribution into the display exactly like `MainWin.pas`'s `AYVisualisation`/`RedrawVisSpectrum` do — per-channel max across both chips (MIG-0110).

Ported from: ay_emul/MainWin.pas (`CalculateSpectrumPoints` ~603-608, `RedrawVisChannels` ~791-816, `RedrawVisSpectrum` ~820-876, `ButSpaClick`/`ButAmpClick`) and settings.pas's `AY_FreqDef`.

## zones.c

Generic hit-testing and drawing primitives shared by every skinned window (main window, About): push-buttons (`gui_button`) with normal/pushed source rects, two-state LEDs, and draggable horizontal sliders (`gui_hslider`) with color-keyed handle bitmaps. Color-keying (transparent pixels matching the handle bitmap's own `(0,0)` pixel) is implemented by hand as a raw ARGB32 surface build, since Cairo has no native color-key compositing primitive — this port additionally honors a real per-pixel alpha channel when the skin bitmap has one (a genuine 32-bit BMP), a capability the original's `TBitmap` never had, falling back to the exact original color-key behavior for ordinary 24-bit skins.

Ported from: ay_emul/MainWin.pas (`TMoveZone.AddBitmaps`'s color-key setup at ~2676-2681, `ToucheBut`/`Touche` thumb-vs-track hit testing at ~2131-2155, `PosX` slider positioning).
