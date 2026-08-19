/* C11/GTK2/Cairo port of MainWin.pas's numeric elapsed-time readout
 * (RedrawTime/BMP_Time, MainWin.pas:570-598) and its click-to-cycle
 * zone (SensTime/ButTimeClick, MainWin.pas:3329-3330/2596-2601) - the
 * small skin display box showing the current song's elapsed, remaining,
 * or total time as M:SS/H:MM:SS text, toggled by clicking the box
 * itself or pressing 'T'.
 *
 * Unlike the original (which redraws only on an actual second-boundary
 * change, tracked via TimeShown/ClearTimeInd, to avoid unnecessary
 * blitting), this port's on_expose already redraws the whole skin fresh
 * every frame - so gui_time_display_draw just recomputes and draws the
 * correct text (or nothing) every call, with no separate dirty-tracking
 * state needed.
 */
#ifndef GUI_TIME_DISPLAY_H
#define GUI_TIME_DISPLAY_H

#include <cairo.h>
#include <stdbool.h>

#include "gui/playback.h"

/* MainWin.pas:67-70. */
#define GUI_TIME_X 24
#define GUI_TIME_Y 65
#define GUI_TIME_WIDTH (93 - 24)
#define GUI_TIME_HEIGHT (20 + 4)

typedef struct gui_time_display {
  /* MainWin.pas: TimeMode - 0=elapsed (CurrTime_Rasch), 1=remaining
   * (Time_ms - CurrTime_Rasch, shown with a leading '-'), 2=total
   * (Time_ms). Cycled 0->1->2->0 by gui_time_display_click. */
  int mode;
} gui_time_display;

void gui_time_display_init(gui_time_display* td);

/* MainWin.pas: ButTimeClick - `Inc(TimeMode); if TimeMode > 2 then
 * TimeMode := 0;`. */
void gui_time_display_click(gui_time_display* td);

/* SensTime's own hit region (MainWin.pas:3329-3330) - checked directly
 * by the caller's mouse-press hit-test chain (mirroring how
 * gui_visualizer_handle_click's SensSpa/SensAmp zones are checked). */
bool gui_time_display_handle_click(gui_time_display* td, int mx, int my);

/* Draws the current time text (or nothing, if no file is loaded or the
 * loaded format has no known duration - MainWin.pas's own `if Time_ms
 * <> 0` gate, matching ClearTimeInd's effect of leaving the box blank)
 * directly onto the already-drawn base skin at GUI_TIME_X/Y - no
 * separate background-then-text compositing needed here, unlike the
 * original's own BMP_Time double-buffer, since this port's on_expose
 * already redraws the whole base skin under it every frame. */
void gui_time_display_draw(const gui_time_display* td, cairo_t* cr,
                            const gui_playback* pb);

#endif /* GUI_TIME_DISPLAY_H */
