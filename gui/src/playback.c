#define _POSIX_C_SOURCE 200809L /* for nanosleep, under strict -std=c11 */
#include "gui/playback.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "player/alsa_output.h"

#define GUI_PLAYBACK_BUF_FRAMES 2048

static const char* basename_of(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

/* CP1251->UTF-8, matching settings.pas's non-Windows CodePageDef
 * default (see ay_file.h's file comment - this port doesn't expose the
 * user-configurable codepage setting, it's hardcoded to CP1251). Empty
 * input or a failed conversion (a malformed/non-CP1251 byte sequence)
 * both result in an empty output string - never raw untranscoded bytes,
 * since GTK widgets require valid UTF-8 (see playback.h's own comment). */
static void cp1251_to_utf8(const char* raw, char* out, size_t cap) {
  out[0] = '\0';
  if (!raw || raw[0] == '\0' || cap == 0) return;
  gsize bytes_written = 0;
  gchar* converted =
      g_convert(raw, -1, "UTF-8", "CP1251", NULL, &bytes_written, NULL);
  if (converted) {
    strncpy(out, converted, cap - 1);
    out[cap - 1] = '\0';
    g_free(converted);
  }
}

/* Shared by gui_playback_load_song (initial load) and do_seek's
 * backward-seek reload path (seeking back to before the current
 * position has no choice but to restart decoding from tick 0 - chip
 * state can only move forward, same as Players.pas's RerollMusic). */
static bool load_player_bytes(player* p, const char* path, int sample_rate,
                               int song_index) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
  long size = ftell(f);
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
  uint8_t* data = (uint8_t*)malloc((size_t)size);
  if (!data) { fclose(f); return false; }
  size_t read = fread(data, 1, (size_t)size, f);
  fclose(f);
  if (read != (size_t)size) { free(data); return false; }

  player_status st = player_load_song(p, path, data, read, sample_rate,
                                       song_index);
  free(data);
  return st == PLAYER_OK;
}

/* Players.pas: RerollMusic's IsZ80EmuFileType/FT.VTX/FT.YM branches
 * (14099-14224) - seek by decoding forward tick-by-tick, discarding the
 * audio, until Global_Tick_Counter reaches the target; if the target is
 * BEHIND the current position, reset (reload from scratch) and decode
 * forward from tick 0 instead, since chip/register state is inherently
 * sequential and can't be un-advanced. The original swaps in a lighter
 * register-only "Converter" OutProc during this loop to skip full audio
 * synthesis for speed; this port doesn't have that fast path and just
 * calls the same player_make_buffer() normal playback already uses,
 * discarding its output - correct either way (chip end-state after the
 * loop is identical), just not as CPU-optimized. Runs entirely on the
 * playback thread (see playback.h's own comment on why). */
static void do_seek(gui_playback* pb, int64_t target_tick) {
  int16_t scratch[GUI_PLAYBACK_BUF_FRAMES * 2];
  int64_t counter, max;
  if (!player_get_tick_position(&pb->p, &counter, &max)) return;

  if (target_tick < counter) {
    player p2;
    if (!load_player_bytes(&p2, pb->path, pb->sample_rate, pb->song_index))
      return; /* leave playback as-is rather than losing it on a failed
                * reload */
    player_free(&pb->p);
    pb->p = p2;
    atomic_store(&pb->frames_played, 0);
    counter = 0;
  }

  while (counter < target_tick) {
    int n = player_make_buffer(&pb->p, scratch, GUI_PLAYBACK_BUF_FRAMES);
    if (n <= 0) break; /* song ended before reaching the target */
    atomic_fetch_add(&pb->frames_played, n);
    if (!player_get_tick_position(&pb->p, &counter, &max)) break;
  }
}

static void* playback_thread_main(void* arg) {
  gui_playback* pb = (gui_playback*)arg;
  int16_t buf[GUI_PLAYBACK_BUF_FRAMES * 2];

  while (!atomic_load(&pb->stop_requested)) {
    /* atomic_exchange, not load-then-store: consumes the request BEFORE
     * do_seek runs, so a NEW seek requested by the GTK thread while
     * do_seek is still executing (a real possibility - a seek can take
     * multiple on_timer ticks for a long forward jump) sets the flag
     * true again and gets picked up on the next loop iteration, instead
     * of being silently clobbered by an unconditional `store(false)`
     * after the OLD seek finishes (a real bug caught during testing:
     * two closely-spaced seek requests lost the second one). */
    if (atomic_exchange(&pb->seek_requested, false)) {
      do_seek(pb, atomic_load(&pb->seek_target_tick));
    }

    if (atomic_load(&pb->paused)) {
      struct timespec ts = {0, 20000000L}; /* 20ms - usleep is removed as
                                             * of POSIX.1-2008, nanosleep
                                             * is the current equivalent */
      nanosleep(&ts, NULL);
      continue;
    }

    int n = player_make_buffer(&pb->p, buf, GUI_PLAYBACK_BUF_FRAMES);
    if (n <= 0 || player_real_end_all(&pb->p)) {
      atomic_store(&pb->finished, true);
      break;
    }

    double vol = pb->volume; /* see playback.h's threading-model note */
    if (vol < 0.999) {
      for (int i = 0; i < n * 2; i++) {
        buf[i] = (int16_t)(buf[i] * vol);
      }
    }

    alsa_output_write((alsa_output*)pb->alsa_out, buf, n);
    atomic_fetch_add(&pb->frames_played, n);
  }
  return NULL;
}

