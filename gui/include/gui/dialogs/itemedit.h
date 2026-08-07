/* C11/GTK2 port of ItemEdit.pas's TFrmPLIEdit - the playlist item
 * "Adjusting" dialog (MIG-0088, "Tiers 1-3, full ItemEdit" - the
 * user's explicit answer to porting the complete feature rather than a
 * subset). Traced end-to-end from PlayList.pas: invoked by
 * MenuItemAdjustingClick (~1009) on the selected item, editing
 * PlayListItems[LastSelected]^'s real fields directly (mirrored here as
 * gui_playlist_entry::overrides), applied at actual playback time by
 * PlayItem (~623-825), each override individually gated behind its own
 * Mixer "Get from list" checkbox (see gui/include/gui/mixer_win.h).
 *
 * Ported (all three approved tiers):
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
 *
 * NOT ported (see ItemEdit.pas's own lower half, and
 * migration_debt.yaml): the read-only FileType/Offset/Length/Address/
 * Time/Loop/FormatSpec debug/diagnostic display fields - these were not
 * part of the approved Tier 1-3 scope (they show already-derived,
 * already-correct engine state for developer debugging in the original,
 * not persisted overrides) and channel-count override (CBChLst's
 * subject) - this port's output is always fixed stereo16, see
 * gui_playlist_overrides's own comment.
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
 * `e` is left untouched. */
void gui_itemedit_show(GtkWindow* parent, gui_playlist_entry* e);

#endif /* GUI_DIALOGS_ITEMEDIT_H */
