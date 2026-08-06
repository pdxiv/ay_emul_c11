/* C11/GTK2 port of MainWin.pas's TFrmMain - Phase 5, updated as work
 * lands (see PHASE5_GUI_PROGRESS.md for the current feature-by-feature
 * status, not this comment - it goes stale, the tracker doesn't).
 *
 * The progress slider (MoveProgr) is draggable/seekable (real position,
 * not a cosmetic sweep) only for AY/YM/VTX - see MIG-0079 and
 * gui_playback_get_progress_fraction's own comment for exactly why only
 * those three; every other format keeps the pre-MIG-0079 cosmetic
 * repeating sweep, since it has no declared song length to seek within.
 *
 * Explicitly DEFERRED, not silently dropped (see migration_debt.yaml
 * and PHASE5_GUI_PROGRESS.md for the authoritative, up-to-date list):
 * visualizer (spectrum/oscilloscope - TSensZone entirely unported,
 * blocked on BASS/FFT being out of this port's scope), ButTools (its
 * destination window, Tools.pas, doesn't exist - MIG-0069), seeking/
 * duration for the 14 formats without a declared song length.
 */
#include "gui/mainwin.h"

#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gui/playlist_win.h"
#include "gui/regions.h"
#include "ay_engine/player.h"
#include "about.h"
#include "jmptime.h"
#include "mxhelper.h"

/* MainWin.pas:40-41. */
#define MW_WIDTH 358
#define MW_HEIGHT 123

static void init_zones(gui_mainwin* mw) {
  /* Coordinates transcribed directly from MainWin.pas:3787-3820's
   * SetMainBmp (TButtZone.Create(ps, x, y, w, h, Bitmap, x1, y1, x2, y2,
   * ...) - (x,y,w,h) is the on-screen rect, (x1,y1) the "normal" source
   * rect top-left, (x2,y2) the "pushed" source rect top-left, both
   * within the same skin bitmap the base image itself comes from). */
  mw->but_play = (gui_button){.x = 119, .y = 96, .w = 35, .h = 27,
                               .src_normal_x = 119, .src_normal_y = 96,
                               .src_pushed_x = 119, .src_pushed_y = 122};
  mw->but_pause = (gui_button){.x = 158, .y = 96, .w = 35, .h = 27,
                                .src_normal_x = 158, .src_normal_y = 96,
                                .src_pushed_x = 158, .src_pushed_y = 122};
  mw->but_stop = (gui_button){.x = 197, .y = 96, .w = 35, .h = 27,
                               .src_normal_x = 197, .src_normal_y = 96,
                               .src_pushed_x = 197, .src_pushed_y = 122};
  mw->but_open = (gui_button){.x = 275, .y = 96, .w = 35, .h = 27,
                               .src_normal_x = 275, .src_normal_y = 96,
                               .src_pushed_x = 275, .src_pushed_y = 122};
  mw->but_min = (gui_button){.x = 282, .y = 6, .w = 16, .h = 16,
                              .src_normal_x = 282, .src_normal_y = 6,
                              .src_pushed_x = 0, .src_pushed_y = 0};
  mw->but_close = (gui_button){.x = 304, .y = 6, .w = 16, .h = 16,
                                .src_normal_x = 304, .src_normal_y = 6,
                                .src_pushed_x = 358 - 16, .src_pushed_y = 0};
  mw->but_prev = (gui_button){.x = 80, .y = 96, .w = 35, .h = 27,
                               .src_normal_x = 80, .src_normal_y = 96,
                               .src_pushed_x = 80, .src_pushed_y = 122};
  mw->but_next = (gui_button){.x = 235, .y = 96, .w = 35, .h = 27,
                               .src_normal_x = 235, .src_normal_y = 96,
                               .src_pushed_x = 235, .src_pushed_y = 122};
  mw->but_loop = (gui_button){.x = 52, .y = 100, .w = 21, .h = 21,
                               .src_normal_x = 52, .src_normal_y = 100,
                               .src_pushed_x = 337, .src_pushed_y = 103};
  mw->but_about = (gui_button){.x = 258, .y = 84, .w = 49, .h = 8,
                                .src_normal_x = 258, .src_normal_y = 84,
                                .src_pushed_x = 0, .src_pushed_y = 115};
  mw->but_list = (gui_button){.x = 310, .y = 77, .w = 26, .h = 26,
                               .src_normal_x = 310, .src_normal_y = 77,
                               .src_pushed_x = 26, .src_pushed_y = 124};
  mw->but_mixer = (gui_button){.x = 318, .y = 21, .w = 26, .h = 26,
                                .src_normal_x = 318, .src_normal_y = 21,
                                .src_pushed_x = 52, .src_pushed_y = 124};
  /* MainWin.pas:3805-3806. */
  mw->but_tools = (gui_button){.x = 322, .y = 50, .w = 26, .h = 26,
                                .src_normal_x = 322, .src_normal_y = 50,
                                .src_pushed_x = 0, .src_pushed_y = 124};

  /* Led_AY/Led_YM/Led_Stereo, MainWin.pas:3815-3820. */
  mw->led_ay = (gui_led){.x = 99, .y = 26, .w = 144 - 99, .h = 33 - 26,
                          .src_off_x = 99, .src_off_y = 26,
                          .src_on_x = 358 - (144 - 99) - 1,
                          .src_on_y = 150 - (33 - 26) - 1};
  mw->led_ym = (gui_led){.x = 144, .y = 26, .w = 190 - 144, .h = 33 - 26,
                          .src_off_x = 144, .src_off_y = 26,
                          .src_on_x = 358 - (190 - 144) - 1,
                          .src_on_y = 150 - (33 - 26) * 2 - 2};
  mw->led_stereo = (gui_led){.x = 190, .y = 26, .w = 234 - 190, .h = 33 - 26,
                              .src_off_x = 190, .src_off_y = 26,
                              .src_on_x = 358 - (234 - 190) - 1,
                              .src_on_y = 150 - (33 - 26) * 3 - 3};
  mw->led_stereo.state = true; /* output is always stereo16 - see file
                                 * comment */

  /* MoveVol/MoveProgr, MainWin.pas:3333-3334. */
  mw->vol_slider = (gui_hslider){.x = 237, .y = 22, .w = 70, .h = 12,
                                  .value = 1.0};
  mw->progr_slider = (gui_hslider){.x = 96, .y = 83, .w = 159, .h = 10,
                                    .value = 0.0};

  /* MoveWin, MainWin.pas:3331-3332. */
  mw->drag_x = 84;
  mw->drag_y = 5;
  mw->drag_w = 279 - 84;
  mw->drag_h = 22 - 5;
}

