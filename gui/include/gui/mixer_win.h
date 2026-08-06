/* C11/GTK2 port of Mixer.pas's TFrmMixer - scoped to only the controls
 * that affect actual audio output (see PHASE5_GUI_PROGRESS.md's own
 * note on why): the six per-channel pan/level trackbars (TBAmpAL/AR/
 * BL/BR/CL/CR) and the AY/YM chip-type radio buttons (RBChTypeAY/
 * RBChTypeYM). Everything else in the real Mixer.pas - BASS/proxy/
 * network settings, Atari MFP/DMA/interrupt-frequency overrides,
 * chip-frequency overrides, digidrum amplitude, filtering - is NOT
 * ported (see migration_debt.yaml): out of this port's scope (BASS/
 * proxy/network), Atari/SNDH-specific and SNDH itself still has no
 * audible output yet (MIG-0021), or would need additional per-format
 * engine plumbing beyond the generic player_ay_engine() accessor this
 * entry adds.
 *
 * Built with idiomatic GTK2 widgets (GtkVScale/GtkRadioButton in a
 * GtkHBox/GtkVBox layout), not generated from Mixer.lfm - same
 * rationale as gui/src/playlist_win.c (Mixer.pas isn't skin-rendered,
 * and Mixer.lfm's generated skeleton is far too large - 1,592 lines -
 * to use as a hand-completion starting point, see MIG-0078).
 *
 * Like gui/src/playlist_win.c, this window is created once and
 * persists for the process lifetime; "closing" it (the window-manager
 * X, or ButMixer again) just hides it.
 */
#ifndef GUI_MIXER_WIN_H
#define GUI_MIXER_WIN_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "gui/playback.h"

typedef struct gui_mixer_win {
  GtkWidget* window;
  gui_playback* playback; /* not owned - the caller's gui_mainwin::
                            * playback; controls are no-ops (silently,
                            * matching a disabled control rather than
                            * erroring) whenever playback->loaded is
                            * false */
  GtkWidget* scale_al, *scale_ar, *scale_bl, *scale_br, *scale_cl, *scale_cr;
  GtkWidget* rb_ay, *rb_ym;

  /* Mixer.pas: CBChTypeLst/CheckBox1/CBChFrqLst/CBIntFrqLst - "use the
   * playlist item's own override" checkboxes gating ItemEdit.pas's
   * per-item chip-type/channel-mode/AY-freq/interrupt-freq overrides
   * (MIG-0088). Read directly by gui/src/mainwin.c's do_load_song when
   * starting playback of a playlist entry - CBChLst (channel count) has
   * no C11 equivalent checkbox since this port's output is always fixed
   * stereo16 (see gui_playlist_overrides's own comment). */
  GtkWidget* cb_use_chip_type_list;
  GtkWidget* cb_use_channel_mode_list;
  GtkWidget* cb_use_ay_freq_list;
  GtkWidget* cb_use_int_freq_list;
  bool syncing; /* true while on_sync_timer is programmatically setting
                  * widget values - lets the change handlers skip
                  * apply_and_recalc for those calls without needing
                  * GLib's g_signal_handlers_block_by_func (which trips
                  * -Wpedantic: casting a function pointer to gpointer is
                  * non-conforming strict ISO C, even though GTK2/GLib
                  * itself relies on it internally) */
  guint sync_timer_id; /* periodically re-syncs the sliders/radio
                         * buttons to the currently-loaded file's actual
                         * values (each new file load resets ay_engine
                         * to its own defaults - see gui_mainwin.c's
                         * do_load_song - so the Mixer window's controls
                         * would otherwise silently go stale after
                         * Open/Next/Prev) */
} gui_mixer_win;

/* `playback` must outlive the mixer window (same lifetime as
 * gui_mainwin's own `playback` field - see gui/include/gui/mainwin.h). */
void gui_mixer_win_create(gui_mixer_win* w, GtkWindow* parent,
                           gui_playback* playback);
void gui_mixer_win_toggle_visible(gui_mixer_win* w);
void gui_mixer_win_destroy(gui_mixer_win* w);

/* Convenience accessors for gui/src/mainwin.c's do_load_song, so it
 * doesn't need to reach into GtkToggleButton internals directly. */
bool gui_mixer_win_use_chip_type_list(const gui_mixer_win* w);
bool gui_mixer_win_use_channel_mode_list(const gui_mixer_win* w);
bool gui_mixer_win_use_ay_freq_list(const gui_mixer_win* w);
bool gui_mixer_win_use_int_freq_list(const gui_mixer_win* w);

#endif /* GUI_MIXER_WIN_H */
