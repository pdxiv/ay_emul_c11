/* Real wiring for the JmpTime.lfm-generated skeleton (MIG-0066) - see
 * jmptime.c for what changed. MainWin.pas: JumpToTime (3984-4023),
 * reached via the 'J' keyboard shortcut (MainWin.pas:3547-3550).
 */
#ifndef GUI_DIALOGS_JMPTIME_H
#define GUI_DIALOGS_JMPTIME_H

#include <gtk/gtk.h>

#include "gui/playback.h"

/* No-ops (matching JumpToTime's own "if not IsPlaying then exit; if
 * Paused then exit" early-exits) if `playback` isn't loaded, isn't
 * actively playing, is paused, or the loaded format has no known
 * duration to jump within (gui_playback_duration_seconds <= 0 - see
 * player_get_tick_position's own comment for the full list of formats
 * this covers: AY/YM/VTX/SNDH/PT3 plus PT1/PT2/GTR/FLS/STC/STP/FXM/PSM/
 * ASC/ASC0/FTC/PSC/SQT). Otherwise shows a modal dialog (search text
 * prefilled with the current position, "Track length: M:SS" label,
 * Jump/Cancel) and, on Jump with a validly parsed time, requests a seek
 * to it. */
void gui_jptime_show(GtkWindow* parent, gui_playback* playback);

#endif /* GUI_DIALOGS_JMPTIME_H */