static void draw_base_skin(cairo_t* cr, GdkPixbuf* skin) {
  gdk_cairo_set_source_pixbuf(cr, skin, 0, 0);
  cairo_rectangle(cr, 0, 0, MW_WIDTH, MW_HEIGHT);
  cairo_fill(cr);
}

static gboolean on_expose(GtkWidget* widget, GdkEventExpose* event,
                           gpointer data) {
  (void)event;
  gui_mainwin* mw = (gui_mainwin*)data;
  cairo_t* cr = gdk_cairo_create(gtk_widget_get_window(widget));

  draw_base_skin(cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_play, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_pause, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_stop, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_open, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_min, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_close, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_prev, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_next, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_loop, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_about, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_list, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_mixer, cr, mw->skin.bitmap);
  gui_button_draw(&mw->but_tools, cr, mw->skin.bitmap);
  gui_led_draw(&mw->led_ay, cr, mw->skin.bitmap);
  gui_led_draw(&mw->led_ym, cr, mw->skin.bitmap);
  gui_led_draw(&mw->led_stereo, cr, mw->skin.bitmap);
  gui_hslider_draw(&mw->vol_slider, cr);
  gui_hslider_draw(&mw->progr_slider, cr);
  gui_visualizer_draw(&mw->vis, cr);

  cairo_destroy(cr);
  return FALSE;
}

/* Loads and plays a SPECIFIC file+subsong, bypassing the playlist -
 * used both as the playlist's own on_play callback (see
 * gui_playlist_win_create's on_play argument) and directly by on_timer
 * for ButLoop's same-song restart. */
/* PlayList.pas: PlayItem (~623-825) applies a playlist item's ItemEdit
 * overrides at actual playback time, each gated behind its own Mixer
 * "Get from list" checkbox (MIG-0088 - see gui_playlist_overrides's own
 * comment and gui_mixer_win.h's cb_use_*_list fields). Called only
 * after a successful gui_playback_load_song, since every override here
 * acts on the freshly-loaded ay_engine/player state. */
