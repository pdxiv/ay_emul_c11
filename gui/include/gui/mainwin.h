/* C11/GTK2 port of MainWin.pas's TFrmMain - Phase 5, updated as work
 * lands. See gui/src/mainwin.c's file comment for exactly what's wired
 * vs deferred, and PHASE5_GUI_PROGRESS.md for the authoritative status.
 */
#ifndef GUI_MAINWIN_H
#define GUI_MAINWIN_H

#include <gtk/gtk.h>

#include "gui/alsa_mixer.h"
#include "gui/mixer_win.h"
#include "gui/playback.h"
#include "gui/playlist_win.h"
#include "gui/skin.h"
#include "gui/ticker.h"
#include "gui/time_display.h"
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
  gui_button* pressed_button; /* MainWin.pas: TButtZone's per-frame
                                * Push/UnPush during a drag (FormMouseMove:
                                * "if Touche(X,Y) then Push else UnPush") -
                                * the button currently held down by the
                                * mouse, if any (NULL otherwise). Kept
                                * pushed-looking (and only fires its
                                * action on release) while the cursor
                                * stays over it; moving off before
                                * releasing un-pushes it and cancels the
                                * click, matching the original's own
                                * press-and-drag-off-to-cancel affordance
                                * - a real gap in this port until this
                                * field was added (every button previously
                                * fired unconditionally on release,
                                * regardless of where the mouse ended up,
                                * and never visually un-pushed either). */
  bool do_loop; /* ButLoop toggle - MainWin.pas: Do_Loop, restarts the
                  * same (sub)song from the top on natural end instead
                  * of stopping, see on_timer */
  guint timer_id;

  gui_time_display time_disp; /* MainWin.pas: SensTime/BMP_Time/RedrawTime */

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

  gui_ticker ticker;    /* MainWin.pas: the scroll-text ticker - see
                          * gui/include/gui/ticker.h (MIG-0098) */
  int ticker_last_index; /* the gui_playlist_win model index the ticker
                           * last targeted - MainWin.pas: Item_Displayed,
                           * compared against the newly-loaded entry's
                           * own index each do_load_song to decide
                           * whether this is a Next/Prev-adjacent step
                           * (animates) or an arbitrary jump (snaps) -
                           * see ticker.h's own file comment. -1 = none
                           * yet. */
  bool ticker_dragging; /* MainWin.pas: MoveScr.Clicked */
  int ticker_drag_last_x; /* MainWin.pas: MoveScr.OldX - app-local x of
                            * the last motion event during a ticker
                            * drag, so on_motion can compute PosX
                            * (the delta gui_ticker_drag expects)
                            * itself */

  int scale; /* MainWin.pas: Scale (1 or 2) - Tools.pas's CBDoubleSz,
              * MIG-0119. The skin bitmap/every button-LED-slider rect/
              * the window-shape region table all stay in their own
              * native 1x coordinate space always (matching the
              * original: it never pre-scales the bitmap or any rect
              * table either) - drawing is scaled via cairo_scale in
              * on_expose, the window/drawing-area's own real pixel
              * size and window-shape region are multiplied by `scale`
              * in gui_mainwin_set_scale, and incoming mouse coordinates
              * are divided by `scale` before hit-testing (see
              * on_button_press/on_motion). */

  /* MainWin.pas: MoveVol IS the system mixer control in the original
   * (VolumeCtrl := MoveVol.PosX; SetSysVolume - MainWin.pas:2725-2726) -
   * gui_playback_set_volume's own in-app PCM-sample scaling (above,
   * already wired to this exact same vol_slider) was itself a documented
   * scope-narrowing substitute for this real ALSA hardware-mixer control,
   * not a faithful port of MoveVol's own role. NULL if no usable ALSA
   * mixer control could be opened (see gui_alsa_mixer_open) - every
   * sysvol_* field/vol_slider handler below degrades gracefully to
   * software-only scaling in that case, same as before this feature
   * existed. See gui/include/gui/alsa_mixer.h's own file comment for the
   * full ALSA-API/platform-substitution rationale. */
  gui_alsa_mixer* sysvol;
  bool sysvol_linear;  /* Mixer.pas: CBLnScale/VolLinear - see gui_alsa_
                         * mixer_set_volume's own comment for the exact
                         * curve this toggles. */
  bool auto_save_volume_pos; /* Mixer.pas: CBSvVolPos/AutoSaveVolumePos -
                               * whether gui_mainwin_save_settings persists
                               * the current vol_slider position/selected
                               * control across sessions. */

  /* Tools.pas: CheckBox40/AutoSaveWindowsPos - gates whether
   * gui_mainwin_save_settings persists MainX/MainY/ListX/ListY/ListW/
   * ListH/ListVis/MixerX/MixerY/ToolsX/ToolsY (MainWin.pas:4355-4374).
   * Loaded from settings at startup (gui_mainwin_create), applied via
   * gtk_window_move BEFORE each window's first gtk_widget_show_all -
   * see gui_mainwin_create's own comment for why order matters here. */
  bool auto_save_windows_pos;

  /* MainWin.pas: TrayMode (0=never,1=minimize,2=always) - Tools.pas's
   * "Icon on system tray" radio group (RadioButton8/9/10). See
   * gui_mainwin_set_tray_mode's own comment for what's wired vs. the
   * documented Windows-only gap (mode 1). */
  int tray_mode;

  /* Mixer.pas: GBSRate/GBChans/GBBRate/GBBuffs/GBDevice ("Digital
   * Sound" tab, WOSheet - MIG-0130). All LOAD-TIME-ONLY (see player_
   * set_sample_bits/player_set_number_of_channels's own comments on
   * why - matches real Pascal's own `if IsPlaying/digsoundthread_
   * active then exit` guards on every one of these setters), read by
   * gui/src/mainwin.c's do_load_song at the same point channels/
   * is_ste already are. Session-wide (not per-playlist-item) state,
   * living here rather than in gui_playback itself for the same
   * reason sysvol/sysvol_linear do - gui_mixer_win's own new "Digital
   * Sound" tab (gui/src/mixer_win.c) is the UI for these, reached via
   * its own `mw` pointer (MIG-0129's own precedent). */
  int sample_rate;      /* settings.pas: SampleRate (SampleRateDef=48000) */
  int sample_bits;      /* settings.pas: SampleBit (SampleBitDef=16) */
  int default_channels; /* settings.pas: NumberOfChannels (NumOfChanDef=2) -
                          * GBChans' own RBChStereo/RBChMono - the REAL
                          * final fallback resolve_number_of_channels
                          * (mainwin.c) uses when CBChLst is unchecked
                          * and no PLDef default is set either, matching
                          * PlayList.pas:748-759's own "leave Number
                          * OfChannels as whatever it already is"
                          * behavior (previously a hardcoded literal 2
                          * here, see this session's own MIG-0130 fix). */
  char output_device[256]; /* digsoundcode.pas: digsoundDevice (an
                             * index into a cached device list there) -
                             * this port instead stores the device NAME
                             * directly (empty = ALSA's own "default"),
                             * avoiding a stale-index problem if the
                             * device list changes between sessions -
                             * see alsa_output.h's own enumerate/open
                             * doc comments. */
  int buf_len_ms;  /* settings.pas: BufLen_ms (BufLen_msDef=200) - ONE
                     * period's length in ms, see alsa_output_open's own
                     * doc comment for the real buffer-time/period-time
                     * split this feeds. */
  int num_buffers; /* settings.pas: NumberOfBuffers (NumberOfBuffersDef=3) */
} gui_mainwin;

