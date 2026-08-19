/* C11/GTK2 port of MainWin.pas's TFrmMain - Phase 5, updated as work
 * lands (see PHASE5_GUI_PROGRESS.md for the current feature-by-feature
 * status, not this comment - it goes stale, the tracker doesn't).
 *
 * The progress slider (MoveProgr) is draggable/seekable (real position,
 * not a cosmetic sweep, MIG-0079) for every format player_get_tick_
 * position() supports - which, as of MIG-0103/MIG-0104 (porting the
 * remaining formats' own GetTimeXXX duration precomputes into their
 * struct fields) and this session's later wiring pass, is now all 18
 * supported formats, not just AY/YM/VTX/SNDH/PT3 (see gui_playback_
 * get_progress_fraction's own comment). No format is left on the pre-
 * MIG-0079 cosmetic repeating sweep any more.
 *
 * The visualizer (spectrum/amplitude, MIG-0094) and ButTools (MIG-0095,
 * see MIG-0069 for why it was originally deferred) are both fully
 * ported, not deferred - this paragraph previously listed them as
 * still-outstanding gaps after they'd already landed; see PHASE5_GUI_
 * PROGRESS.md and migration_debt.yaml for what (if anything) is still
 * genuinely open in this file.
 */
#include "gui/mainwin.h"

#include <gdk/gdkkeysyms.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gui/playlist_win.h"
#include "gui/regions.h"
#include "gui/settings.h"
#include "ay_engine/player.h"
#include "gui/dialogs/about.h"
#include "gui/dialogs/jmptime.h"
#include "gui/dialogs/mxhelper.h"

/* INT_MIN as the "key absent" sentinel for gui_settings_get_int lookups
 * below - every real saved window coordinate this session ever writes is
 * a normal screen coordinate, never anywhere close to INT_MIN, so this
 * is safe as a "not present" marker without needing a separate has-key
 * query. */
#define SETTINGS_INT_ABSENT INT_MIN

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
  mw->led_stereo.state = false; /* MainWin.pas:1063 - lit only once a
                                  * mono-output file has actually been
                                  * loaded (see on_timer's own comment);
                                  * false here just matches no file being
                                  * loaded yet. */

  /* MoveVol/MoveProgr, MainWin.pas:3333-3334; handle bitmap size/source
   * from their own AddBitmaps calls (MainWin.pas:3813-3814). */
  mw->vol_slider = (gui_hslider){.x = 237, .y = 22, .w = 70, .h = 12,
                                  .thumb_w = 18, .thumb_h = 11,
                                  .thumb_src_x = 358 - 41, .thumb_src_y = 113,
                                  .value = 1.0};
  mw->progr_slider = (gui_hslider){.x = 96, .y = 83, .w = 159, .h = 10,
                                    .thumb_w = 20, .thumb_h = 10,
                                    .thumb_src_x = 0, .thumb_src_y = 103,
                                    .value = 0.0};

  /* MoveWin, MainWin.pas:3331-3332. */
  mw->drag_x = 84;
  mw->drag_y = 5;
  mw->drag_w = 279 - 84;
  mw->drag_h = 22 - 5;
}

static void draw_base_skin(cairo_t* cr, GdkPixbuf* skin) {
  gdk_cairo_set_source_pixbuf(cr, skin, 0, 0);
  /* Hard-edge/nearest-pixel sampling, matching zones.c's draw_pixbuf_region
   * (see its own comment) - keeps the base skin's own edges crisp at any
   * scale and, more importantly, matches exactly the pixel grid every
   * button/LED/slider sprite is composited onto, so no scale-dependent
   * seam appears between this base paint and their own overlays. */
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
  cairo_rectangle(cr, 0, 0, MW_WIDTH, MW_HEIGHT);
  cairo_fill(cr);
}

static gboolean on_expose(GtkWidget* widget, GdkEventExpose* event,
                           gpointer data) {
  (void)event;
  gui_mainwin* mw = (gui_mainwin*)data;
  cairo_t* cr = gdk_cairo_create(gtk_widget_get_window(widget));

  /* MainWin.pas: every Canvas.CopyRect call multiplies its destination
   * rect by Scale while blitting from the SAME 1x-resolution skin
   * bitmap (MIG-0119) - cairo_scale achieves the identical net effect
   * (the same 1x-space drawing calls below render `scale`x larger) in
   * one call instead of hand-multiplying every one of their x/y/w/h
   * arguments, since every draw call below already goes through this
   * one `cr` and Cairo's scale transform applies to all of them
   * uniformly, including the offscreen-surface composites in zones.c/
   * ticker.c (their own internal surfaces stay 1x - only the final
   * paint onto `cr` is scaled, matching a stretched CopyRect blit). */
  cairo_scale(cr, mw->scale, mw->scale);

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
  gui_hslider_draw(&mw->vol_slider, cr, mw->skin.bitmap);
  gui_hslider_draw(&mw->progr_slider, cr, mw->skin.bitmap);
  gui_visualizer_draw(&mw->vis, cr);
  gui_time_display_draw(&mw->time_disp, cr, &mw->playback);
  gui_ticker_draw(&mw->ticker, cr, mw->skin.bitmap);

  cairo_destroy(cr);
  return FALSE;
}

/* Keeps but_list/but_mixer/but_tools's Is_On state (and their skin-button
 * "pushed" appearance) mirroring each sub-window's ACTUAL visibility,
 * rather than only the button-click/keyboard-shortcut paths that used to
 * toggle it directly. Connected to both the "show" and "hide" GObject
 * signals of each sub-window below, so it fires for every way a window's
 * visibility can change - not just gui_playlist_win_toggle_visible's own
 * gtk_widget_show_all/gtk_widget_hide calls, but also the window's own
 * delete-event (gtk_widget_hide_on_delete), which previously left the
 * button visually stuck "pushed" after being closed via its own
 * titlebar/window-manager close control instead of the skin button. */