static void apply_item_overrides(gui_mainwin* mw,
                                  const gui_playlist_overrides* ov) {
  if (!ov) return;
  ay_engine* e = player_ay_engine(&mw->playback.p);
  bool recalc = false;

  if (ov->has_chip_type && gui_mixer_win_use_chip_type_list(&mw->mixerwin)) {
    e->chip_type = ov->chip_type;
    recalc = true;
  }

  if (ov->channel_mode != -1 &&
      gui_mixer_win_use_channel_mode_list(&mw->mixerwin)) {
    if (ov->channel_mode == -2) {
      e->index_al = ov->al;
      e->index_ar = ov->ar;
      e->index_bl = ov->bl;
      e->index_br = ov->br;
      e->index_cl = ov->cl;
      e->index_cr = ov->cr;
    } else {
      /* Mixer.pas: SBHelperClick's echo = 85 for AY_Chip, 13 for
       * YM_Chip - keyed off ChType AFTER any chip-type override above,
       * matching the original's own evaluation order (PlayItem applies
       * ChType before ChanTable, PlayList.pas ~700-750). */
      int echo = (e->chip_type == AY_CHIP_TYPE_AY) ? 85 : 13;
      mxhelper_calc_mode_coefs(ov->channel_mode, echo, &e->index_al,
                                &e->index_ar, &e->index_bl, &e->index_br,
                                &e->index_cl, &e->index_cr);
    }
    recalc = true;
  }

  if (recalc) ay_engine_calculate_level_tables(e);

  if (ov->ay_freq != -1 && gui_mixer_win_use_ay_freq_list(&mw->mixerwin)) {
    player_set_chip_freq(&mw->playback.p, ov->ay_freq, 48000);
  }
  if (ov->int_freq != -1 && gui_mixer_win_use_int_freq_list(&mw->mixerwin)) {
    player_set_player_freq(&mw->playback.p, ov->int_freq);
  }
}

static void do_load_song(gui_mainwin* mw, const char* path, int song_index,
                          const gui_playlist_overrides* overrides) {
  if (mw->file_loaded) gui_playback_free(&mw->playback);
  mw->file_loaded =
      gui_playback_load_song(&mw->playback, path, 48000, song_index);
  if (mw->file_loaded) {
    gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
    if (overrides) {
      mw->current_overrides = *overrides;
    }
    apply_item_overrides(mw, &mw->current_overrides);
    /* MIG-0094: (re)enable vis sampling for the freshly-loaded
     * ay_engine - gui_playback_load_song built a brand new player (and
     * therefore a brand new, vis-sampling-disabled-by-default
     * ay_engine, see ay_engine_init's own comment), so this needs to
     * run on every load, not just once at startup. 0.2 matches this
     * port's own configured ALSA output latency (tools/ay_player/src/
     * alsa_output.c's snd_pcm_set_params 200ms argument) - see
     * ay_engine_init_vis's own comment on why that's the right
     * substitute for the original's BufferLength*NumberOfBuffers. */
    ay_engine_init_vis(player_ay_engine(&mw->playback.p), 48000, 0.2);
    gui_playback_play(&mw->playback);
    char title[600];
    /* Real Author/Title metadata (MainWin.pas: PlayList.pas's
     * FormatScrollString "Author - Title" convention) when the format
     * and file actually carry it (currently only .ay - see
     * player_get_metadata_raw's own comment); falls back to the
     * filename otherwise, same as before this metadata support existed.
     * ItemEdit.pas's own title/author overrides (MIG-0088), if set,
     * take precedence over the read-only extracted values - matching
     * gui_playlist_entry_refresh_display's same precedence for the
     * playlist view. */
    const char* disp_author = mw->current_overrides.author[0]
                                   ? mw->current_overrides.author
                                   : mw->playback.meta_author;
    const char* disp_title = mw->current_overrides.title[0]
                                  ? mw->current_overrides.title
                                  : mw->playback.meta_title;
    if (disp_author[0] || disp_title[0]) {
      snprintf(title, sizeof(title), "ay_emul_c11 - %s - %s",
               disp_author[0] ? disp_author : "?",
               disp_title[0] ? disp_title : mw->playback.title);
    } else {
      snprintf(title, sizeof(title), "ay_emul_c11 - %s", mw->playback.title);
    }
    gtk_window_set_title(GTK_WINDOW(mw->window), title);
  } else {
    fprintf(stderr, "gui: failed to load or open audio for '%s' (song %d)\n",
            path, song_index);
  }
}

static void on_playlist_play(const char* path, int song_index,
                              const gui_playlist_overrides* overrides,
                              void* userdata) {
  do_load_song((gui_mainwin*)userdata, path, song_index, overrides);
}

