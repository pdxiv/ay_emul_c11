/* C11/GTK2 port of MainWin.pas's TFrmMain - Phase 5, updated as work
 * lands. See gui/src/mainwin.c's file comment for exactly what's wired
 * vs deferred, and PHASE5_GUI_PROGRESS.md for the authoritative status.
 */
#ifndef GUI_MAINWIN_H
#define GUI_MAINWIN_H

#include <gtk/gtk.h>

#include "gui/mixer_win.h"
#include "gui/playback.h"
#include "gui/playlist_win.h"
#include "gui/skin.h"
#include "gui/tools_win.h"
#include "gui/visualizer.h"
#include "gui/zones.h"

typedef struct gui_mainwin {
  GtkWidget* window;
  GtkWidget* area; /* the single drawing-area covering the whole window */
  gui_skin skin;

  gui_playback playback;
  bool file_loaded;
  gui_playlist_overrides current_overrides; /* ItemEdit.pas overrides for
                                              * the currently-loaded
                                              * playlist entry, if any
                                              * (MIG-0088) - reapplied on
                                              * ButLoop same-song restart
                                              * (do_load_song's `overrides
                                              * == NULL` case), since a
                                              * fresh gui_playback_load_
                                              * song resets ay_engine to
                                              * its own per-file defaults
                                              * (same reason gui_mixer_win
                                              * re-syncs on a timer). */
  gui_playlist_win plwin; /* MainWin.pas: FrmPLst - see MIG-0071 */
  gui_mixer_win mixerwin; /* MainWin.pas: FrmMixer - see MIG-0078 */
  gui_tools_win toolswin; /* MainWin.pas: FrmTools - see MIG-0095 */

  gui_button but_play, but_pause, but_stop, but_open, but_min, but_close;
  gui_button but_prev, but_next, but_loop, but_about, but_list, but_mixer;
  gui_button but_tools;
  gui_led led_ay, led_ym, led_stereo;
  gui_hslider vol_slider;
  gui_hslider progr_slider; /* draggable (real seeking, MIG-0079) only
                              * when gui_playback_get_progress_fraction
                              * succeeds (AY/YM/VTX - see its own
                              * comment); a cosmetic sweep otherwise,
                              * same as before this entry - see on_timer */

  int drag_x, drag_y, drag_w, drag_h; /* MoveWin equivalent - titlebar
                                        * drag zone, handled directly via
                                        * gdk_window_begin_move_drag
                                        * rather than a full gui_hslider/
                                        * button (it's neither) */

  bool dragging_vol;
  bool dragging_progr;
  bool do_loop; /* ButLoop toggle - MainWin.pas: Do_Loop, restarts the
                  * same (sub)song from the top on natural end instead
                  * of stopping, see on_timer */
  guint timer_id;

  gui_visualizer vis; /* MainWin.pas: SensSpa/SensAmp - see MIG-0094 */
  guint vis_timer_id;  /* MainWin.pas: VisTimer, VisTimerPeriod=30ms - a
                         * separate timer from timer_id above (matching
                         * the original's own separate VisTimer object),
                         * not folded into the existing 200ms timer_id
                         * tick so that cadence stays exactly as it was
                         * before this entry. */

  GtkStatusIcon* tray_icon; /* MainWin.pas: TrayIcon1 - see MIG-0073 for
                              * what's simplified */
  bool minimized;           /* tracks iconify state for the tray icon's
                              * click-to-toggle behavior (MainWin.pas:
                              * TrayIcon1MouseUp) - see on_tray_activate */

  /* Tools.pas subset (MIG-0095) - see gui/include/gui/tools_win.h's
   * own file comment for the full trace of what's ported vs not.
   * default_dir empty means "no override", matching
   * FrmMain.DefaultDirectory's own initial empty-string state. */
  char default_dir[1024]; /* Tools.pas: EMFolder */
  int vis_period_ms;       /* Tools.pas: EVisPeriod/VisTimerPeriod */
} gui_mainwin;

/* Tools.pas: EVisPeriod's EditingDone -> FrmMain.SetVisTimerPeriod
 * (MainWin.pas:1714-1721: `if (VTP > 9) and (VTP < 101)`) - exposed
 * here (rather than a plain field write) since changing the period
 * means tearing down and re-creating the actual GLib timeout source,
 * which only mainwin.c (owner of the static on_vis_timer callback)
 * can do. Out-of-range values (outside 10-100ms) are silently ignored,
 * exactly matching SetVisTimerPeriod's own guard. */
void gui_mainwin_set_vis_period(gui_mainwin* mw, int period_ms);

/* Builds and shows the window; does not start the GTK main loop (the
 * caller, gui/src/main.c, does that). Returns false if the default skin
 * fails to load - fatal for this milestone, since there's no fallback
 * rendering path yet. */
bool gui_mainwin_create(gui_mainwin* mw);

void gui_mainwin_destroy(gui_mainwin* mw);

#endif /* GUI_MAINWIN_H */