/* Tools.pas: EVisPeriod's EditingDone -> FrmMain.SetVisTimerPeriod
 * (MainWin.pas:1714-1721: `if (VTP > 9) and (VTP < 101)`) - exposed
 * here (rather than a plain field write) since changing the period
 * means tearing down and re-creating the actual GLib timeout source,
 * which only mainwin.c (owner of the static on_vis_timer callback)
 * can do. Out-of-range values (outside 10-100ms) are silently ignored,
 * exactly matching SetVisTimerPeriod's own guard. */
void gui_mainwin_set_vis_period(gui_mainwin* mw, int period_ms);

/* Tools.pas: CBDoubleSzClick (MIG-0119) - `NewS := Ord(CBDoubleSz.
 * Checked) + 1; if NewS <> Scale then begin Scale := NewS; FrmMain.
 * RecreateRgn; end;`. Resizes the actual window/drawing-area to
 * MW_WIDTH*scale x MW_HEIGHT*scale, reapplies the window-shape region
 * at the new scale, and queues a redraw (drawing itself picks up the
 * new mw->scale via cairo_scale in on_expose). A no-op if `scale`
 * already equals mw->scale (matching the original's own `if NewS <>
 * Scale` guard) or isn't 1 or 2. */
void gui_mainwin_set_scale(gui_mainwin* mw, int scale);

