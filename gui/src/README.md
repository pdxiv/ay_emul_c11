# gui/src/

Implementation files for the GTK2 desktop GUI (top-level; the
`dialogs/` subdirectory has its own README).

## ipc.c

Implements `gui/include/gui/ipc.h`: a Unix domain socket server/client
in the user's XDG runtime directory, used to detect an already-running
instance and forward a single file/directory path argument to it.

Ported from: `WinVersion.pas`'s `IPCServer`/`IPCClient` mechanism
(`StartIPC`/`StopIPC`/`IPCSendParams`), narrowed to a per-user socket
(the original's `IPCServer.Global := True` was machine-wide) and to
forwarding one plain path argument rather than the full
`CommandLineInterpreter` flag set.

## main.c

Program entry point: initializes GTK, checks for an already-running
instance via `gui_ipc_init` (handing off a command-line file argument
and exiting if one exists), otherwise creates the main window,
optionally loads a file passed on the command line, and runs the GTK
main loop.

Ported from: Not a direct port of one `.pas` file — mirrors the
early-exit/single-instance startup sequence documented in
`WinVersion.pas`'s IPC usage, but is otherwise original GTK
application bootstrap code (Lazarus/Pascal projects don't have an
equivalent explicit `main`).

## mainwin.c

Implements `gui/include/gui/mainwin.h`: creates and wires the main
application window (skin bitmap, irregular window shape, ticker,
visualizer, zones/buttons, sliders, and the playlist/mixer/tools/about
sub-windows), and handles input events.

Ported from: `MainWin.pas`'s `TFrmMain`. Deferred pieces are tracked
in `migration_debt.yaml`/`PHASE5_GUI_PROGRESS.md` (e.g. visualizer
click zones, seeking for the remaining `type=AY` tracker formats).

## mixer_win.c

Implements `gui/include/gui/mixer_win.h`: applies slider/radio-button
changes to the AL/AR/BL/BR/CL/CR level tables and chip type, mirroring
`Set_Mode_Manual`'s "assign fields, then recalculate" pattern.

Ported from: `Mixer.pas`'s `TFrmMixer`, scoped to the core AY channel
controls only (see `gui/include/gui/mixer_win.h`).

## playback.c

Implements `gui/include/gui/playback.h`: a background thread that
pulls decoded audio from the engine (`ay_engine/player.h`) and writes
it to ALSA, exposing pause/resume/seek/volume via atomics, with no
mutex needed for the single-writer-per-field fields.

Ported from: Not a direct port of a single `.pas` file — original
glue/infrastructure combining the already-ported engine's pull-based
`player_make_buffer` with a caller-driven playback loop; the reused
ALSA output wrapper itself lives in `tools/ay_player`, not the engine.

## playlist.c

Implements `gui/include/gui/playlist.h`: playlist entry storage, add
single file, recursive directory scan (`Add_File`), and
next/previous/clear navigation, expanding multi-song `.ay` files into
one entry per subsong.

Ported from: `PlayList.pas`'s core data model and directory-scan path,
plus the subsong-expansion behavior mirrored from `Players.pas`'s
`OpenAYFile`.

## playlist_win.c

Implements `gui/include/gui/playlist_win.h`: the playlist window UI —
`GtkTreeView` list, add-file/add-folder buttons with
`GtkFileChooserDialog`, and item double-click/adjust handling (opening
the item-edit dialog).

Ported from: `PlayList.pas`'s `TFrmPLst` window, hand-built with
idiomatic GTK2 widgets rather than generated from `PlayList.lfm`.

## regions.c

Implements `gui/include/gui/regions.h`: builds and applies the main
window's and About window's irregular (non-rectangular) shapes from
static span tables via `gdk_window_shape_combine_region`.

Ported from: `MainWin.pas`'s `PrepareRgn` (main window's `rgn.inc`
span table) and `About.pas`'s equivalent shape data (`rgn2.inc`).

## skin.c

Implements `gui/include/gui/skin.h`: parses the `.ays` skin file
format (24-byte ID header, little-endian uncompressed size, LZH-
compressed author/comment strings and embedded `.bmp`), decompressing
via the engine's `lh5_decompress` and decoding the bitmap via
`GdkPixbufLoader`.

Ported from: `MainWin.pas`'s `LoadSkin`/`SetMainBmp`, with the format
details cross-checked against `lh5.pas`'s `InitLZHDepacker` and the
real `ay_emul/Ay_Emul2.ays` file.

## ticker.c

Implements `gui/include/gui/ticker.h`: renders the scrolling title
text using AND-mask compositing against the skin background (Cairo),
plus horizontal scroll and vertical slide-transition animation.

Ported from: `MainWin.pas`'s scroll-text ticker (`RedrawScroll` and
its `Scr_Left`/`HorScrl_Offset`/`Scr_Pause`/`Item_Displayed` state
machines).

## tools_win.c

Implements `gui/include/gui/tools_win.h`: the Tools dialog's wiring,
including the skin author/comment label refresh.

Ported from: `Tools.pas`'s `TFrmTools`, scoped down after a full trace
of the original's 5 tabs (see `gui/include/gui/tools_win.h` for the
itemized list of what was found out of scope).

## visualizer.c

Implements `gui/include/gui/visualizer.h`: draws the spectrum and
amplitude bar displays from each AY channel's tone-period and
amplitude register values on a log scale.

Ported from: `MainWin.pas`'s `RedrawVisSpectrum`/`RedrawVisChannels`/
`CalculateSpectrumPoints`, with the default chip-frequency constant
from `settings.pas`'s `AY_FreqDef`.

## zones.c

Implements `gui/include/gui/zones.h`: draws and hit-tests button, LED,
and slider ("move zone") regions of the skinned main window.

Ported from: `MainWin.pas`'s `TButtZone`/`TLedZone`/`TMoveZone`
classes, with rectangular (not true rounded-region) hit-testing as a
documented simplification.
