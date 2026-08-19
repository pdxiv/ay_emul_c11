# gui/include/gui/dialogs/

Headers for the GUI's modal/secondary dialog windows, each a C11/GTK2 port of one small standalone Pascal unit reached from the main window (About, per-item edit, jump-to-time, mixer-preset helper, folder-scan progress).

## about.h

Declares `gui_about_show(GtkWindow* parent)` - shows the About dialog as a fully skinned window of its own (a separate embedded bitmap and irregular window shape from the main window's), not a plain `GtkAboutDialog`.

Ported from: ay_emul/About.pas (`TAboutBox`), reached via `MainWin.pas`'s `ButAbout`. Ports the embedded `About.bmp` sprite sheet (baked in as gui/src/about_bitmap.c), the irregular shape (`gui_apply_about_window_shape`, ay_emul's `rgn2.inc`), the OK and logo/"Help" clickable zones, and the centered "3.0" version-string overlay. This is `MIG-0090`, superseding an earlier plain-`GtkAboutDialog` stand-in (`MIG-0068`). NOT ported: the logo/"Help" zone's real action (`About.pas`'s `FormMouseUp` calling `FrmMain.CallHelp`, which opens a Windows `.chm` help file this port doesn't have) - clicking it here shows an informational message dialog instead, matching `CallHelp`'s own graceful-failure behavior when the help file is missing.

## itemedit.h

Declares `gui_itemedit_show(GtkWindow* parent, gui_playlist_entry* e, gui_playlist_defaults* pldef)` - shows the modal playlist-item "Adjusting" dialog, pre-filled from the entry's current overrides (falling back to the extracted author/title), writing edits back into `e->overrides` on OK and refreshing the entry's display string.

Ported from: ay_emul/ItemEdit.pas (`TFrmPLIEdit`), invoked by ay_emul/PlayList.pas's `MenuItemAdjustingClick` on the selected item. This is `MIG-0088` ("Tiers 1-3, full ItemEdit") plus `MIG-0103`/`MIG-0107` (Tier 4 channel-count override and the read-only diagnostics panel). Ports all four override tiers (chip-type/channel-mode, AY/interrupt-frequency, Title/Author/Program/Tracker/Computer/Date/Comment text, mono/stereo channel count) plus Load/Save-as-Defaults. Documented simplifications: frequency overrides use one free-form Hz entry field instead of the original's named-preset combo (same underlying effect, simpler UI only), and `ItemEdit.pas`'s separate numeric Year field is folded into the free-text `date` override. Diagnostic fields with no real backing data in this port (Address, Loop) are shown as "n/a" rather than fabricated - a small, honestly-tracked gap per `MIG-0103`.

## jmptime.h

Declares `gui_jptime_show(GtkWindow* parent, gui_playback* playback)` - shows the "jump to time" modal dialog (search text prefilled with current position, track-length label, Jump/Cancel), requesting a seek on Jump. No-ops if nothing is playing, is paused, or the format has no known duration.

Ported from: ay_emul/JmpTime.pas's `JumpToTime` (MainWin.pas:3984-4023), reached via the 'J' keyboard shortcut (MainWin.pas:3547-3550). This is real wiring (`MIG-0079` ticks-to-seconds conversion, closed out) on top of the `.lfm`-generated skeleton produced by `MIG-0066`'s `tools/lfm_gen/lfm_gen.py`.

## mxhelper.h

Declares `gui_mxhelper_show(GtkWindow* parent, gui_playback* playback)` - shows the 13-way stereo channel-allocation preset picker (AY/YM x ABC/ACB/BAC/BCA/CAB/CBA Stereo, plus Mono), applying the chosen preset's amplitude/pan values and chip type on Set. Also exposes `mxhelper_calc_mode_coefs(int mode, int echo, uint8_t* al, ar, bl, br, cl, cr)`, the pure arithmetic turning a (mode, chip-type) pair into the six channel amplitude/pan values, reused by `itemedit.h`'s own channel-mode override.

Ported from: ay_emul/mxhelper.pas (`TFrmMxHlp`, itself just a bare form declaration with no logic) and the real behavior in ay_emul/Mixer.pas's `SBHelperClick`, plus ay_emul/MainWin.pas's `CalcModeCoefs` (MainWin.pas:1953-2027) for the arithmetic. NOT ported: the `TSDMAChG` check-group (ZX Turbo-Sound / STe DMA-Sound headroom options) and the PreAmp-search overflow-avoidance loop in `SBHelperClick` - both are about Atari DMA-sound/TurboSound channel headroom, a feature this port's Mixer window doesn't have at all per `MIG-0078`'s scope decision.

## progbox.h

Declares `gui_prbox` (window, indeterminate progress bar, label, `aborted` flag) and `gui_prbox_create`/`gui_prbox_pulse`/`gui_prbox_destroy`, used as the folder-scan progress dialog for the playlist window's "Add Folder" action; `gui_prbox_pulse` advances the bar and pumps pending GTK events so the window (and its Abort button) stays responsive during a synchronous directory scan.

Ported from: ay_emul/ProgBox.pas ("Searching for tunes"), real wiring on top of the `.lfm`-generated skeleton produced by `MIG-0066`'s `tools/lfm_gen/lfm_gen.py`. Only the "Abort current operation" button is wired; ay_emul/ProgBox.pas's second button ("Switch off tunes finder", a global auto-scan settings toggle) is omitted rather than drawn as a dead control, since this port has no background auto-scan feature to switch off (documented in migration_debt.yaml).