static void do_open(gui_mainwin* mw) {
  GtkWidget* dlg = gtk_file_chooser_dialog_new(
      "Open chiptune file", GTK_WINDOW(mw->window), GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
      NULL);
  /* Tools.pas: EMFolder -> FrmMain.DefaultDirectory -> OpenDialog1.
   * InitialDir (MIG-0095). */
  if (mw->default_dir[0]) {
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg),
                                         mw->default_dir);
  }
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    gui_playlist_win_replace_with_path(&mw->plwin, path);
    g_free(path);
  }
  gtk_widget_destroy(dlg);
}

/* MainWin.pas: FormDropFiles - real behavior distinguishes dropped skin
 * files (.ays) from tune files. Simplified here (documented, not
 * silent - see migration_debt.yaml): runtime skin-switching was never
 * wired (only the embedded default skin loads, see MIG-0064), so a
 * dropped .ays is silently rejected by gui_playlist_add_file's own
 * player_load_song probe (not a recognized playable format) rather than
 * triggering a skin change. Non-skin drops now replace the playlist and
 * play the first entry, same as Open (MIG-0071) - matching
 * FormDropFiles's real StopAndFreeAll+ClearPlayList behavior more
 * closely than MIG-0070's original "first file only, no playlist"
 * version did. */
static void on_drag_data_received(GtkWidget* widget, GdkDragContext* ctx,
                                   gint x, gint y,
                                   GtkSelectionData* sel_data, guint info,
                                   guint time, gpointer data) {
  (void)widget;
  (void)x;
  (void)y;
  (void)info;
  gui_mainwin* mw = (gui_mainwin*)data;
  gchar** uris = gtk_selection_data_get_uris(sel_data);
  if (uris && uris[0]) {
    gchar* path = g_filename_from_uri(uris[0], NULL, NULL);
    if (path) {
      gui_playlist_win_replace_with_path(&mw->plwin, path);
      g_free(path);
    }
  }
  g_strfreev(uris);
  gtk_drag_finish(ctx, uris != NULL, FALSE, time);
}

static void do_about(gui_mainwin* mw) {
  /* MainWin.pas's ButAbout opens AboutBox - now the real skinned window
   * (gui/dialogs/about.c, MIG-0090), superseding the plain
   * GtkAboutDialog placeholder MIG-0068 used. */
  gui_about_show(GTK_WINDOW(mw->window));
}