bool gui_playback_load_song(gui_playback* pb, const char* path,
                             int sample_rate, int song_index) {
  memset(pb, 0, sizeof(*pb));
  pb->volume = 1.0;

  if (!load_player_bytes(&pb->p, path, sample_rate, song_index)) return false;

  alsa_output* out = NULL;
  if (alsa_output_open(&out, 2, sample_rate) != ALSA_OUTPUT_OK) {
    player_free(&pb->p);
    memset(pb, 0, sizeof(*pb));
    return false;
  }

  pb->loaded = true;
  pb->sample_rate = sample_rate;
  pb->alsa_out = out;
  pb->volume = 1.0;
  pb->song_index = song_index;
  pb->song_count = player_song_count(&pb->p);
  strncpy(pb->title, basename_of(path), sizeof(pb->title) - 1);
  strncpy(pb->path, path, sizeof(pb->path) - 1);

  {
    char raw_author[256], raw_title[256], raw_comment[256];
    player_get_metadata_raw(&pb->p, raw_author, sizeof(raw_author),
                             raw_title, sizeof(raw_title), raw_comment,
                             sizeof(raw_comment));
    cp1251_to_utf8(raw_author, pb->meta_author, sizeof(pb->meta_author));
    cp1251_to_utf8(raw_title, pb->meta_title, sizeof(pb->meta_title));
    cp1251_to_utf8(raw_comment, pb->meta_comment, sizeof(pb->meta_comment));
  }
  return true;
}

bool gui_playback_load(gui_playback* pb, const char* path, int sample_rate) {
  return gui_playback_load_song(pb, path, sample_rate, 0);
}

void gui_playback_play(gui_playback* pb) {
  if (!pb->loaded) return;
  atomic_store(&pb->paused, false);
  if (!pb->thread_started) {
    atomic_store(&pb->stop_requested, false);
    pthread_create(&pb->thread, NULL, playback_thread_main, pb);
    pb->thread_started = true;
  }
}

void gui_playback_pause(gui_playback* pb) {
  atomic_store(&pb->paused, true);
}

bool gui_playback_is_paused(const gui_playback* pb) {
  return atomic_load(&pb->paused);
}

bool gui_playback_is_finished(const gui_playback* pb) {
  return atomic_load(&pb->finished);
}

void gui_playback_stop(gui_playback* pb) {
  if (pb->thread_started) {
    atomic_store(&pb->stop_requested, true);
    pthread_join(pb->thread, NULL);
    pb->thread_started = false;
  }
  atomic_store(&pb->paused, false);
  atomic_store(&pb->finished, false);
  atomic_store(&pb->frames_played, 0);
}

void gui_playback_set_volume(gui_playback* pb, double volume) {
  if (volume < 0.0) volume = 0.0;
  if (volume > 1.0) volume = 1.0;
  pb->volume = volume;
}

double gui_playback_position_seconds(const gui_playback* pb) {
  if (pb->sample_rate <= 0) return 0.0;
  return (double)atomic_load(&pb->frames_played) / pb->sample_rate;
}

/* Both functions below read pb->p's tick-position fields from the GTK
 * thread while the playback thread may concurrently be mutating them
 * inside player_make_buffer() - a benign, accepted data race (same
 * class as `volume`/Mixer's index_al etc., see this file's and
 * mixer_win.c's own comments): global_tick_counter is a simple integer
 * counter, so a torn read is, at worst, one frame's progress-bar value
 * or one seek target being off by a negligible amount - never a crash. */

bool gui_playback_get_progress_fraction(const gui_playback* pb,
                                         double* fraction) {
  if (!pb->loaded) return false;
  int64_t counter, max;
  if (!player_get_tick_position(&pb->p, &counter, &max) || max <= 0)
    return false;
  double f = (double)counter / (double)max;
  if (f < 0.0) f = 0.0;
  if (f > 1.0) f = 1.0;
  *fraction = f;
  return true;
}

void gui_playback_request_seek(gui_playback* pb, double fraction) {
  if (!pb->loaded) return;
  int64_t counter, max;
  if (!player_get_tick_position(&pb->p, &counter, &max) || max <= 0) return;
  if (fraction < 0.0) fraction = 0.0;
  if (fraction > 1.0) fraction = 1.0;
  atomic_store(&pb->seek_target_tick, (int64_t)(fraction * (double)max));
  atomic_store(&pb->seek_requested, true);
}

double gui_playback_duration_seconds(const gui_playback* pb) {
  if (!pb->loaded) return 0.0;
  int64_t counter, max;
  if (!player_get_tick_position(&pb->p, &counter, &max) || max <= 0)
    return 0.0;
  double spt = player_get_seconds_per_tick(&pb->p);
  if (spt <= 0.0) return 0.0;
  return (double)max * spt;
}

bool gui_playback_request_seek_seconds(gui_playback* pb, double seconds) {
  double duration = gui_playback_duration_seconds(pb);
  if (duration <= 0.0) return false;
  gui_playback_request_seek(pb, seconds / duration);
  return true;
}

void gui_playback_free(gui_playback* pb) {
  gui_playback_stop(pb);
  if (pb->alsa_out) alsa_output_close((alsa_output*)pb->alsa_out);
  if (pb->loaded) player_free(&pb->p);
  memset(pb, 0, sizeof(*pb));
}
