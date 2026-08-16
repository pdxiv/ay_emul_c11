# gui/include/gui/dialogs/

Public headers for the GUI's secondary dialog windows.

## about.h

Declares the About dialog: a fully skinned window using its own
embedded bitmap and irregular window shape, with OK and logo/"Help"
clickable zones.

Ported from: `About.pas`'s `TAboutBox`, opened by `MainWin.pas`'s
`ButAbout`. Supersedes an earlier plain-`GtkAboutDialog` stand-in
(documented as `MIG-0068`, replaced by `MIG-0090`).

## itemedit.h

Declares the playlist item "Adjusting" dialog: per-item chip-type
override, channel-mode override (13-preset or manual AL/AR/BL/BR/CL/CR
entry), and AY-chip-frequency/interrupt-frequency overrides.

Ported from: `ItemEdit.pas`'s `TFrmPLIEdit`, traced end-to-end from
`PlayList.pas` (`MenuItemAdjustingClick`, `PlayItem`). All three
approved tiers ("Tiers 1-3, full ItemEdit") are ported per
`migration_debt.yaml`'s `MIG-0088`.

## jmptime.h

Declares `gui_jptime_show`, the "jump to time" modal dialog (search
text prefilled with current position, track-length label, Jump/Cancel).

Ported from: `MainWin.pas`'s `JumpToTime` (reached via the 'J'
keyboard shortcut), built starting from a `JmpTime.lfm`-generated
skeleton (`MIG-0066`) and then hand-wired with real behavior.

## mxhelper.h

Declares the stereo channel-mode preset picker dialog, launched from
the Mixer window's "Presets..." button.

Ported from: `mxhelper.pas`'s `TFrmMxHlp` (an empty form declaration
in the original) together with the actual logic, which lived in
`Mixer.pas`'s `SBHelperClick`, and `MainWin.pas`'s `CalcModeCoefs`. The
`TSDMAChG` Turbosound/DMA-headroom options and PreAmp-search loop are
not ported (out of this port's Mixer scope).

## progbox.h

Declares `gui_prbox`, the folder-scan progress dialog used by the
playlist window's "Add Folder" button, with an Abort button.

Ported from: `ProgBox.pas`, built starting from a `ProgBox.lfm`-
generated skeleton and then hand-wired. The original's second button
("Switch off tunes finder", a background auto-scan toggle) is omitted
since this port has no background auto-scan feature.
