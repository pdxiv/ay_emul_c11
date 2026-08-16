# gui/src/dialogs/

Implementation files for the GUI's secondary dialog windows.

## about.c

Implements `gui/include/gui/dialogs/about.h`: creates the About
dialog window, draws the cropped source sprite sheet, applies the
irregular window shape, and handles the OK and logo/"Help" clickable
zones.

Ported from: `About.pas`'s `TAboutBox`. The embedded bitmap
(`gui/src/about_bitmap.c`, generated from `ay_emul/About.bmp`) and the
base-image crop coordinates come directly from `About.pas:FormCreate`.

## itemedit.c

Implements `gui/include/gui/dialogs/itemedit.h`: the playlist item
"Adjusting" dialog's widgets and logic — chip-type override, channel-
mode override (preset or manual entry), and AY-chip-frequency/
interrupt-frequency override fields.

Ported from: `ItemEdit.pas`'s `TFrmPLIEdit`, with the chip-type radio
trio modeled on `Mixer.pas`'s `AY_Chip`/`YM_Chip` pair (plus an added
"none" state for an absent per-item override).

## jmptime.c

Implements `gui/include/gui/dialogs/jmptime.h`: the "jump to time"
dialog's real behavior (time parsing/formatting, seek request on
Jump).

Ported from: `MainWin.pas`'s `JumpToTime`, with `TimeSToStr`
re-implemented via `strtol`/`snprintf`. Started from a
`JmpTime.lfm`-generated skeleton (`MIG-0066`), since hand-completed
with real wiring.

## mxhelper.c

Implements `gui/include/gui/dialogs/mxhelper.h`: the 13-preset
channel-mode radio group and the `calc_mode_coefs` arithmetic that
converts a (mode, chip-type) pair into AL/AR/BL/BR/CL/CR index values.

Ported from: `MainWin.pas`'s `CalcModeCoefs`, invoked as
`mxhelper.pas`'s `TFrmMxHlp` logic — which in the original lived
entirely in `Mixer.pas`'s `SBHelperClick` handler, since `mxhelper.pas`
itself declares only an empty form.

## progbox.c

Implements `gui/include/gui/dialogs/progbox.h`: the folder-scan
progress dialog with a progress bar and Abort button.

Ported from: `ProgBox.pas`. Started from a `ProgBox.lfm`-generated
skeleton (`MIG-0066`), since hand-completed with real wiring. The
original's second button ("Switch off tunes finder") is omitted, this
port having no background auto-scan feature to toggle.