static void on_subwin_visibility_changed(GtkWidget* widget, gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  GtkWidget* area = mw->area;
  if (widget == mw->plwin.window) {
    mw->but_list.is_on = gtk_widget_get_visible(widget);
  } else if (widget == mw->mixerwin.window) {
    mw->but_mixer.is_on = gtk_widget_get_visible(widget);
  } else if (widget == mw->toolswin.window) {
    mw->but_tools.is_on = gtk_widget_get_visible(widget);
  }
  gtk_widget_queue_draw(area);
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
  ay_engine* e = player_ay_engine(&mw->playback.pair.primary);
  bool recalc = false;

  if (ov->has_chip_type && gui_mixer_win_use_chip_type_list(&mw->mixerwin)) {
    e->chip_type = ov->chip_type;
    recalc = true;
  }

  bool item_channel_mode_applied = ov->channel_mode != -1 &&
                                    gui_mixer_win_use_channel_mode_list(
                                        &mw->mixerwin);
  if (item_channel_mode_applied) {
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

  /* PlayList.pas:798: AtariYMMonoChk -> Set_Mode(0) (mono panning preset),
   * a SNDH-specific fallback default applied only when no real per-item
   * channel-mode override already won above (MIG-0121). */
  if (!item_channel_mode_applied &&
      mw->playback.pair.primary.format == PLAYER_FORMAT_SNDH &&
      gui_mixer_win_atari_ym_mono(&mw->mixerwin)) {
    int echo = (e->chip_type == AY_CHIP_TYPE_AY) ? 85 : 13;
    mxhelper_calc_mode_coefs(0, echo, &e->index_al, &e->index_ar,
                              &e->index_bl, &e->index_br, &e->index_cl,
                              &e->index_cr);
    recalc = true;
  }

  if (recalc) ay_engine_calculate_level_tables(e);

  /* PlayList.pas:761: AtariMonoChk -> Set_Stereo(1) (mono OUTPUT) -
   * SNDH-specific fallback default, wired to the same load-time-only
   * player_set_number_of_channels this port already uses for CBChLst's
   * real per-item channel-count override (MIG-0107/0108, see
   * resolve_number_of_channels/do_load_song above); a plain "when
   * AtariMonoChk is checked and this is a loaded SNDH file, force mono
   * output" mapping, independent of the channel-mode/panning override
   * above (PlayList.pas applies both as separate steps). */
  if (mw->playback.pair.primary.format == PLAYER_FORMAT_SNDH &&
      gui_mixer_win_atari_mono(&mw->mixerwin)) {
    player_set_number_of_channels(&mw->playback.pair.primary, 1);
  }

  if (ov->ay_freq != -1 && gui_mixer_win_use_ay_freq_list(&mw->mixerwin)) {
    player_set_chip_freq(&mw->playback.pair.primary, ov->ay_freq,
                          mw->playback.sample_rate);
  }
  if (ov->int_freq != -1 && gui_mixer_win_use_int_freq_list(&mw->mixerwin)) {
    player_set_player_freq(&mw->playback.pair.primary, ov->int_freq);
  }
}

/* PlayList.pas: PlayItem's Number_Of_Channels resolution (~748-759) -
 * CBChLst-gated, item override first, then PLDef_Number_Of_Channels,
 * else left unchanged in the original (a persistent global there); this
 * port's per-load fresh player has no such persistent global to fall
 * back to, so "neither set" resolves to 2 (stereo) - the same default
 * ay_engine_init itself already applies, and the standing pattern this
 * port's own comments already document for every other per-load engine
 * default (see do_load_song's own ay_engine_init_vis comment). Channel
 * count must be resolved BEFORE gui_playback_load_song (unlike the other
 * three overrides in apply_item_overrides below), since it drives the
 * ALSA device's own channel count at open time, not just engine state
 * set after the fact. */
static int resolve_number_of_channels(gui_mainwin* mw,
                                       const gui_playlist_overrides* ov) {
  /* MIG-0130: both fallbacks below were a hardcoded literal 2 - PlayList.
   * pas:748-759's own real logic never falls back to a fixed constant:
   * when CBChLst is unchecked, or checked but neither an item nor a
   * PLDef override is actually set, NumberOfChannels is simply left
   * UNTOUCHED, i.e. whatever GBChans' own RBChStereo/RBChMono last set
   * it to (mw->default_channels here, this port's own per-load-fresh-
   * engine equivalent of that persistent global - see mainwin.h's own
   * field comment). */
  if (!gui_mixer_win_use_channel_count_list(&mw->mixerwin))
    return mw->default_channels;
  if (ov && ov->number_of_channels > 0) return ov->number_of_channels;
  if (mw->plwin.defaults.number_of_channels > 0)
    return mw->plwin.defaults.number_of_channels;
  return mw->default_channels;
}

static void do_load_song(gui_mainwin* mw, const char* path, int song_index,
                          const gui_playlist_overrides* overrides) {
  if (mw->file_loaded) gui_playback_free(&mw->playback);
  /* Update mw->current_overrides BEFORE resolving channels (not just
   * before apply_item_overrides, as this used to do) - a ButLoop same-
   * song restart calls this with `overrides == NULL` to mean "keep the
   * previous item's overrides" (see this function's own caller below),
   * and resolve_number_of_channels must see THOSE persisted overrides,
   * not silently fall back to the PLDef/2 default, exactly matching how
   * apply_item_overrides already relies on mw->current_overrides being
   * current by the time it reads it. */
  if (overrides) {
    mw->current_overrides = *overrides;
  }
  int channels = resolve_number_of_channels(mw, &mw->current_overrides);
  /* MIG-0112: mw->plwin.model.current already indexes the item being
   * loaded here for every load path (Open, playlist double-click,
   * Next/Prev, loop-restart) - see this function's own ticker-text
   * comment below, which already relies on that same invariant. */
  int pl_idx = mw->plwin.model.current;
  bool ts_pair = (pl_idx >= 0 && pl_idx < mw->plwin.model.count) &&
                 mw->plwin.model.items[pl_idx].has_ts_pair;
  /* Mixer.pas: GBSNDH's STRB/STeRB (MIG-0121) - a load-time-only
   * format-variant flag, resolved here before gui_playback_load_song,
   * same timing as `channels`/`ts_pair` above. */
  bool is_ste = gui_mixer_win_is_ste(&mw->mixerwin);
  /* Mixer.pas: GBSRate/GBBRate/GBBuffs/GBDevice (MIG-0130) - resolved
   * here, same load-time-only timing as every override above. */
  const char* device = mw->output_device[0] ? mw->output_device : NULL;
  mw->file_loaded = gui_playback_load_song(
      &mw->playback, path, mw->sample_rate, song_index, channels, ts_pair,
      is_ste, device, mw->sample_bits, mw->buf_len_ms, mw->num_buffers);
  if (mw->file_loaded) {
    gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
    apply_item_overrides(mw, &mw->current_overrides);
    /* Tools.pas: CBForceLoop - MIG-0114. Read here (not pushed live by a
     * "toggled" handler) matching gui_mixer_win's own use_*_list
     * checkboxes' established pattern (see apply_item_overrides above) -
     * a no-op on the non-paired/non-active side of player_pair_set_
     * force_loop when this load didn't produce a real pairing. */
    player_pair_set_force_loop(&mw->playback.pair,
                                gui_tools_win_force_loop(&mw->toolswin));
    /* MIG-0106: MainWin.pas's Set_Chip_Frq is what actually calls
     * SetFilter(FilterQuality) (see player_set_chip_freq's own comment) -
     * apply_item_overrides above only calls it when a playlist item has
     * an explicit AY-frequency override (rare), so a plain load would
     * otherwise never activate filtering at all (gui_playback_load_song
     * built a brand new, filter-disabled-by-default ay_engine, same
     * "resets to defaults" situation as the MIG-0094 vis call just
     * below). Re-establishing with the file's own CURRENT clock
     * (already-overridden or not) is a no-op for delay_in_tiks/tik_re/
     * frq_ay_by_frq_z80 (recomputing from the same value that's already
     * there) - the only observable effect is ay_engine_set_filter
     * actually running. */
    player_set_chip_freq(&mw->playback.pair.primary,
                          player_get_ay_freq(&mw->playback.pair.primary),
                          mw->playback.sample_rate);
    /* MIG-0094: (re)enable vis sampling for the freshly-loaded
     * ay_engine - gui_playback_load_song built a brand new player (and
     * therefore a brand new, vis-sampling-disabled-by-default
     * ay_engine, see ay_engine_init's own comment), so this needs to
     * run on every load, not just once at startup. MIG-0130: now uses
     * the REAL configured ALSA output latency (mw->buf_len_ms *
     * mw->num_buffers, GBBuffs) instead of a fixed 200ms guess -
     * exactly the original's own BufferLength*NumberOfBuffers this was
     * always meant to substitute for (see ay_engine_init_vis's own
     * comment). */
    ay_engine_init_vis(player_ay_engine(&mw->playback.pair.primary),
                        mw->playback.sample_rate,
                        (double)(mw->buf_len_ms * mw->num_buffers) / 1000.0);
    /* MIG-0123: MainWin.pas:907's `Atari_DMAMax := Atari_DMAMaxDef;`
     * (146) - a ONE-TIME real-interactive-app startup default in the
     * original (a persistent global, never reset by a file load - see
     * ay.h's own atari_dma_max comment for why this port's own per-load
     * ay_engine_init instead defaults to 0, matching the oracle-diff
     * test harness's own deliberate reset convention rather than this
     * value). Applied here, on every fresh load, to match this port's
     * own already-established "every Mixer control resets to its
     * compiled-in default on a fresh load, live-adjustable within the
     * session" model (same as beeper_max, filter_quality, vis sampling
     * just above) - the closest faithful equivalent given this port's
     * per-load struct architecture has no persistent cross-load global
     * to fall back to instead. Every per-format _file_load already calls
     * ay_engine_calculate_level_tables once at the very end of loading
     * (with atari_dma_max still 0 at that point, since this field write
     * runs after the load completes) - so this needs its own explicit
     * recalc call, exactly like apply_item_overrides's own `recalc`
     * flag above, or the new value would sit unused in the struct until
     * something else (a Mixer slider drag) happened to trigger one. */
    {
      ay_engine* dma_e = player_ay_engine(&mw->playback.pair.primary);
      dma_e->atari_dma_max = 146;
      ay_engine_calculate_level_tables(dma_e);
    }
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

    /* MIG-0098: PlayList.pas's PlayItem setting `Scroll_Distination :=
     * Index` (~657) - gui_playlist_win's own model.current already IS
     * that index by the time do_load_song runs here (fire_play sets it
     * before invoking on_playlist_play), for every load path (Open,
     * playlist double-click, Next/Prev, and the loop-restart call
     * below all funnel through the playlist model - see gui_playlist_
     * win_replace_with_path's own comment). model.items[idx].display
     * already reflects ItemEdit override precedence (gui_playlist_
     * entry_refresh_display), matching this exact function's own
     * disp_author/disp_title precedence above, so it's reused directly
     * rather than reformatted a third time. */
    int idx = mw->plwin.model.current;
    const char* ticker_text =
        (idx >= 0 && idx < mw->plwin.model.count)
            ? mw->plwin.model.items[idx].display
            : mw->playback.title;
    bool is_next = mw->ticker_last_index >= 0 && idx == mw->ticker_last_index + 1;
    bool is_prev = mw->ticker_last_index >= 0 && idx == mw->ticker_last_index - 1;
    gui_ticker_set_target(&mw->ticker, ticker_text, is_next, is_prev);
    mw->ticker_last_index = idx;
  } else {
    fprintf(stderr, "gui: failed to load or open audio for '%s' (song %d)\n",
            path, song_index);
    /* PlayList.pas: PlayListItems[n]^.Error<>FileNoError (MIG-0126) -
     * see gui/include/gui/playlist.h's own load_error comment. */
    if (pl_idx >= 0 && pl_idx < mw->plwin.model.count) {
      mw->plwin.model.items[pl_idx].load_error = true;
      gtk_widget_queue_draw(mw->plwin.tree_view);
    }
  }
  if (mw->file_loaded && pl_idx >= 0 && pl_idx < mw->plwin.model.count &&
      mw->plwin.model.items[pl_idx].load_error) {
    /* RedrawItemRealy reads Error fresh on every redraw, not a sticky
     * latch - a previously-failing entry that loads successfully now
     * (e.g. the underlying file was fixed) goes back to its normal
     * color, matching that same always-current semantics. */
    mw->plwin.model.items[pl_idx].load_error = false;
    gtk_widget_queue_draw(mw->plwin.tree_view);
  }
}

static void on_playlist_play(const char* path, int song_index,
                              const gui_playlist_overrides* overrides,
                              void* userdata) {
  do_load_song((gui_mainwin*)userdata, path, song_index, overrides);
}

/* MainWin.pas: PlayClick -> PlayCurrent -> InitForAllTypes(True)
 * (Players.pas:4049-4068) - starting a NEW play session (as opposed to
 * resuming from pause, which must NOT do this - see on_button_release's
 * but_pause branch) always rewinds the song to its very beginning first:
 * Real_End_All/Real_End[0..1] cleared, every tracker format's own
 * position/pattern pointers reset via InitTrackerModule (or the Z80
 * PC/registers reset to the init entry point for AY-native formats),
 * Global_Tick_Counter/CurrTime_Rasch zeroed, chip state reset. Without
 * this, pressing Play again after Stop or a natural end left the
 * loaded player's own real_end_all flag (and every format's tick/
 * position state) exactly where the previous session left it - the
 * very first buffer-fill call of the new playback thread would
 * immediately see real_end_all still true and exit again on the spot,
 * with but_play.is_on flickering on then straight back off as on_timer
 * re-detected "finished" a moment later and auto-stopped it again, and
 * no audio ever actually played (a real bug, not merely cosmetic).
 * This port has no lightweight in-place "rewind" equivalent to
 * InitForAllTypes - it reuses do_load_song's own reload-from-disk path
 * instead (the exact same mechanism the do_loop natural-end restart
 * below already relies on), which reconstructs an equally fresh
 * player/ay_engine and correctly reapplies ItemEdit overrides/chip-
 * frequency/vis-init along the way, at the cost of a real (but tiny,
 * a few KB for any real chiptune file) file re-read Pascal's own
 * in-place reset avoids. */
static void restart_current_song(gui_mainwin* mw) {
  char path[sizeof(mw->playback.path)];
  strncpy(path, mw->playback.path, sizeof(path) - 1);
  path[sizeof(path) - 1] = '\0';
  int song_index = mw->playback.song_index;
  do_load_song(mw, path, song_index, NULL);
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

/* MainWin.pas: MoveVol's Action callback ultimately runs VolumeCtrl :=
 * MoveVol.PosX; SetSysVolume (MainWin.pas:2725-2726) on every drag/key/
 * scroll step - this is that same "push the current slider position out
 * everywhere it needs to go" step, shared by every vol_slider mutator
 * below (on_button_press's press branch, on_motion's drag branch, and
 * vol_up/vol_down). Pushes to the software PCM-scale path (unchanged
 * from before this session, file_loaded-gated - see gui_playback_set_
 * volume's own comment) AND, new this session, the real ALSA hardware
 * mixer control if one is open (see mainwin.h's own sysvol field
 * comment for why both now run side by side rather than one replacing
 * the other). */
static void apply_volume(gui_mainwin* mw) {
  if (mw->file_loaded)
    gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
  if (mw->sysvol)
    gui_alsa_mixer_set_volume(mw->sysvol, mw->vol_slider.value,
                               mw->sysvol_linear);
}

static gboolean on_button_press(GtkWidget* widget, GdkEventButton* event,
                                 gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  /* MIG-0119: every zone/button rect below is in 1x skin-space (see
   * on_expose's own cairo_scale comment) - incoming event coordinates
   * are real window pixels, `scale`x larger, so they're converted back
   * to skin-space here once, up front. event->x_root/y_root (used only
   * for the titlebar-drag branch below) are UNRELATED screen
   * coordinates for the window manager, not skin-space, and must NOT
   * be scaled. */
  int x = (int)event->x / mw->scale, y = (int)event->y / mw->scale;

  /* MainWin.pas:2092-2098 - double-click ANYWHERE in the ticker rect
   * toggles Do_Scroll, checked first/exclusively (the original's own
   * `if ssDouble in Shift then ... Exit;` at the very top of
   * FormMouseDown, before any zone routing). */
  if (event->type == GDK_2BUTTON_PRESS && x >= GUI_TICKER_X &&
      x < GUI_TICKER_X + GUI_TICKER_WIDTH && y >= GUI_TICKER_Y &&
      y < GUI_TICKER_Y + GUI_TICKER_LINE_HEIGHT) {
    gui_ticker_toggle_scroll(&mw->ticker);
    gtk_widget_queue_draw(widget);
    return TRUE;
  }

  if (gui_button_hit_test(&mw->but_play, x, y)) {
    mw->but_play.is_pushed = true;
    mw->pressed_button = &mw->but_play;
  } else if (gui_button_hit_test(&mw->but_pause, x, y)) {
    mw->but_pause.is_pushed = true;
    mw->pressed_button = &mw->but_pause;
  } else if (gui_button_hit_test(&mw->but_stop, x, y)) {
    mw->but_stop.is_pushed = true;
    mw->pressed_button = &mw->but_stop;
  } else if (gui_button_hit_test(&mw->but_open, x, y)) {
    mw->but_open.is_pushed = true;
    mw->pressed_button = &mw->but_open;
  } else if (gui_button_hit_test(&mw->but_min, x, y)) {
    mw->but_min.is_pushed = true;
    mw->pressed_button = &mw->but_min;
  } else if (gui_button_hit_test(&mw->but_close, x, y)) {
    mw->but_close.is_pushed = true;
    mw->pressed_button = &mw->but_close;
  } else if (gui_button_hit_test(&mw->but_prev, x, y)) {
    mw->but_prev.is_pushed = true;
    mw->pressed_button = &mw->but_prev;
  } else if (gui_button_hit_test(&mw->but_next, x, y)) {
    mw->but_next.is_pushed = true;
    mw->pressed_button = &mw->but_next;
  } else if (gui_button_hit_test(&mw->but_loop, x, y)) {
    mw->but_loop.is_pushed = true;
    mw->pressed_button = &mw->but_loop;
  } else if (gui_button_hit_test(&mw->but_about, x, y)) {
    mw->but_about.is_pushed = true;
    mw->pressed_button = &mw->but_about;
  } else if (gui_button_hit_test(&mw->but_list, x, y)) {
    mw->but_list.is_pushed = true;
    mw->pressed_button = &mw->but_list;
  } else if (gui_button_hit_test(&mw->but_mixer, x, y)) {
    mw->but_mixer.is_pushed = true;
    mw->pressed_button = &mw->but_mixer;
  } else if (gui_button_hit_test(&mw->but_tools, x, y)) {
    mw->but_tools.is_pushed = true;
    mw->pressed_button = &mw->but_tools;
  } else if (gui_hslider_hit_test(&mw->vol_slider, x, y)) {
    mw->dragging_vol = true;
    gui_hslider_press(&mw->vol_slider, x);
    apply_volume(mw);
  } else if (mw->file_loaded &&
             gui_hslider_hit_test(&mw->progr_slider, x, y)) {
    double frac;
    if (gui_playback_get_progress_fraction(&mw->playback, &frac)) {
      mw->dragging_progr = true;
      gui_hslider_press(&mw->progr_slider, x);
      gui_playback_request_seek(&mw->playback, mw->progr_slider.value);
    } /* else: no known duration for this format (see playback.h's own
       * comment) - the slider stays the cosmetic, non-interactive
       * sweep it always was, same as before this entry. */
  } else if (gui_visualizer_handle_click(&mw->vis, x, y)) {
    /* MainWin.pas: ButSpaClick/ButAmpClick (MIG-0094) - toggled
     * already inside gui_visualizer_handle_click; nothing else to do
     * here besides the queue_draw every branch falls through to. */
  } else if (gui_time_display_handle_click(&mw->time_disp, x, y)) {
    /* MainWin.pas: ButTimeClick (SensTime) - cycled already inside
     * gui_time_display_handle_click; nothing else to do here besides
     * the queue_draw every branch falls through to. */
  } else if (x >= GUI_TICKER_X && x < GUI_TICKER_X + GUI_TICKER_WIDTH &&
             y >= GUI_TICKER_Y && y < GUI_TICKER_Y + GUI_TICKER_LINE_HEIGHT) {
    /* MainWin.pas: MoveScr's own single-click press (a single click,
     * not the double-click handled above) - starts a manual scrub
     * drag, see on_motion/gui_ticker_drag. */
    mw->ticker_dragging = true;
    mw->ticker.dragging = true;
    mw->ticker_drag_last_x = x;
  } else if (x >= mw->drag_x && x < mw->drag_x + mw->drag_w &&
             y >= mw->drag_y && y < mw->drag_y + mw->drag_h) {
    /* gdk_window_begin_move_drag sends a _NET_WM_MOVERESIZE client
     * message the window manager expects to target the TOPLEVEL
     * window - `widget` here is mw->area (a GtkDrawingArea, which has
     * its own child GdkWindow nested inside the toplevel's, even
     * though it visually fills the whole thing), so passing its
     * window instead of mw->window's own toplevel GdkWindow silently
     * no-ops on WMs that don't walk up to find a real toplevel
     * themselves - this was why dragging the title strip did nothing
     * at all (a real bug, not a documented simplification). */
    gdk_window_begin_move_drag(gtk_widget_get_window(mw->window),
                                event->button, (int)event->x_root,
                                (int)event->y_root, event->time);
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
    /* MainWin.pas:952-961 - PlayClick's `if IsPlaying then Exit;` -
     * Play is a no-op once a play session is active, including while
     * paused (IsPlaying stays true across a pause; only the Pause
     * button itself resumes - MainWin.pas doesn't set/clear IsPlaying
     * anywhere in ButPauseClick). Previously this port let a Play press
     * during pause silently resume playback too, a second-order variant
     * of MIG-0100's pause/stop bug found while auditing this logic.
     * Starting a NEW session (not already playing/paused) always
     * rewinds first - see restart_current_song's own comment. */
    if (mw->file_loaded && !mw->playback.thread_started)
      restart_current_song(mw);
  }
  if (mw->but_pause.is_pushed) {
    mw->but_pause.is_pushed = false;
    /* MainWin.pas:971-975 - `if not IsPlaying then begin ButPause.
     * UnPush; exit; end;` - pausing while stopped is a no-op, not "start
     * playback". Real bug fixed here (MIG-0100): this guard was missing
     * entirely, so pressing Pause while stopped set paused=true with no
     * playback thread running to observe it; the NEXT Pause press then
     * saw gui_playback_is_paused() return that stale true and called
     * gui_playback_play(), starting the song from a dead stop. */
    if (mw->file_loaded && mw->playback.thread_started) {
      if (gui_playback_is_paused(&mw->playback))
        gui_playback_play(&mw->playback);
      else
        gui_playback_pause(&mw->playback);
    }
  }
  if (mw->but_stop.is_pushed) {
    mw->but_stop.is_pushed = false;
    gui_mainwin_stop(mw);
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
    /* Part B: save BEFORE quitting, not after - see on_delete_event's
     * own comment just above gui_mainwin_create for why relying solely
     * on a post-gtk_main() call (gui/src/main.c) is unsafe: this is the
     * normal/only realistic close path for this undecorated window
     * (see gtk_window_set_decorated(FALSE) below), so it's the one that
     * matters most to get right. gui_mainwin_save_settings is a cheap,
     * idempotent read-only-until-the-very-end operation - calling it
     * again from main.c after gtk_main() returns (if that path is ever
     * reached) just re-saves the same values, harmlessly. */
    gui_mainwin_save_settings(mw);
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
    /* is_on itself is kept in sync by on_subwin_visibility_changed's
     * "show"/"hide" signal handlers, not set directly here - see its own
     * comment for why. */
    gui_playlist_win_toggle_visible(&mw->plwin);
  }
  if (mw->but_mixer.is_pushed) {
    mw->but_mixer.is_pushed = false;
    gui_mixer_win_toggle_visible(&mw->mixerwin);
  }
  if (mw->but_tools.is_pushed) {
    mw->but_tools.is_pushed = false;
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
  mw->pressed_button = NULL;
  mw->ticker_dragging = false;
  mw->ticker.dragging = false;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean on_motion(GtkWidget* widget, GdkEventMotion* event,
                           gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  /* MIG-0119: same skin-space conversion as on_button_press. */
  int x = (int)event->x / mw->scale, y = (int)event->y / mw->scale;
  if (mw->pressed_button) {
    /* MainWin.pas: FormMouseMove's ButtZoneRoot walk (2276-2282) - see
     * mainwin.h's own comment on pressed_button. */
    mw->pressed_button->is_pushed = gui_button_hit_test(mw->pressed_button, x, y);
    gtk_widget_queue_draw(widget);
  }
  if (mw->ticker_dragging) {
    gui_ticker_drag(&mw->ticker, x - mw->ticker_drag_last_x);
    mw->ticker_drag_last_x = x;
    gtk_widget_queue_draw(widget);
  }
  if (mw->dragging_progr) {
    gui_hslider_drag(&mw->progr_slider, x);
    if (mw->file_loaded)
      gui_playback_request_seek(&mw->playback, mw->progr_slider.value);
    gtk_widget_queue_draw(widget); /* immediate feedback - previously
                                     * missing here (unlike the vol_slider
                                     * branch below), so the thumb only
                                     * caught up on the next ~30ms vis-
                                     * timer tick during a drag, real gap
                                     * found and fixed alongside MIG-0099 */
  }
  if (mw->dragging_vol) {
    gui_hslider_drag(&mw->vol_slider, x);
    apply_volume(mw);
    gtk_widget_queue_draw(widget);
  }
  return TRUE;
}

/* MainWin.pas: VisTimerEvent -> DoVisualisation's AYVisualisation call
 * (MIG-0094) - a separate, faster-cadence timer than on_timer below
 * (30ms, matching VisTimerPeriod's own default, vs on_timer's 200ms). */
static gboolean on_vis_timer(gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  ay_engine* engine = mw->file_loaded ? player_ay_engine(&mw->playback.pair.primary) : NULL;
  uint32_t smp = mw->file_loaded
                     ? (uint32_t)atomic_load(&mw->playback.frames_played)
                     : 0;
  gui_visualizer_tick(&mw->vis, engine, smp);
  gui_ticker_tick(&mw->ticker); /* MainWin.pas: DoVisualisation also
                                  * drives the scroll ticker, same
                                  * 30ms VisTimer - see ticker.h */
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
     * exclusive. */
    ay_chip_type ct = player_chip_type(&mw->playback.pair.primary);
    mw->led_ay.state = (ct == AY_CHIP_TYPE_AY);
    mw->led_ym.state = (ct == AY_CHIP_TYPE_YM);

    /* Led_Stereo, MainWin.pas:1063: `Led_Stereo.State := NumberOfChannels
     * = 1` - despite the name, this LED lights when the OUTPUT IS MONO,
     * not stereo (a real quirk of the original, not a typo here). */
    mw->led_stereo.state = (mw->playback.channels == 1);

    /* MainWin.pas: PlayCurrent's `ButPlay.Switch_On`/`ButPause.Switch_
     * Off` (a play session starting) and ButPauseClick's `ButPause.
     * Switch_On`/`Switch_Off` (pause toggling) - ButPlay stays visibly
     * pushed for the WHOLE play session (not just momentarily during
     * the click), only released by RestoreControls/ButStopClick's own
     * `ButPlay.Switch_Off`/`ButStop.UnPush` on an actual stop; ButPause
     * separately stays pushed only while actually paused. Recomputed
     * fresh from live state every tick here (rather than trying to set
     * is_on at every individual Play/Pause/Stop/natural-end call site,
     * which is exactly the kind of easy-to-miss-one-spot bug already
     * found and fixed elsewhere in this port this session) - matches
     * the same "derive from live state" pattern led_ay/led_ym already
     * use just above. gui_playback::thread_started is true from the
     * moment Play starts the background thread until an explicit Stop
     * joins it (including the natural-end-triggered stop just below),
     * making it the right proxy for "a play session is active" (unlike
     * `file_loaded`, which - correctly - stays true across a Stop, so
     * the next Play/Open doesn't need a fresh file dialog). */
    mw->but_play.is_on = mw->playback.thread_started;
    mw->but_pause.is_on =
        mw->playback.thread_started && gui_playback_is_paused(&mw->playback);

    if (gui_playback_is_finished(&mw->playback)) {
      if (mw->do_loop) {
        restart_current_song(mw); /* keep overrides */
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
    mw->but_play.is_on = false;
    mw->but_pause.is_on = false;
  }
  gtk_widget_queue_draw(mw->area);
  return TRUE; /* keep firing */
}

static void on_realize(GtkWidget* widget, gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  gui_apply_main_window_shape(gtk_widget_get_window(widget), mw->scale);
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

/* MainWin.pas: VolUp/VolDown, shared by the Up/Down key handlers below
 * and on_scroll (MainWin.pas: FormMouseWheelUp/Down - mouse wheel
 * ANYWHERE on the window adjusts volume, no zone restriction, matching
 * a plain TForm.OnMouseWheel handler - this port previously had no
 * scroll handling at all, a real missed feature caught by this entry's
 * own sweep). */
static void vol_up(gui_mainwin* mw) {
  mw->vol_slider.value += 1.0 / mw->vol_slider.w;
  if (mw->vol_slider.value > 1.0) mw->vol_slider.value = 1.0;
  apply_volume(mw);
  gtk_widget_queue_draw(mw->area);
}

static void vol_down(gui_mainwin* mw) {
  mw->vol_slider.value -= 1.0 / mw->vol_slider.w;
  if (mw->vol_slider.value < 0.0) mw->vol_slider.value = 0.0;
  apply_volume(mw);
  gtk_widget_queue_draw(mw->area);
}

static gboolean on_scroll(GtkWidget* widget, GdkEventScroll* event,
                           gpointer data) {
  (void)widget;
  gui_mainwin* mw = (gui_mainwin*)data;
  if (event->direction == GDK_SCROLL_UP) {
    vol_up(mw);
  } else if (event->direction == GDK_SCROLL_DOWN) {
    vol_down(mw);
  }
  return TRUE;
}

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
      gui_playlist_win_toggle_visible(&mw->plwin);
      return TRUE;
    case GDK_g:
    case GDK_G:
      gui_mixer_win_toggle_visible(&mw->mixerwin);
      return TRUE;
    case GDK_p:
    case GDK_P:
      gui_tools_win_toggle_visible(&mw->toolswin);
      return TRUE;
    case GDK_t:
    case GDK_T:
      gui_time_display_click(&mw->time_disp);
      gtk_widget_queue_draw(mw->area);
      return TRUE;
    case GDK_r:
    case GDK_R:
      mw->do_loop = !mw->do_loop;
      mw->but_loop.is_on = mw->do_loop;
      return TRUE;
    case GDK_x:
    case GDK_X:
      /* MainWin.pas:3558's `byte('X'): Push(ButPlay);` - same PlayClick
       * no-op-while-already-playing-or-paused guard, and same rewind-
       * on-new-session behavior, as the mouse Play button - see
       * on_button_release's but_play branch. */
      if (mw->file_loaded && !mw->playback.thread_started)
        restart_current_song(mw);
      return TRUE;
    case GDK_v:
    case GDK_V:
      if (mw->file_loaded) gui_playback_stop(&mw->playback);
      return TRUE;
    case GDK_c:
    case GDK_C:
      /* MainWin.pas:3567-3568 - `byte('C'): Push(ButPause);` runs
       * through the same ButPauseClick as a mouse click, including its
       * `if not IsPlaying then exit` guard - see on_button_release's
       * but_pause branch for the bug this mirrors (MIG-0100). */
      if (mw->file_loaded && mw->playback.thread_started) {
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
      vol_up(mw);
      return TRUE;
    case GDK_Down:
      vol_down(mw);
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
/* Part B safety net: this window has no titlebar (gtk_window_set_
 * decorated(FALSE) below), so the only REALISTIC close path in normal
 * use is the skinned but_close button above (which now saves settings
 * itself before quitting) - but a window manager can still deliver a
 * WM_DELETE_WINDOW client message through other means (a keyboard
 * shortcut, `wmctrl -c`, session logout, etc.), and GTK's own default
 * "delete-event" handling is to call gtk_widget_destroy, which would
 * tear mw->window down (and, via the "destroy" signal below, quit the
 * main loop) WITHOUT ever running gui_mainwin_save_settings - silently
 * losing that session's window-position/tray-mode/volume changes.
 * Hooking "delete-event" here (fired BEFORE any destruction happens,
 * unlike "destroy") saves first, then returns FALSE so GTK's normal
 * destroy-then-quit sequence still proceeds exactly as before. */
static gboolean on_delete_event(GtkWidget* widget, GdkEvent* event,
                                 gpointer data) {
  (void)widget;
  (void)event;
  gui_mainwin_save_settings((gui_mainwin*)data);
  return FALSE; /* allow the default handler to destroy the window */
}

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
  mw->scale = 1; /* MainWin.pas: Scale: integer = 1; - MIG-0119 */

  /* Part B/C settings load (gui/include/gui/settings.h) - see this
   * file's own gui_mainwin_save_settings for the write side. A missing
   * settings file (first-ever launch) just leaves every gui_settings_
   * get_* call below returning its fallback, so this is safe to call
   * unconditionally. */
  gui_settings_load();
  /* MainWin.pas:497 `AutoSaveWindowsPos: boolean = True;` - default on,
   * matching the original's own default. */
  mw->auto_save_windows_pos =
      gui_settings_get_bool("Window", "AutoSaveWindowsPos", true);
  /* MainWin.pas:462 `TrayMode: integer = 0;` (never) is the real
   * Pascal default - deliberately NOT mirrored here: this port's own
   * pre-existing MIG-0073 tray icon has always been shown
   * unconditionally (no mode concept existed before this session), so
   * defaulting to 0 would silently regress every existing user of this
   * port's tray icon into "no tray icon" on their very next launch
   * before they ever visit Tools to notice/change it. Defaulting to 2
   * (always) instead preserves this port's own already-shipped
   * behavior - a documented, deliberate deviation from the Pascal
   * default, not an oversight (see migration_debt.yaml). */
  mw->tray_mode = gui_settings_get_int("Window", "TrayMode", 2);
  /* settings.pas:54 `VolLinear:boolean = False;` / MainWin.pas:498
   * `AutoSaveVolumePos: boolean = False;` - both default off, matching
   * the original's own defaults exactly. */
  mw->sysvol_linear = gui_settings_get_bool("Volume", "VolLinear", false);
  mw->auto_save_volume_pos =
      gui_settings_get_bool("Volume", "AutoSaveVolumePos", false);

  /* Mixer.pas: WOSheet ("Digital Sound" tab, MIG-0130) - settings.pas's
   * own SampleRateDef/SampleBitDef/NumOfChanDef/BufLen_msDef/
   * NumberOfBuffersDef, all matching this port's own PRE-EXISTING
   * hardcoded values exactly (confirmed by the research pass behind
   * this entry) - so these defaults are not a behavior change on their
   * own, only the fact that they're now user-adjustable is. */
  mw->sample_rate = gui_settings_get_int("DigitalSound", "SampleRate", 48000);
  mw->sample_bits = gui_settings_get_int("DigitalSound", "SampleBit", 16);
  mw->default_channels =
      gui_settings_get_int("DigitalSound", "NumberOfChannels", 2);
  mw->buf_len_ms = gui_settings_get_int("DigitalSound", "BufLenMs", 200);
  mw->num_buffers = gui_settings_get_int("DigitalSound", "NumberOfBuffers", 3);
  mw->output_device[0] = '\0';
  {
    char* dev = gui_settings_get_string("DigitalSound", "Device");
    if (dev) {
      strncpy(mw->output_device, dev, sizeof(mw->output_device) - 1);
      g_free(dev);
    }
  }

  if (!gui_skin_load_default(&mw->skin)) {
    fprintf(stderr, "gui: failed to load default skin\n");
    return false;
  }
  init_zones(mw);

  /* Mixer.pas: OpenMixer(saved Path1/Path2/Path3) at startup, then
   * GetSysVolume - gui_mainwin_sysvol_reopen both opens the control and
   * (on success) reads its current hardware level into vol_slider.value,
   * same as the original's own OpenMixer->GetSysVolume->RedrawVolume
   * chain. Runs AFTER init_zones (which sets vol_slider.value's own
   * 1.0 compiled-in default) so a successful open's real hardware level
   * - or the AutoSaveVolumePos override just below - is what actually
   * sticks, not init_zones' default. */
  char* saved_control = gui_settings_get_string("Volume", "MixerControl");
  gui_mainwin_sysvol_reopen(mw, saved_control);
  g_free(saved_control);
  if (mw->auto_save_volume_pos) {
    /* MainWin.pas:4671-4674 - `if (v >= 0) and (v <= VolumeCtrlMax) then
     * VolumeCtrl := v; SetSysVolume;` - a persisted volume, when the
     * CBSvVolPos option is on, overrides whatever GetSysVolume just read
     * and is pushed back out to the hardware, not just displayed. */
    int saved_vol = gui_settings_get_int("Volume", "Volume", -1);
    if (saved_vol >= 0 && saved_vol <= 1000) {
      mw->vol_slider.value = saved_vol / 1000.0;
      if (mw->sysvol)
        gui_alsa_mixer_set_volume(mw->sysvol, mw->vol_slider.value,
                                   mw->sysvol_linear);
    }
  }

  mw->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(mw->window), "ay_emul_c11");
  gtk_window_set_decorated(GTK_WINDOW(mw->window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(mw->window), FALSE);
  gtk_widget_set_size_request(mw->window, MW_WIDTH * mw->scale,
                               MW_HEIGHT * mw->scale);
  g_signal_connect(mw->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(mw->window, "delete-event", G_CALLBACK(on_delete_event),
                    mw);
  g_signal_connect(mw->window, "realize", G_CALLBACK(on_realize), mw);
  g_signal_connect(mw->window, "key-press-event", G_CALLBACK(on_key_press),
                    mw);

  mw->area = gtk_drawing_area_new();
  gtk_widget_set_size_request(mw->area, MW_WIDTH * mw->scale,
                               MW_HEIGHT * mw->scale);
  gtk_widget_add_events(mw->area, GDK_BUTTON_PRESS_MASK |
                                       GDK_BUTTON_RELEASE_MASK |
                                       GDK_POINTER_MOTION_MASK |
                                       GDK_SCROLL_MASK);
  g_signal_connect(mw->area, "expose-event", G_CALLBACK(on_expose), mw);
  g_signal_connect(mw->area, "button-press-event",
                    G_CALLBACK(on_button_press), mw);
  g_signal_connect(mw->area, "button-release-event",
                    G_CALLBACK(on_button_release), mw);
  g_signal_connect(mw->area, "motion-notify-event", G_CALLBACK(on_motion),
                    mw);
  g_signal_connect(mw->area, "scroll-event", G_CALLBACK(on_scroll), mw);

  static const GtkTargetEntry drop_targets[] = {
      {(gchar*)"text/uri-list", 0, 0}};
  gtk_drag_dest_set(mw->area, GTK_DEST_DEFAULT_ALL, drop_targets,
                     G_N_ELEMENTS(drop_targets), GDK_ACTION_COPY);
  g_signal_connect(mw->area, "drag-data-received",
                    G_CALLBACK(on_drag_data_received), mw);

  gtk_container_add(GTK_CONTAINER(mw->window), mw->area);

  /* MainWin.pas:4744-4749 - `Position := poDesigned; if GetDW('MainX',v)
   * then Left := v; if GetDW('MainY',v) then Top := v;` - applied BEFORE
   * the window's first show (gtk_window_move on an unrealized/unmapped
   * window just sets the position it will first appear at, same as
   * setting Left/Top before a Delphi/Lazarus TForm.Show). AdjustFormOnDesktop's
   * own "clamp fully onto the visible desktop" step has no plain GTK2
   * equivalent (no portable multi-monitor geometry query) and is not
   * replicated - a saved position from a since-disconnected monitor may
   * put the window off-screen, same residual risk this port already
   * accepts for the sub-windows below. */
  if (mw->auto_save_windows_pos) {
    int mx = gui_settings_get_int("Window", "MainX", SETTINGS_INT_ABSENT);
    int my = gui_settings_get_int("Window", "MainY", SETTINGS_INT_ABSENT);
    if (mx != SETTINGS_INT_ABSENT && my != SETTINGS_INT_ABSENT)
      gtk_window_move(GTK_WINDOW(mw->window), mx, my);
  }

  gtk_widget_show_all(mw->window);

  mw->vis_period_ms = 30; /* MainWin.pas: VisTimerPeriod's own default -
                            * set before gui_tools_win_create below so
                            * its entry is pre-filled correctly */

  gui_playlist_win_create(&mw->plwin, GTK_WINDOW(mw->window),
                           on_playlist_play, mw);
  /* MainWin.pas:4624-4636 - PLColor/PLFont load side (MIG-0125),
   * applied right after gui_playlist_win_create sets up its own
   * fixed-literal PLColorPl/PLColorPlSel defaults, so a saved value (if
   * any) correctly overrides them - matching this port's own SETTINGS_
   * INT_ABSENT "key not present, leave whatever's already there" idiom
   * used for the window-geometry settings below. Colors are stored as
   * plain 0xRRGGBB ints (this port's own settings.ini, not shared with
   * the real Pascal's own BGR-ordered TColor SaveDW values, so no byte-
   * order concern) - 257 = 65535/255 is the exact 8-bit-to-16-bit GdkColor
   * channel scale. */
  {
    const struct {
      const char* key;
      gui_playlist_color* field;
    } pl_colors[] = {
        {"PLColor", &mw->plwin.text},
        {"PLColorBk", &mw->plwin.back},
        {"PLColorSel", &mw->plwin.sel_text},
        {"PLColorBkSel", &mw->plwin.sel_back},
        {"PLColorPl", &mw->plwin.play_text},
        {"PLColorBkPl", &mw->plwin.play_back},
        {"PLColorPlSel", &mw->plwin.play_sel_text},
        {"PLColorErr", &mw->plwin.err_text},
        {"PLColorErrSel", &mw->plwin.err_sel_text},
    };
    for (size_t i = 0; i < sizeof(pl_colors) / sizeof(pl_colors[0]); i++) {
      int v = gui_settings_get_int("Playlist", pl_colors[i].key,
                                    SETTINGS_INT_ABSENT);
      if (v == SETTINGS_INT_ABSENT) continue;
      pl_colors[i].field->color.red = (guint16)(((v >> 16) & 0xFF) * 257);
      pl_colors[i].field->color.green = (guint16)(((v >> 8) & 0xFF) * 257);
      pl_colors[i].field->color.blue = (guint16)((v & 0xFF) * 257);
      pl_colors[i].field->set = true;
    }
    char* font_name = gui_settings_get_string("Playlist", "Font");
    if (font_name) {
      mw->plwin.font = pango_font_description_from_string(font_name);
      g_free(font_name);
    }
    gui_playlist_win_refresh_colors(&mw->plwin);

    /* MainWin.pas:4386/4621-4622 - `PlayListLoop`/ListLooped (MIG-0127) -
     * `direction` itself is session-only, matching real Pascal's own
     * apparent non-persistence of Direction (see gui_playlist_win's
     * own struct comment). */
    bool looped = gui_settings_get_bool("Playlist", "PlayListLoop", false);
    gui_playlist_win_set_looped(&mw->plwin, looped);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mw->plwin.check_loop),
                                  looped);
  }
  gui_mixer_win_create(&mw->mixerwin, GTK_WINDOW(mw->window), mw);
  gui_tools_win_create(&mw->toolswin, GTK_WINDOW(mw->window), mw);

  /* See on_subwin_visibility_changed's own comment: keeps but_list/
   * but_mixer/but_tools's pushed/unpushed appearance in sync with each
   * window's real visibility, however it changes. */
  g_signal_connect(mw->plwin.window, "show",
                    G_CALLBACK(on_subwin_visibility_changed), mw);
  g_signal_connect(mw->plwin.window, "hide",
                    G_CALLBACK(on_subwin_visibility_changed), mw);
  g_signal_connect(mw->mixerwin.window, "show",
                    G_CALLBACK(on_subwin_visibility_changed), mw);
  g_signal_connect(mw->mixerwin.window, "hide",
                    G_CALLBACK(on_subwin_visibility_changed), mw);
  g_signal_connect(mw->toolswin.window, "show",
                    G_CALLBACK(on_subwin_visibility_changed), mw);
  g_signal_connect(mw->toolswin.window, "hide",
                    G_CALLBACK(on_subwin_visibility_changed), mw);

  /* MainWin.pas:4751-4784 - ListX/ListY/ListW/ListH/ListVis, MixerX/
   * MixerY, ToolsX/ToolsY, applied now that plwin/mixerwin/toolswin all
   * exist (each window's own gtk_window_new already ran inside its
   * *_win_create above; none of them are shown yet - see playlist_win.h/
   * mixer_win.h/tools_win.h's own file comments: "closing" just hides,
   * so every sub-window persists for the process lifetime and starts
   * hidden). gtk_window_move/resize on a not-yet-shown toplevel just
   * sets its first-appearance geometry, same as gtk_window_move on
   * mw->window above. */
  if (mw->auto_save_windows_pos) {
    int x = gui_settings_get_int("Window", "ListX", SETTINGS_INT_ABSENT);
    int y = gui_settings_get_int("Window", "ListY", SETTINGS_INT_ABSENT);
    if (x != SETTINGS_INT_ABSENT && y != SETTINGS_INT_ABSENT)
      gtk_window_move(GTK_WINDOW(mw->plwin.window), x, y);
    int w = gui_settings_get_int("Window", "ListW", SETTINGS_INT_ABSENT);
    int h = gui_settings_get_int("Window", "ListH", SETTINGS_INT_ABSENT);
    if (w != SETTINGS_INT_ABSENT && h != SETTINGS_INT_ABSENT && w > 0 &&
        h > 0)
      gtk_window_resize(GTK_WINDOW(mw->plwin.window), w, h);
    if (gui_settings_get_bool("Window", "ListVis", false)) {
      mw->but_list.is_on = true; /* MainWin.pas:4770-4774's own
                                   * `ButList.Switch_On` */
      gui_playlist_win_toggle_visible(&mw->plwin);
    }

    int mxv = gui_settings_get_int("Window", "MixerX", SETTINGS_INT_ABSENT);
    int myv = gui_settings_get_int("Window", "MixerY", SETTINGS_INT_ABSENT);
    if (mxv != SETTINGS_INT_ABSENT && myv != SETTINGS_INT_ABSENT)
      gtk_window_move(GTK_WINDOW(mw->mixerwin.window), mxv, myv);

    int tx = gui_settings_get_int("Window", "ToolsX", SETTINGS_INT_ABSENT);
    int ty = gui_settings_get_int("Window", "ToolsY", SETTINGS_INT_ABSENT);
    if (tx != SETTINGS_INT_ABSENT && ty != SETTINGS_INT_ABSENT)
      gtk_window_move(GTK_WINDOW(mw->toolswin.window), tx, ty);
  }

  /* MainWin.pas: AddTrayIcon - real behavior loads one of the app's own
   * embedded ICON00-ICON99 Windows icon resources (TrayIconNumber,
   * user-selectable); those are separate assets not present in the
   * .ays skin and not ported (see migration_debt.yaml) - a generic
   * icon-theme lookup is used instead, a documented simplification.
   * Initial visibility now follows mw->tray_mode (Part C, MIG-0073's own
   * follow-up) rather than being unconditionally TRUE - see
   * gui_mainwin_set_tray_mode's own comment for the 3 modes. */
  mw->tray_icon = gtk_status_icon_new_from_icon_name("multimedia-player");
  gtk_status_icon_set_tooltip_text(mw->tray_icon, "AY Emulator");
  gtk_status_icon_set_visible(mw->tray_icon, mw->tray_mode != 0);
  g_signal_connect(mw->tray_icon, "activate", G_CALLBACK(on_tray_activate),
                    mw);

  gui_visualizer_init(&mw->vis);
  gui_time_display_init(&mw->time_disp);
  gui_ticker_init(&mw->ticker);
  mw->ticker_last_index = -1;

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

/* MainWin.pas:3300-3303: `Width := MWWidth * Scale; Height := MWHeight *
 * Scale; Application.QueueAsyncCall(@SetRgn, 0);` - PrepareRgn resizes
 * the form SYNCHRONOUSLY but explicitly DEFERS SetWindowRgn (the shape-
 * mask application) to run asynchronously afterward, once the resize
 * has actually been processed by the widget toolkit - applying the new,
 * larger shape region synchronously (before the underlying window has
 * actually grown) races against the resize and gets combined against
 * the window's still-old size, clipping away everything outside the
 * OLD bounds until something else happens to trigger a re-shape (the
 * real bug this comment's own history records: the window itself grew
 * correctly, but buttons in the newly-added area were invisible/
 * unclickable). g_idle_add is this port's own Application.QueueAsyncCall
 * equivalent - runs once, after the current resize has been processed
 * by the GTK main loop, matching the original's own timing exactly. */
static gboolean apply_shape_idle(gpointer data) {
  gui_mainwin* mw = (gui_mainwin*)data;
  if (gtk_widget_get_realized(mw->window)) {
    gui_apply_main_window_shape(gtk_widget_get_window(mw->window), mw->scale);
  }
  return FALSE; /* G_SOURCE_REMOVE - one-shot, matching QueueAsyncCall */
}

/* Tools.pas: CBDoubleSzClick -> Scale/FrmMain.RecreateRgn (PrepareRgn +
 * ClueRgn) - MIG-0119. ClueRgn's own role (wiring RgnPlay/RgnStop/etc.
 * region HANDLES for hit-testing) has no equivalent needed here - this
 * port's zones.c hit-tests plain skin-space rects directly, and mouse
 * events are converted back to skin-space by dividing by mw->scale
 * (on_button_press/on_motion), so no separate "rebuild the hit-test
 * geometry" step exists to redo. */
void gui_mainwin_set_scale(gui_mainwin* mw, int scale) {
  if (scale != 1 && scale != 2) return;
  if (scale == mw->scale) return; /* MainWin.pas: `if NewS <> Scale` */
  mw->scale = scale;

  gtk_widget_set_size_request(mw->area, MW_WIDTH * scale, MW_HEIGHT * scale);
  if (gtk_widget_get_realized(mw->window)) {
    /* gtk_window_set_resizable(FALSE) (set once at window creation)
     * locks the WM-facing min/max size hints to whatever was in effect
     * at the time - some window managers cache those hints and won't
     * honor a later gtk_window_resize while they're still pinned to
     * the OLD fixed size, even though GTK's own internal widget
     * allocation eventually catches up regardless (this port's own
     * `mw->area`, the actual drawing surface, would then stay clipped
     * to the old size while the skin/buttons paint at the new scale
     * "underneath" - matching the exact symptom reported: the
     * rendered content looks correctly doubled, but anything past the
     * old window's own bottom/right edge is outside what's actually
     * visible/clickable). Briefly allowing resize for the duration of
     * this call sends a fresh, correct min=max=new-size hint on
     * re-lock, robust across window managers rather than relying on
     * this session's own WM happening to cooperate. */
    gtk_window_set_resizable(GTK_WINDOW(mw->window), TRUE);
    gtk_window_resize(GTK_WINDOW(mw->window), MW_WIDTH * scale,
                       MW_HEIGHT * scale);
    gtk_widget_set_size_request(mw->window, MW_WIDTH * scale,
                                 MW_HEIGHT * scale);
    gtk_window_set_resizable(GTK_WINDOW(mw->window), FALSE);
    g_idle_add(apply_shape_idle, mw);
  } else {
    gtk_widget_set_size_request(mw->window, MW_WIDTH * scale, MW_HEIGHT * scale);
  }
  gtk_widget_queue_draw(mw->area);
}

void gui_mainwin_destroy(gui_mainwin* mw) {
  if (mw->timer_id) g_source_remove(mw->timer_id);
  if (mw->vis_timer_id) g_source_remove(mw->vis_timer_id);
  if (mw->file_loaded) gui_playback_free(&mw->playback);
  gui_playlist_win_destroy(&mw->plwin);
  gui_mixer_win_destroy(&mw->mixerwin);
  gui_tools_win_destroy(&mw->toolswin);
  if (mw->tray_icon) g_object_unref(mw->tray_icon);
  if (mw->sysvol) gui_alsa_mixer_close(mw->sysvol);
  gui_skin_free(&mw->skin);
}

/* See gui/include/gui/mainwin.h's own comment for the full contract. */
void gui_mainwin_sysvol_reopen(gui_mainwin* mw, const char* selem_name) {
  if (mw->sysvol) {
    gui_alsa_mixer_close(mw->sysvol);
    mw->sysvol = NULL;
  }
  mw->sysvol = gui_alsa_mixer_open(NULL, selem_name);
  if (mw->sysvol) {
    double v;
    if (gui_alsa_mixer_get_volume(mw->sysvol, &v, mw->sysvol_linear)) {
      mw->vol_slider.value = v;
      if (mw->file_loaded)
        gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
    }
  }
  if (mw->area) gtk_widget_queue_draw(mw->area);
}

void gui_mainwin_set_sysvol_linear(gui_mainwin* mw, bool linear) {
  mw->sysvol_linear = linear;
  if (mw->sysvol) {
    double v;
    if (gui_alsa_mixer_get_volume(mw->sysvol, &v, mw->sysvol_linear)) {
      mw->vol_slider.value = v;
      if (mw->file_loaded)
        gui_playback_set_volume(&mw->playback, mw->vol_slider.value);
    }
  }
  if (mw->area) gtk_widget_queue_draw(mw->area);
}

void gui_mainwin_set_tray_mode(gui_mainwin* mw, int mode) {
  if (mode < 0 || mode > 2) return;
  if (mode == mw->tray_mode) return; /* Set_TrayMode2's own `if TrayMode
                                       * = TM then Exit` guard */
  mw->tray_mode = mode;
  switch (mode) {
    case 0: /* never */
      gtk_status_icon_set_visible(mw->tray_icon, FALSE);
      break;
    case 1: /* minimize - real behavior is Windows-only (see mainwin.h's
             * own comment) - falls through to "always" here. */
      /* fall through */
    case 2: /* always */
      gtk_status_icon_set_visible(mw->tray_icon, TRUE);
      break;
  }
}

void gui_mainwin_stop(gui_mainwin* mw) {
  if (mw->file_loaded) gui_playback_stop(&mw->playback);
}

/* See gui/include/gui/mainwin.h's own comment for the full contract and
 * why this is NOT called from gui_mainwin_destroy. */
void gui_mainwin_save_settings(gui_mainwin* mw) {
  if (!GTK_IS_WIDGET(mw->window)) return; /* window already gone (e.g. a
                                            * WM-initiated delete-event
                                            * ran the destroy->gtk_main_
                                            * quit chain before gtk_main()
                                            * returned) - nothing left to
                                            * query a position from. */

  /* mw->auto_save_windows_pos/auto_save_volume_pos only hold the
   * value gui_mainwin_create loaded at STARTUP (needed then, since
   * gui_tools_win doesn't exist yet at that point) - by save time the
   * Tools window's own checkboxes (toggled live by the user, with no
   * other side effect of their own - see tools_win.h's own comment)
   * are the real, current source of truth, same lazy-read pattern this
   * port already uses for e.g. Force_Loop elsewhere. */
  bool auto_save_win = gui_tools_win_auto_save_windows_pos(&mw->toolswin);
  bool auto_save_vol = gui_mixer_win_auto_save_volume_pos(&mw->mixerwin);

  gui_settings_set_bool("Window", "AutoSaveWindowsPos", auto_save_win);
  gui_settings_set_int("Window", "TrayMode", mw->tray_mode);
  gui_settings_set_bool("Volume", "VolLinear", mw->sysvol_linear);
  gui_settings_set_bool("Volume", "AutoSaveVolumePos", auto_save_vol);
  if (mw->sysvol) {
    gui_settings_set_string("Volume", "MixerControl",
                             gui_alsa_mixer_selem_name(mw->sysvol));
  }

  /* Mixer.pas: WOSheet ("Digital Sound" tab, MIG-0130) - see this
   * function's own gui_mainwin_create load-side comment. */
  gui_settings_set_int("DigitalSound", "SampleRate", mw->sample_rate);
  gui_settings_set_int("DigitalSound", "SampleBit", mw->sample_bits);
  gui_settings_set_int("DigitalSound", "NumberOfChannels",
                        mw->default_channels);
  gui_settings_set_int("DigitalSound", "BufLenMs", mw->buf_len_ms);
  gui_settings_set_int("DigitalSound", "NumberOfBuffers", mw->num_buffers);
  gui_settings_set_string("DigitalSound", "Device", mw->output_device);

  if (auto_save_vol) {
    /* MainWin.pas:4353-4354 - `if AutoSaveVolumePos then SaveDW('Volume',
     * VolumeCtrl);` - stored here as a 0-1000 integer (this port's
     * vol_slider.value is already a 0.0-1.0 fraction, unlike VolumeCtrl's
     * own raw pixel-position integer, so this just picks a fixed
     * resolution instead of reusing a slider-width-dependent range). */
    gui_settings_set_int("Volume", "Volume",
                          (int)(mw->vol_slider.value * 1000.0 + 0.5));
  }

  if (auto_save_win) {
    int x, y, w, h;
    gtk_window_get_position(GTK_WINDOW(mw->window), &x, &y);
    gui_settings_set_int("Window", "MainX", x);
    gui_settings_set_int("Window", "MainY", y);

    if (GTK_IS_WIDGET(mw->plwin.window)) {
      gtk_window_get_position(GTK_WINDOW(mw->plwin.window), &x, &y);
      gui_settings_set_int("Window", "ListX", x);
      gui_settings_set_int("Window", "ListY", y);
      gtk_window_get_size(GTK_WINDOW(mw->plwin.window), &w, &h);
      gui_settings_set_int("Window", "ListW", w);
      gui_settings_set_int("Window", "ListH", h);
      gui_settings_set_bool("Window", "ListVis",
                             gtk_widget_get_visible(mw->plwin.window));
    }

    if (GTK_IS_WIDGET(mw->mixerwin.window)) {
      gtk_window_get_position(GTK_WINDOW(mw->mixerwin.window), &x, &y);
      gui_settings_set_int("Window", "MixerX", x);
      gui_settings_set_int("Window", "MixerY", y);
    }

    /* MainWin.pas:4367-4373 - ToolsX/ToolsY are only overwritten while
     * the Tools window is actually visible (`if ButTools.Is_On then...`)
     * - otherwise whatever was saved last time is left untouched rather
     * than being clobbered with a stale hidden-window position. */
    if (GTK_IS_WIDGET(mw->toolswin.window) &&
        gtk_widget_get_visible(mw->toolswin.window)) {
      gtk_window_get_position(GTK_WINDOW(mw->toolswin.window), &x, &y);
      gui_settings_set_int("Window", "ToolsX", x);
      gui_settings_set_int("Window", "ToolsY", y);
    }
  }

  /* MainWin.pas:4387-4399 - PLColor/PLFont save side (MIG-0125). Only
   * a `.set` color is written - an unset one (still native-theme) has
   * nothing meaningful to persist, and NOT writing it leaves whatever
   * was there from a previous session's own explicit pick untouched
   * rather than overwriting it with "absent" - see this function's own
   * gui_mainwin_create load-side comment for the matching read half. */
  {
    const struct {
      const char* key;
      const gui_playlist_color* field;
    } pl_colors[] = {
        {"PLColor", &mw->plwin.text},
        {"PLColorBk", &mw->plwin.back},
        {"PLColorSel", &mw->plwin.sel_text},
        {"PLColorBkSel", &mw->plwin.sel_back},
        {"PLColorPl", &mw->plwin.play_text},
        {"PLColorBkPl", &mw->plwin.play_back},
        {"PLColorPlSel", &mw->plwin.play_sel_text},
        {"PLColorErr", &mw->plwin.err_text},
        {"PLColorErrSel", &mw->plwin.err_sel_text},
    };
    for (size_t i = 0; i < sizeof(pl_colors) / sizeof(pl_colors[0]); i++) {
      if (!pl_colors[i].field->set) continue;
      int v = ((pl_colors[i].field->color.red / 257) << 16) |
              ((pl_colors[i].field->color.green / 257) << 8) |
              (pl_colors[i].field->color.blue / 257);
      gui_settings_set_int("Playlist", pl_colors[i].key, v);
    }
    if (mw->plwin.font) {
      char* font_name = pango_font_description_to_string(mw->plwin.font);
      gui_settings_set_string("Playlist", "Font", font_name);
      g_free(font_name);
    }
    gui_settings_set_bool("Playlist", "PlayListLoop", mw->plwin.looped);
  }

  gui_settings_save();
}
