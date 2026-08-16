# gui/include/gui/

Public headers for the GTK2 desktop GUI (top-level; the `dialogs/`
subdirectory has its own README).

## ipc.h

Declares the single-instance IPC mechanism (`gui_ipc_*`): a second
invocation of the app forwards its file/directory argument to the
already-running instance over a named Unix domain socket instead of
opening a second window.

Ported from: `WinVersion.pas` (its `IPCServer`/`IPCClient`
`StartIPC`/`StopIPC`/`IPCSendParams` mechanism, backed by Lazarus's
`TSimpleIPCServer`/`TSimpleIPCClient`). Scoped narrower than the
original: only forwards a single plain path argument, not the full
`CommandLineInterpreter` flag set (`MainWin.pas`).

## mainwin.h

Declares `gui_mainwin`, the main application window struct and its
lifecycle functions, aggregating the mixer, playback, playlist,
skin, ticker, tools, visualizer and zones subsystems.

Ported from: `MainWin.pas`'s `TFrmMain`.

## mixer_win.h

Declares `gui_mixer_win`, the mixer window: six per-channel pan/level
sliders and the AY/YM chip-type radio buttons.

Ported from: `Mixer.pas`'s `TFrmMixer`, scoped to only the controls
that affect actual audio output. BASS/proxy/network settings, Atari
MFP/DMA/interrupt-frequency overrides, chip-frequency overrides,
digidrum amplitude and filtering are not ported (see
`migration_debt.yaml`).

## playback.h

Declares `gui_playback`, a background-thread playback driver wrapping
`engine/ay_engine/player.h` and the existing ALSA output wrapper
(`tools/ay_player/include/player/alsa_output.h`, reused directly).
Implements pause/resume/position/volume at the caller-driving-loop
level using C11 atomics for thread-safety.

Ported from: Not a direct port of a single `.pas` file — original
infrastructure/glue built on top of the already-ported engine, needed
because the Pascal original's playback loop and this port's pull-based
`player_make_buffer` engine interface don't share a structure.

## playlist.h

Declares `gui_playlist`/`gui_playlist_entry`, the playlist data model:
one entry per playable file, expanded to one entry per subsong for
multi-song `.ay` files, plus add-file/add-directory (recursive scan)
and next/previous/clear operations.

Ported from: `PlayList.pas`'s core data model (`TPlayListItem`/
`PlayListItems`) and `Add_File`'s directory-scan path, plus the
subsong-expansion behavior of `Players.pas`'s `OpenAYFile`. Scoped
narrower than the original: no `.ayl`/`.cue`/`.m3u` playlist-file
load/save, no deduplication, and no conversion-menu features beyond
WAV (see `migration_debt.yaml`).

## playlist_win.h

Declares `gui_playlist_win`, the playlist window: file list view,
add-file/add-folder dialogs, and window show/hide (hide-not-destroy)
semantics.

Ported from: `PlayList.pas`'s `TFrmPLst` window, built with idiomatic
GTK2 widgets (`GtkTreeView`/`GtkListStore`, `GtkFileChooserDialog`)
rather than generated from `PlayList.lfm`. The hide-on-close behavior
mirrors `MainWin.pas`'s `ButList` toggle (`Is_On`).

## regions.h

Declares `gui_apply_main_window_shape` and the About-window equivalent,
building the main window's irregular (non-rectangular) silhouette from
a static span table and applying it via
`gdk_window_shape_combine_region`.

Ported from: `MainWin.pas`'s `PrepareRgn` (main window shape) and,
for the About window's own shape, `About.pas`'s equivalent region
setup.

## skin.h

Declares the `.ays` skin file loader/decoder (LZH-compressed author
string, comment string, and embedded `.bmp`).

Ported from: `MainWin.pas`'s `LoadSkin`/`SetMainBmp`.

## ticker.h

Declares the Cairo-based scroll-text ticker: AND-mask text rendering
against the skin background, horizontal scroll for overflowing text,
and vertical slide transitions between playlist entries.

Ported from: `MainWin.pas`'s scroll-text ticker (`RedrawScroll`,
`Scr_Left`/`HorScrl_Offset`/`Scr_Pause` state machine, `Item_Displayed`/
`Scroll_Distination`/`Scroll_Offset` machinery), simplified to the
common single-transition case.

## tools_win.h

Declares `gui_tools_win`, the Tools dialog.

Ported from: `Tools.pas`'s `TFrmTools`, scoped down after a full trace
of all 5 original tabs (`GenTools`/`FTypTools`/`SearchTool`/
`FIDOTools`/`PListOpts`): Windows registry/shell-integration features,
BASS streaming prescan, and Turbosound-pair-only settings are out of
scope (see `migration_debt.yaml`).

## visualizer.h

Declares the spectrum/amplitude visualizer (`SensSpa`/`SensAmp`
display areas).

Ported from: `MainWin.pas`'s spectrum/amplitude visualizer
(`RedrawVisSpectrum`/`RedrawVisChannels`/`CalculateSpectrumPoints`) and
`AY.pas`'s `FillVis`/`VisPoints` sampling ring buffer. The oscilloscope/
time-display mode (`SensTime`) is not ported (see
`migration_debt.yaml`).

## zones.h

Declares the button/LED/slider hit-testing and redraw structs used by
the main window's clickable skin regions.

Ported from: `MainWin.pas`'s `TButtZone`/`TLedZone`/`TMoveZone` classes.
`TSensZone` (visualizer click zones) is not ported. Hit-testing is
simplified to axis-aligned bounding boxes instead of the original's
true rounded-rectangle GDI regions (documented simplification, not a
functional gap).
