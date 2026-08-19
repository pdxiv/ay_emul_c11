# gui/src/dialogs/

Modal and semi-modal dialog windows spawned from the main window and its child windows — each corresponds to one small standalone Delphi form in the original `ay_emul` application. Several of these started life as `tools/lfm_gen/lfm_gen.py`-generated `.lfm`-to-GTK skeletons (MIG-0066) before being hand-completed with real widgets and wiring; that lineage is noted per-file below where it still shows (e.g. a literal `GtkFixed` coordinate layout carried over from the generator's output).

## about.c

The real skinned About window (superseding an earlier plain `GtkAboutDialog` placeholder, MIG-0090): draws a 343x346 crop of the embedded About sprite sheet starting at `(1,2)` (matching the original's `AbDBuffer.Canvas.CopyRect` offset), an OK button and a "Help" logo button (both skinned, drawn via `gui/src/zones.c`), and the hardcoded version string "3.0" centered at `(122,260)`. Runs as a real modal loop via a private `GMainLoop` (not `GtkDialog`, since every click here is custom hit-tested against the sprite sheet, not a stock dialog button) so `gui_about_show` blocks its caller until the window is closed. The "Help" button shows an informational message rather than opening a `.chm` help file, which this port doesn't have. A real drag-to-move bug shared with `gui/src/mainwin.c` was found and fixed here too: `gdk_window_begin_move_drag` must target the toplevel window, not the child drawing area's own nested `GdkWindow`.

Ported from: ay_emul/About.pas (`TFrmAbout`: `FormCreate`'s button rects and bitmap crop, `FormMouseDown`/`FormMouseUp`/`FormMouseMove`, `FormKeyPress`'s Escape-to-close) plus MainWin.pas:2574-2577's Linux-branch version-string font/position. Uses `gui/build/generated/about_bitmap.c` (the embedded sprite sheet) and `gui/src/regions.c`'s `gui_apply_about_window_shape` (the non-rectangular window silhouette, from ay_emul/rgn2.inc).

## itemedit.c

The per-playlist-item override editor ("Playlist Item Adjusting", MIG-0088): a three-tab notebook (Metadata: title/author/program/tracker/computer/date/comment; Sound chip: chip-type override, channel-amplitude preset-or-manual override, output channel-count override, sound-chip/interrupt frequency overrides; File info: read-only diagnostics) plus playlist-wide "Load Defaults"/"Save as Defaults" buttons. The File info tab shows real data where this port has it (file type, filename, on-disk length, a throwaway-decoded duration for AY/YM/VTX/SNDH/PT3) and explicit `"n/a"` — not a fabricated value — for fields this port's engine has no equivalent for (embedded-container load Address, per-format loop point; both tracked under MIG-0103).

Ported from: ay_emul/ItemEdit.pas (`GetPlayItems`/`SetPlayItems` ~136-276, `GBFile`'s read-only diagnostic fields ~90-108, `BDefLoadClick`/`BDefSaveClick` ~198-213, `GBNChans`/`RBNStereo`/`RBNMono`/`RBNDef` MIG-0103) plus ay_emul/Mixer.pas's AY/YM chip-type radio convention (extended here with a third "unchanged" state an always-live Mixer control doesn't need).

## jmptime.c

The "Jump to time" dialog: accepts either a plain integer (seconds) or `M:SS`/`MM:SS`, pre-filled with the current playback position and showing the track's total length. Guarded exactly like the original — a no-op if nothing is playing, paused, or the current format has no known duration (`gui_playback_duration_seconds`). Re-implements the original's digit-by-digit `Val()`-based parsing with `strtol`, accepting the identical grammar. Kept as a hand-completed `lfm_gen.py` skeleton using `GtkFixed` literal coordinates (unlike playlist_win.c/mixer_win.c, which were rebuilt from scratch), since it was actually built from the generator's output rather than replacing it.

Ported from: ay_emul/JmpTime.pas (dialog layout) and ay_emul/MainWin.pas's `JumpToTime` (the `IsPlaying`/`Paused` guards, the nested `TimeValid` parser) and PlayList.pas's `TimeSToStr` (H:MM:SS / M:SS formatting).

## mxhelper.c

The Mixer window's "Presets..." helper dialog: 13 radio-button channel-allocation presets (6 AY stereo modes + 6 YM stereo modes + Mono), applying AL/AR/BL/BR/CL/CR amplitude coefficients and chip type directly to the loaded file's live `ay_engine` on "Set". The DMA/TS/BeeperMax outputs the original's `CalcModeCoefs` also computes are dropped (out of scope for this port, see the file's own header comment); the `mxhelper_calc_mode_coefs` core coefficient table is also reused directly by `gui/src/mainwin.c`'s playlist-item channel-mode override application, keeping both call sites consistent.

Ported from: ay_emul/mxhelper.pas (dialog layout, default selection = item 6 "YM ABC Stereo") and MainWin.pas's `CalcModeCoefs` (~1953-2027) and Mixer.pas's `SBHelperClick` (~601-613, the `ChansRG.ItemIndex` → mode/chip-type mapping and its AY=85/YM=13 echo constant).

## progbox.c

A minimal modal progress dialog (label + pulsing progress bar + Abort button) shown during folder-add playlist scans, pumping the GTK event loop on every pulse so the UI stays responsive and the Abort button remains clickable mid-scan. Uses a plain `GtkVBox` rather than the generator's default `GtkFixed` layout, since ProgBox isn't part of the skin and there's no coordinate-fidelity reason to keep the literal layout once hand-wired.

Ported from: ay_emul/ProgBox.pas (progress dialog shown during `PlayList.pas`'s folder-scan operations, its Abort-button cancellation).
