/* C11/GTK2 port of ItemEdit.pas's TFrmPLIEdit - the playlist item
 * "Adjusting" dialog (MIG-0088, "Tiers 1-3, full ItemEdit" - the
 * user's explicit answer to porting the complete feature rather than a
 * subset; MIG-0103 then closed out the remaining gaps - see that
 * entry). Traced end-to-end from PlayList.pas: invoked by
 * MenuItemAdjustingClick (~1009) on the selected item, editing
 * PlayListItems[LastSelected]^'s real fields directly (mirrored here as
 * gui_playlist_entry::overrides), applied at actual playback time by
 * PlayItem (~623-825), each override individually gated behind its own
 * Mixer "Get from list" checkbox (see gui/include/gui/mixer_win.h).
 *
 * Ported (all four override tiers, plus the read-only diagnostics and
 * playlist-wide defaults panel):
 *  - Tier 1: chip-type override (AY/YM/none) and channel-mode override
 *    (the same 13-preset Mono/ABC/ACB/.../CBA set as Mixer's own
 *    "Presets..." helper, reusing mxhelper_calc_mode_coefs - see
 *    gui/dialogs/mxhelper.h) or a manual AL/AR/BL/BR/CL/CR entry.
 *  - Tier 2: AY-chip-frequency override and interrupt/VBL-frequency
 *    override (free-form Hz entry - ItemEdit.pas itself uses a combo of
 *    named presets, Speccy/Atari/Amstrad/Other, but since Set_Chip_Frq/
 *    Set_Player_Frq (player_set_chip_freq/player_set_player_freq,
 *    MIG-0087) just take a raw Hz value, a single numeric entry field
 *    covers every preset's actual effect without needing to hardcode
 *    the same preset list twice - a documented simplification of the
 *    UI only, not of the underlying behavior).
 *  - Tier 3: editable Title/Author/Program/Tracker/Computer/Date/
 *    Comment text overrides, taking precedence over the read-only
 *    extracted title/author wherever non-empty (see
 *    gui_playlist_overrides's own comment). ItemEdit.pas's separate
 *    numeric Year field is folded into free-text `date` here - the
 *    original UI just concatenates them for display anyway
 *    (Players.pas's FormatSpec conventions), so this loses no real
 *    information, only the original's split-field data-entry UI.
 *  - Tier 4 (MIG-0103): mono/stereo output-channel-count override
 *    (GBNChans/RBNStereo/RBNMono/RBNDef) - see gui_playlist_overrides::
 *    number_of_channels and player_set_number_of_channels. Applied at
 *    load time only (ALSA device channel count can't change mid-
 *    stream), same load-time-only pattern as Tier 2's frequency
 *    overrides - see gui/src/mainwin.c's resolve_number_of_channels/
 *    do_load_song.
 *  - Read-only diagnostic fields (GBFile: FileType/Offset/Address/
 *    Length/Time/Loop/FormatSpec/filename) - sourced from real,
 *    already-computed data (gui_playlist_entry::format/path/song_index
 *    via player_format_name, stat(2), and a throwaway player_load_song
 *    for Time - see itemedit.c's load_diag_player) wherever this port
 *    actually has the underlying data; fields with no real backing data
 *    (Address, Loop - see itemedit.c's own comment) are shown as "n/a"
 *    rather than fabricated, a small honestly-tracked gap (MIG-0103).
 *  - "Load Defaults"/"Save as Defaults" buttons (GBDef/BDefLoad/
 *    BDefSave) - read/write the playlist-wide PLDef_* fallback
 *    defaults (gui_playlist_defaults, gui_playlist_win::defaults),
 *    session-lifetime only (no .ayl persistence - see gui_playlist_
 *    defaults's own comment).
 */
#ifndef GUI_DIALOGS_ITEMEDIT_H
#define GUI_DIALOGS_ITEMEDIT_H

#include <gtk/gtk.h>

#include "gui/playlist.h"

/* Shows a modal dialog pre-filled from `e->overrides` (falling back to
 * `e->author`/`e->title` for the Author/Title fields when no override
 * is set yet, matching ItemEdit.pas's own "show the current effective
 * value" prefill). On OK, writes the edited values back into
 * `e->overrides` and calls gui_playlist_entry_refresh_display(e) so the
 * playlist view reflects a Title/Author edit immediately. On Cancel,
 * `e` is left untouched.
 *
 * `pldef` is the owning gui_playlist_win's playlist-wide PLDef_*
 * fallback-default state (see gui_playlist_defaults) - "Load Defaults"
 * reads it into the dialog's controls, "Save as Defaults" writes the
 * dialog's current control values into it (then immediately reloads,
 * matching BDefSaveClick's own `BDefLoadClick(Sender)` tail call).
 * Mutated in place if the user clicks either button, independent of
 * whether the dialog is ultimately OK'd or Cancelled (matching the
 * original: BDefSaveClick isn't gated behind the dialog's own OK). */
void gui_itemedit_show(GtkWindow* parent, gui_playlist_entry* e,
                        gui_playlist_defaults* pldef);

#endif /* GUI_DIALOGS_ITEMEDIT_H */