static gboolean on_button_press(GtkWidget* widget, GdkEventButton* event,
                                 gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  int x = (int)event->x, y = (int)event->y;

  if (gui_button_hit_test(&mw->but_play, x, y)) {
    mw->but_play.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_pause, x, y)) {
    mw->but_pause.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_stop, x, y)) {
    mw->but_stop.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_open, x, y)) {
    mw->but_open.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_min, x, y)) {
    mw->but_min.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_close, x, y)) {
    mw->but_close.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_prev, x, y)) {
    mw->but_prev.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_next, x, y)) {
    mw->but_next.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_loop, x, y)) {
    mw->but_loop.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_about, x, y)) {
    mw->but_about.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_list, x, y)) {
    mw->but_list.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_mixer, x, y)) {
    mw->but_mixer.is_pushed = true;
  } else if (gui_button_hit_test(&mw->but_tools, x, y)) {
    mw->but_tools.is_pushed = true;
  } else if (gui_hslider_hit_test(&mw->vol_slider, x, y)) {
    mw->dragging_vol = true;
    mw->vol_slider.value = gui_hslider_value_from_x(&mw->vol_slider, x);
    if (mw->file_loaded)
      gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
  } else if (mw->file_loaded &&
             gui_hslider_hit_test(&mw->progr_slider, x, y)) {
    double frac;
    if (gui_playback_get_progress_fraction(&mw->playback, &frac)) {
      mw->dragging_progr = true;
      mw->progr_slider.value = gui_hslider_value_from_x(&mw->progr_slider, x);
      gui_playback_request_seek(&mw->playback, mw->progr_slider.value);
    } /* else: no known duration for this format (see playback.h's own
       * comment) - the slider stays the cosmetic, non-interactive
       * sweep it always was, same as before this entry. */
  } else if (gui_visualizer_handle_click(&mw->vis, x, y)) {
    /* MainWin.pas: ButSpaClick/ButAmpClick (MIG-0094) - toggled
     * already inside gui_visualizer_handle_click; nothing else to do
     * here besides the queue_draw every branch falls through to. */
  } else if (x >= mw->drag_x && x < mw->drag_x + mw->drag_w &&
             y >= mw->drag_y && y < mw->drag_y + mw->drag_h) {
    gdk_window_begin_move_drag(gtk_widget_get_window(widget), event->button,
                                (int)event->x_root, (int)event->y_root,
                                event->time);
  }
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean on_button_release(GtkWidget* widget, GdkEventButton* event,
                                   gpointer data) {
  (void)event;
  gui_mainwin* mw = (gui_mainwin*)data;

  if (mw->but_play.is_pushed) {
    mw->but_play.is_pushed = false;
    if (mw->file_loaded) gui_playback_play(&mw->playback);
  }
  if (mw->but_pause.is_pushed) {
    mw->but_pause.is_pushed = false;
    if (mw->file_loaded) {
      if (gui_playback_is_paused(&mw->playback))
        gui_playback_play(&mw->playback);
      else
        gui_playback_pause(&mw->playback);
    }
  }
  if (mw->but_stop.is_pushed) {
    mw->but_stop.is_pushed = false;
    if (mw->file_loaded) gui_playback_stop(&mw->playback);
  }
  if (mw->but_open.is_pushed) {
    mw->but_open.is_pushed = false;
    do_open(mw);
  }
  if (mw->but_min.is_pushed) {
    mw->but_min.is_pushed = false;
    gtk_window_iconify(GTK_WINDOW(mw->window));
    mw->minimized = true;
  }
  if (mw->but_close.is_pushed) {
    mw->but_close.is_pushed = false;
    gtk_main_quit();
  }
  if (mw->but_prev.is_pushed) {
    mw->but_prev.is_pushed = false;
    gui_playlist_win_prev(&mw->plwin); /* MainWin.pas: ButPrevClick ->
                                         * FrmPLst.PlayPreviousItem */
  }
  if (mw->but_next.is_pushed) {
    mw->but_next.is_pushed = false;
    gui_playlist_win_next(&mw->plwin); /* ButNextClick ->
                                         * FrmPLst.PlayNextItem */
  }
  if (mw->but_loop.is_pushed) {
    mw->but_loop.is_pushed = false;
    mw->do_loop = !mw->do_loop;
    mw->but_loop.is_on = mw->do_loop;
  }
  if (mw->but_list.is_pushed) {
    mw->but_list.is_pushed = false;
    mw->but_list.is_on = !mw->but_list.is_on;
    gui_playlist_win_toggle_visible(&mw->plwin);
  }
  if (mw->but_mixer.is_pushed) {
    mw->but_mixer.is_pushed = false;
    mw->but_mixer.is_on = !mw->but_mixer.is_on;
    gui_mixer_win_toggle_visible(&mw->mixerwin);
  }
  if (mw->but_tools.is_pushed) {
    mw->but_tools.is_pushed = false;
    mw->but_tools.is_on = !mw->but_tools.is_on;
    gui_tools_win_toggle_visible(&mw->toolswin);
  }
  if (mw->but_about.is_pushed) {
    mw->but_about.is_pushed = false;
    do_about(mw);
  }
  if (mw->dragging_progr) {
    mw->dragging_progr = false;
    /* Final seek to exactly the release position - on_motion below
     * already requested seeks during the drag itself (a live-scrub
     * effect), but the last of those may have been superseded by mouse
     * movement between the final motion event and this release. */
    if (mw->file_loaded)
      gui_playback_request_seek(&mw->playback, mw->progr_slider.value);
  }
  mw->dragging_vol = false;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean on_motion(GtkWidget* widget, GdkEventMotion* event,
                           gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  if (mw->dragging_progr) {
    mw->progr_slider.value =
        gui_hslider_value_from_x(&mw->progr_slider, (int)event->x);
    if (mw->file_loaded)
      gui_playback_request_seek(&mw->playback, mw->progr_slider.value);
  }
  if (mw->dragging_vol) {
    mw->vol_slider.value = gui_hslider_value_from_x(&mw->vol_slider,
                                                      (int)event->x);
    if (mw->file_loaded)
      gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
    gtk_widget_queue_draw(widget);
  }
  return TRUE;
}

/* MainWin.pas: VisTimerEvent -> DoVisualisation's AYVisualisation call
 * (MIG-0094) - a separate, faster-cadence timer than on_timer below
 * (30ms, matching VisTimerPeriod's own default, vs on_timer's 200ms). */
static gboolean on_vis_timer(gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  ay_engine* engine = mw->file_loaded ? player_ay_engine(&mw->playback.p) : NULL;
  uint32_t smp = mw->file_loaded
                     ? (uint32_t)atomic_load(&mw->playback.frames_played)
                     : 0;
  gui_visualizer_tick(&mw->vis, engine, smp);
  gtk_widget_queue_draw(mw->area);
  return TRUE;
}

static gboolean on_timer(gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  if (mw->file_loaded) {
    /* Real fraction-of-song-length progress for AY/YM/VTX (MIG-0079,
     * player_get_tick_position's own comment on why only these three);
     * every other format still has no reliable total-duration concept
     * (most tracker formats loop forever with no declared song length),
     * so those keep the same cosmetic repeating sweep as before this
     * entry - just enough to visibly show playback is advancing, not a
     * real position. Skipped entirely while the user is actively
     * dragging the slider (on_button_press/on_motion own that value
     * during a drag - overwriting it here would fight the drag). */
    if (!mw->dragging_progr) {
      double frac;
      if (gui_playback_get_progress_fraction(&mw->playback, &frac)) {
        mw->progr_slider.value = frac;
      } else {
        double pos = gui_playback_position_seconds(&mw->playback);
        mw->progr_slider.value = fmod(pos, 240.0) / 240.0;
      }
    }

    /* Led_AY/Led_YM, MainWin.pas: reflects the loaded format's actual
     * AY.pas ChType (see player_chip_type's own comment) - mutually
     * exclusive, unlike Led_Stereo which is always on. */
    ay_chip_type ct = player_chip_type(&mw->playback.p);
    mw->led_ay.state = (ct == AY_CHIP_TYPE_AY);
    mw->led_ym.state = (ct == AY_CHIP_TYPE_YM);

    if (gui_playback_is_finished(&mw->playback)) {
      if (mw->do_loop) {
        char path[sizeof(mw->playback.path)];
        strncpy(path, mw->playback.path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        int song_index = mw->playback.song_index;
        do_load_song(mw, path, song_index,
                     NULL); /* restart same (sub)song, keep overrides */
      } else {
        gui_playback_stop(&mw->playback);
      }
    }

    /* MainWin.pas:1938: TrayIcon1.Hint := ss (the same formatted
     * Author-Title/filename string used for the title bar, MIG-0072). */
    char hint[600];
    if (mw->playback.meta_author[0] || mw->playback.meta_title[0]) {
      snprintf(hint, sizeof(hint), "%s - %s",
               mw->playback.meta_author[0] ? mw->playback.meta_author : "?",
               mw->playback.meta_title[0] ? mw->playback.meta_title
                                           : mw->playback.title);
    } else {
      snprintf(hint, sizeof(hint), "%s", mw->playback.title);
    }
    gtk_status_icon_set_tooltip_text(mw->tray_icon, hint);
  } else {
    gtk_status_icon_set_tooltip_text(mw->tray_icon, "AY Emulator");
  }
  gtk_widget_queue_draw(mw->area);
  return TRUE; /* keep firing */
}

static void on_realize(GtkWidget* widget, gpointer data) {
  (void)data;
  gui_apply_main_window_shape(gtk_widget_get_window(widget));
}

/* MainWin.pas:3525-3596's full FormKeyDown dispatch (the nested Push()
 * helper simulates a button click for the letter-key shortcuts).
 * Ported: J (JumpToTime), E/G/R/X/V/C/B/Z/L (List/Mixer/Loop/Play/Stop/
 * Pause/Next/Prev/Open), Up/Down (VolUp/VolDown, 1/w of the slider
 * width per press - MoveVol.PosX moves by exactly 1 pixel per call),
 * Left/Right (rewind/advance 5s, MainWin.pas:3577-3588's
 * `Rewind(CurrTime_Rasch +/- 5000, Time_ms)`), Escape (DoMinimize).
 * 'P' (ButTools - MIG-0095, was previously unported per MIG-0069).
 * NOT ported: VK_NUMPAD5/6/4/0/8/2 (redundant numpad
 * duplicates of X/B/Z/L/Up/Down, already reachable via their letter/
 * arrow-key equivalents); F1 (CallHelp opens a .chm help file this
 * port doesn't have). */
static gboolean on_key_press(GtkWidget* widget, GdkEventKey* event,
                              gpointer data) {
  (void)widget;
  gui_mainwin* mw = (gui_mainwin*)data;
  switch (event->keyval) {
    case GDK_j:
    case GDK_J:
      gui_jptime_show(GTK_WINDOW(mw->window), &mw->playback);
      return TRUE;
    case GDK_e:
    case GDK_E:
      mw->but_list.is_on = !mw->but_list.is_on;
      gui_playlist_win_toggle_visible(&mw->plwin);
      return TRUE;
    case GDK_g:
    case GDK_G:
      mw->but_mixer.is_on = !mw->but_mixer.is_on;
      gui_mixer_win_toggle_visible(&mw->mixerwin);
      return TRUE;
    case GDK_p:
    case GDK_P:
      mw->but_tools.is_on = !mw->but_tools.is_on;
      gui_tools_win_toggle_visible(&mw->toolswin);
      return TRUE;
    case GDK_r:
    case GDK_R:
      mw->do_loop = !mw->do_loop;
      mw->but_loop.is_on = mw->do_loop;
      return TRUE;
    case GDK_x:
    case GDK_X:
      if (mw->file_loaded) gui_playback_play(&mw->playback);
      return TRUE;
    case GDK_v:
    case GDK_V:
      if (mw->file_loaded) gui_playback_stop(&mw->playback);
      return TRUE;
    case GDK_c:
    case GDK_C:
      if (mw->file_loaded) {
        if (gui_playback_is_paused(&mw->playback))
          gui_playback_play(&mw->playback);
        else
          gui_playback_pause(&mw->playback);
      }
      return TRUE;
    case GDK_b:
    case GDK_B:
      gui_playlist_win_next(&mw->plwin);
      return TRUE;
    case GDK_z:
    case GDK_Z:
      gui_playlist_win_prev(&mw->plwin);
      return TRUE;
    case GDK_l:
    case GDK_L:
      do_open(mw);
      return TRUE;
    case GDK_Up:
      mw->vol_slider.value += 1.0 / mw->vol_slider.w;
      if (mw->vol_slider.value > 1.0) mw->vol_slider.value = 1.0;
      if (mw->file_loaded)
        gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
      gtk_widget_queue_draw(mw->area);
      return TRUE;
    case GDK_Down:
      mw->vol_slider.value -= 1.0 / mw->vol_slider.w;
      if (mw->vol_slider.value < 0.0) mw->vol_slider.value = 0.0;
      if (mw->file_loaded)
        gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
      gtk_widget_queue_draw(mw->area);
      return TRUE;
    case GDK_Left:
      if (mw->file_loaded && gui_playback_duration_seconds(&mw->playback) > 0)
        gui_playback_request_seek_seconds(
            &mw->playback, gui_playback_position_seconds(&mw->playback) - 5.0);
      return TRUE;
    case GDK_Right:
      if (mw->file_loaded && gui_playback_duration_seconds(&mw->playback) > 0)
        gui_playback_request_seek_seconds(
            &mw->playback, gui_playback_position_seconds(&mw->playback) + 5.0);
      return TRUE;
    case GDK_Escape:
      gtk_window_iconify(GTK_WINDOW(mw->window));
      mw->minimized = true;
      return TRUE;
    default:
      return FALSE;
  }
}

/* MainWin.pas: TrayIcon1MouseUp (left-click toggles iconify/restore -
 * the real logic also checks WindowState=wsMinimized directly rather
 * than tracking a separate flag, but GTK2 has no simple synchronous
 * "is this window iconified" query independent of window-state-event
 * bookkeeping, so mw->minimized (updated by both ButMin and this
 * handler) serves the same purpose). GTK's "activate" signal fires on
 * left-click, matching TrayIcon1MouseUp's `Button = mbLeft` guard -
 * TrayIcon1DblClick has no additional real effect beyond what
 * MouseDown/MouseUp already do (it only sets the same TrayIconClicked
 * flag MouseDown does), so it isn't separately wired here. */
static void on_tray_activate(GtkStatusIcon* icon, gpointer data) {
  (void)icon;
  gui_mainwin* mw = (gui_mainwin*)data;
  if (mw->minimized) {
    gtk_window_deiconify(GTK_WINDOW(mw->window));
    gtk_window_present(GTK_WINDOW(mw->window));
    mw->minimized = false;
  } else {
    gtk_window_iconify(GTK_WINDOW(mw->window));
    mw->minimized = true;
  }
}

bool gui_mainwin_create(gui_mainwin* mw) {
  memset(mw, 0, sizeof(*mw));
  mw->current_overrides.channel_mode = -1;
  mw->current_overrides.ay_freq = -1;
  mw->current_overrides.int_freq = -1;

  if (!gui_skin_load_default(&mw->skin)) {
    fprintf(stderr, "gui: failed to load default skin\n");
    return false;
  }
  init_zones(mw);

  mw->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(mw->window), "ay_emul_c11");
  gtk_window_set_decorated(GTK_WINDOW(mw->window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(mw->window), FALSE);
  gtk_widget_set_size_request(mw->window, MW_WIDTH, MW_HEIGHT);
  g_signal_connect(mw->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(mw->window, "realize", G_CALLBACK(on_realize), mw);
  g_signal_connect(mw->window, "key-press-event", G_CALLBACK(on_key_press),
                    mw);

  mw->area = gtk_drawing_area_new();
  gtk_widget_set_size_request(mw->area, MW_WIDTH, MW_HEIGHT);
  gtk_widget_add_events(mw->area, GDK_BUTTON_PRESS_MASK |
                                       GDK_BUTTON_RELEASE_MASK |
                                       GDK_POINTER_MOTION_MASK);
  g_signal_connect(mw->area, "expose-event", G_CALLBACK(on_expose), mw);
  g_signal_connect(mw->area, "button-press-event",
                    G_CALLBACK(on_button_press), mw);
  g_signal_connect(mw->area, "button-release-event",
                    G_CALLBACK(on_button_release), mw);
  g_signal_connect(mw->area, "motion-notify-event", G_CALLBACK(on_motion),
                    mw);

  static const GtkTargetEntry drop_targets[] = {
      {(gchar*)"text/uri-list", 0, 0}};
  gtk_drag_dest_set(mw->area, GTK_DEST_DEFAULT_ALL, drop_targets,
                     G_N_ELEMENTS(drop_targets), GDK_ACTION_COPY);
  g_signal_connect(mw->area, "drag-data-received",
                    G_CALLBACK(on_drag_data_received), mw);

  gtk_container_add(GTK_CONTAINER(mw->window), mw->area);

  gtk_widget_show_all(mw->window);

  mw->vis_period_ms = 30; /* MainWin.pas: VisTimerPeriod's own default -
                            * set before gui_tools_win_create below so
                            * its entry is pre-filled correctly */

  gui_playlist_win_create(&mw->plwin, GTK_WINDOW(mw->window),
                           on_playlist_play, mw);
  gui_mixer_win_create(&mw->mixerwin, GTK_WINDOW(mw->window), &mw->playback);
  gui_tools_win_create(&mw->toolswin, GTK_WINDOW(mw->window), mw);

  /* MainWin.pas: AddTrayIcon - real behavior loads one of the app's own
   * embedded ICON00-ICON99 Windows icon resources (TrayIconNumber,
   * user-selectable); those are separate assets not present in the
   * .ays skin and not ported (see migration_debt.yaml) - a generic
   * icon-theme lookup is used instead, a documented simplification. */
  mw->tray_icon = gtk_status_icon_new_from_icon_name("multimedia-player");
  gtk_status_icon_set_tooltip_text(mw->tray_icon, "AY Emulator");
  gtk_status_icon_set_visible(mw->tray_icon, TRUE);
  g_signal_connect(mw->tray_icon, "activate", G_CALLBACK(on_tray_activate),
                    mw);

  gui_visualizer_init(&mw->vis);

  mw->timer_id = g_timeout_add(200, on_timer, mw);
  mw->vis_timer_id = g_timeout_add((guint)mw->vis_period_ms, on_vis_timer, mw);
  return true;
}

void gui_mainwin_set_vis_period(gui_mainwin* mw, int period_ms) {
  if (period_ms <= 9 || period_ms >= 101) return; /* MainWin.pas:1716 */
  mw->vis_period_ms = period_ms;
  if (mw->vis_timer_id) g_source_remove(mw->vis_timer_id);
  mw->vis_timer_id = g_timeout_add((guint)mw->vis_period_ms, on_vis_timer, mw);
}

void gui_mainwin_destroy(gui_mainwin* mw) {
  if (mw->timer_id) g_source_remove(mw->timer_id);
  if (mw->vis_timer_id) g_source_remove(mw->vis_timer_id);
  if (mw->file_loaded) gui_playback_free(&mw->playback);
  gui_playlist_win_destroy(&mw->plwin);
  gui_mixer_win_destroy(&mw->mixerwin);
  gui_tools_win_destroy(&mw->toolswin);
  if (mw->tray_icon) g_object_unref(mw->tray_icon);
  gui_skin_free(&mw->skin);
}