/* Builds and shows the window; does not start the GTK main loop (the
 * caller, gui/src/main.c, does that). Returns false if the default skin
 * fails to load - fatal for this milestone, since there's no fallback
 * rendering path yet. */
bool gui_mainwin_create(gui_mainwin* mw);

void gui_mainwin_destroy(gui_mainwin* mw);

/* Mixer.pas: BVolCtrlSelectClick/BVolCtrlDetectClick, adapted per this
 * session's own platform-substitution note (see gui/include/gui/
 * alsa_mixer.h) - closes mw->sysvol if open, opens `selem_name` (or
 * auto-detects if NULL/"") via gui_alsa_mixer_open, and on success reads
 * the NEW control's own current hardware level back into vol_slider.value
 * (MainWin.pas: OpenMixer -> GetSysVolume -> RedrawVolume, mixer.pas
 * OpenMixer's own comment - adopts the newly-selected control's existing
 * level rather than force-pushing the old slider position onto it).
 * gui/src/tools_win.c's Volume section calls this for BVolCtrlSelect (an
 * explicit name from its combo box) and BVolCtrlDetect (NULL). Also
 * called once from gui_mainwin_create itself to open the startup/saved
 * control. Leaves mw->sysvol NULL (not fatal) if no usable ALSA mixer
 * control exists at all - see this header's own sysvol field comment. */
void gui_mainwin_sysvol_reopen(gui_mainwin* mw, const char* selem_name);

/* Mixer.pas: CBLnScaleClick - `VolLinear := checked; GetSysVolume;`.
 * Updates mw->sysvol_linear and, if a control is open, re-reads its
 * current hardware level under the NEW curve into vol_slider.value
 * (the same physical position maps to a different slider fraction under
 * linear vs. logarithmic - see gui_alsa_mixer_set_volume's own comment
 * for the exact curve) and queues a redraw. */
void gui_mainwin_set_sysvol_linear(gui_mainwin* mw, bool linear);

/* Tools.pas: Set_TrayMode2 (MainWin.pas:4985-5012), narrowed to this
 * port's own already-shipped tray icon (MIG-0073 - GtkStatusIcon, no
 * separate taskbar-button concept to toggle alongside it the way the
 * real AddTaskbarButton/RemoveTaskbarButton pair does):
 *   0 (never):    tray icon hidden/never shown.
 *   1 (minimize): real behavior is Windows-only (MainWin.pas:3100's
 *                 `{$IFDEF Windows} ShowWindow(GetParent(FrmMain.Handle),
 *                 SW_HIDE)` toolwindow trick) - no Linux/GTK equivalent
 *                 exists to port, so this mode behaves IDENTICALLY to
 *                 "always" here (documented, not silently dropped - see
 *                 migration_debt.yaml). Tools.pas's own radio option is
 *                 still shown (not greyed out) for layout fidelity,
 *                 since selecting it has a real, just simplified, effect.
 *                 2 (always):   tray icon always shown - this port's own
 *                 pre-existing MIG-0073 default behavior.
 * No-op if `mode` is already the current mode or out of [0,2] range,
 * matching Set_TrayMode2's own `if (TrayMode = TM) or (DWORD(TM) > 2)
 * then Exit` guard. */
void gui_mainwin_set_tray_mode(gui_mainwin* mw, int mode);

/* Tools.pas: SaveParams's window-geometry/tray-mode/volume subset (Part
 * B/C of this session) - writes every setting gui_mainwin_create loads
 * back to disk via gui/include/gui/settings.h. Called once at the real
 * shutdown path (gui/src/main.c, right before gui_mainwin_destroy, while
 * every window is still alive/realized so gtk_window_get_position/
 * get_size are meaningful) - NOT from gui_mainwin_destroy itself, since
 * by the time that runs via the "destroy" signal -> gtk_main_quit chain
 * the window may already be torn down (see this function's own gui/src/
 * mainwin.c definition comment for the exact ordering hazard this
 * avoids). */
void gui_mainwin_save_settings(gui_mainwin* mw);

/* Mixer.pas: SBStopClick -> StopAndFreeAll (MIG-0130) - the exact same
 * stop path MainWin.pas's own ButStopClick uses (both just call
 * StopAndFreeAll there too); exposed so gui/src/mixer_win.c's own
 * "Digital Sound" tab Stop button can reuse it instead of duplicating
 * the `if (mw->file_loaded) gui_playback_stop(...)` guard. */
void gui_mainwin_stop(gui_mainwin* mw);

#endif /* GUI_MAINWIN_H */
