#define _POSIX_C_SOURCE 200809L /* for nanosleep, under strict -std=c11 */
#include "gui/playback.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ay_player/alsa_output.h"

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
  if (!player_get_tick_position(&pb->pair.primary, &counter, &max)) return;

  if (target_tick < counter) {
    /* MIG-0112: reload the WHOLE pair (both sides, if pb->has_ts_pair),
     * not just primary - chip/register state can only move forward, and
     * that's now true for secondary's own state too when paired, not
     * just primary's. player_pair_load_song takes pre-read bytes, not a
     * path to open itself (matching player_load_song's own layering),
     * so read the file here first. */
    uint8_t* data = NULL;
    size_t size = 0;
    FILE* f = fopen(pb->path, "rb");
    if (f) {
      if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz >= 0 && fseek(f, 0, SEEK_SET) == 0) {
          data = (uint8_t*)malloc((size_t)sz);
          if (data && fread(data, 1, (size_t)sz, f) == (size_t)sz) {
            size = (size_t)sz;
          } else {
            free(data);
            data = NULL;
          }
        }
      }
      fclose(f);
    }
    if (!data) return; /* leave playback as-is rather than losing it on a
                         * failed reload */

    player_pair pair2;
    player_status st = player_pair_load_song(
        &pair2, pb->path, data, size, pb->has_ts_pair ? pb->path : NULL,
        pb->has_ts_pair ? data : NULL, pb->has_ts_pair ? size : 0,
        pb->sample_rate, pb->song_index, pb->is_ste);
    free(data);
    if (st != PLAYER_OK) return;
    player_set_number_of_channels(&pair2.primary, pb->channels);
    player_set_sample_bits(&pair2.primary, pb->bits_per_sample);
    player_pair_free(&pb->pair);
    pb->pair = pair2;
    atomic_store(&pb->frames_played, 0);
    counter = 0;
  }

  /* MIG-0017 update: for SNDH, skip the generic decode-and-discard loop
   * below entirely in favor of Atari_SeekTo's own real algorithm (no
   * audio synthesis during the whole forward jump) - see player.h's own
   * player_seek_fast_forward comment. Returns false (leaving `counter`
   * untouched) for every other format, which then falls through to the
   * unchanged generic loop. frames_played is set directly (not
   * incrementally accumulated, since the fast path never generates any
   * frames to count) from the real seconds-per-tick rate, so the
   * elapsed-time display stays consistent with the new position. */
  if (player_seek_fast_forward(&pb->pair.primary, target_tick)) {
    double secs_per_tick = player_get_seconds_per_tick(&pb->pair.primary);
    if (secs_per_tick > 0.0) {
      atomic_store(&pb->frames_played,
                   (int64_t)(target_tick * secs_per_tick * pb->sample_rate));
    }
    return;
  }

  while (counter < target_tick) {
    int n = player_pair_make_buffer(&pb->pair, scratch, GUI_PLAYBACK_BUF_FRAMES);
    if (n <= 0) break; /* song ended before reaching the target */
    atomic_fetch_add(&pb->frames_played, n);
    if (!player_get_tick_position(&pb->pair.primary, &counter, &max)) break;
  }
}

static void* playback_thread_main(void* arg) {
  gui_playback* pb = (gui_playback*)arg;
  int16_t buf[GUI_PLAYBACK_BUF_FRAMES * 2]; /* sized for the stereo (2
                                              * int16/frame) worst case;
                                              * only the first n*channels
                                              * entries are used/valid for
                                              * a given call, same as
                                              * do_seek's own scratch
                                              * buffer below */

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

    int n = player_pair_make_buffer(&pb->pair, buf, GUI_PLAYBACK_BUF_FRAMES);
    if (n <= 0 || player_pair_real_end_all(&pb->pair)) {
      atomic_store(&pb->finished, true);
      break;
    }

    double vol = pb->volume; /* see playback.h's threading-model note */
    if (vol < 0.999) {
      /* MIG-0130: an 8-bit-per-sample load packs UNSIGNED, 128-centered
       * uint8_t samples into this same `buf` memory (see ay_synthesizer_
       * stereo16's own doc comment on the aliasing convention) - scaling
       * around the 128 silence point, not 0, matches that width; 16-bit
       * stays the pre-existing signed-around-0 int16_t scale. */
      if (pb->bits_per_sample == 8) {
        uint8_t* b8 = (uint8_t*)buf;
        for (int i = 0; i < n * pb->channels; i++) {
          b8[i] = (uint8_t)(128 + (int)(((int)b8[i] - 128) * vol));
        }
      } else {
        for (int i = 0; i < n * pb->channels; i++) {
          buf[i] = (int16_t)(buf[i] * vol);
        }
      }
    }

    alsa_output_write((alsa_output*)pb->alsa_out, buf, n);
    atomic_fetch_add(&pb->frames_played, n);
  }
  return NULL;
}

bool gui_playback_load_song(gui_playback* pb, const char* path,
                             int sample_rate, int song_index, int channels,
                             bool ts_pair, bool is_ste, const char* device,
                             int bits_per_sample, int buf_len_ms,
                             int num_buffers) {
  memset(pb, 0, sizeof(*pb));
  pb->volume = 1.0;
  if (channels != 1 && channels != 2) channels = 2; /* MainWin.pas:1795 */
  if (bits_per_sample != 8 && bits_per_sample != 16)
    bits_per_sample = 16; /* MainWin.pas:1776's implicit range */

  /* MIG-0112: player_pair_load_song takes pre-read bytes twice (once per
   * side), matching player_load_song's own layering. This port's own
   * .ayl pairing always reuses the SAME path for both sides (see gui_
   * playlist_entry::has_ts_pair's own comment), so read it once and
   * pass the same buffer twice rather than opening the file twice. */
  uint8_t* data = NULL;
  size_t size = 0;
  {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    data = (uint8_t*)malloc((size_t)sz);
    if (!data) { fclose(f); return false; }
    size_t read = fread(data, 1, (size_t)sz, f);
    fclose(f);
    if (read != (size_t)sz) { free(data); return false; }
    size = (size_t)sz;
  }

  player_status st = player_pair_load_song(
      &pb->pair, path, data, size, ts_pair ? path : NULL,
      ts_pair ? data : NULL, ts_pair ? size : 0, sample_rate, song_index,
      is_ste);
  free(data);
  if (st != PLAYER_OK) return false;
  pb->has_ts_pair = ts_pair;
  pb->is_ste = is_ste; /* MIG-0121: remembered so do_seek's own backward-
                         * seek reload (below) re-loads with the SAME
                         * is_ste this file was originally opened with,
                         * not always-true. */

  /* MainWin.pas:1793-1798, Set_Stereo - load-time only (see playback.h's
   * own comment); applied before the first gui_playback_play call, same
   * ordering MIG-0088's chip-type/frequency overrides already use. Only
   * primary needs this - player_pair_make_buffer always dispatches
   * through primary's own shared ay_engine (see player.h). */
  player_set_number_of_channels(&pb->pair.primary, channels);
  /* Mixer.pas: RBBt16Click/RBBt8Click's Set_Sample_Bit (MIG-0130) - see
   * player_set_sample_bits's own doc comment. Only primary needs this,
   * same rationale as player_set_number_of_channels above. */
  player_set_sample_bits(&pb->pair.primary, bits_per_sample);

  alsa_output* out = NULL;
  if (alsa_output_open(&out, device, channels, sample_rate, bits_per_sample,
                        buf_len_ms, num_buffers) != ALSA_OUTPUT_OK) {
    player_pair_free(&pb->pair);
    memset(pb, 0, sizeof(*pb));
    return false;
  }

  pb->loaded = true;
  pb->sample_rate = sample_rate;
  pb->bits_per_sample = bits_per_sample;
  pb->alsa_out = out;
  pb->volume = 1.0;
  pb->song_index = song_index;
  pb->channels = channels;
  pb->song_count = player_song_count(&pb->pair.primary);
  strncpy(pb->title, basename_of(path), sizeof(pb->title) - 1);
  strncpy(pb->path, path, sizeof(pb->path) - 1);

  {
    char raw_author[256], raw_title[256], raw_comment[256];
    player_get_metadata_raw(&pb->pair.primary, raw_author, sizeof(raw_author),
                             raw_title, sizeof(raw_title), raw_comment,
                             sizeof(raw_comment));
    cp1251_to_utf8(raw_author, pb->meta_author, sizeof(pb->meta_author));
    cp1251_to_utf8(raw_title, pb->meta_title, sizeof(pb->meta_title));
    cp1251_to_utf8(raw_comment, pb->meta_comment, sizeof(pb->meta_comment));
  }
  return true;
}

bool gui_playback_is_ts_paired(const gui_playback* pb) {
  return pb->loaded && pb->pair.active;
}

bool gui_playback_load(gui_playback* pb, const char* path, int sample_rate) {
  return gui_playback_load_song(pb, path, sample_rate, 0, 2, false, true,
                                 NULL, 16, 200, 3);
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

/* Both functions below read pb->pair.primary's tick-position fields from the GTK
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
  if (!player_get_tick_position(&pb->pair.primary, &counter, &max) || max <= 0)
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
  if (!player_get_tick_position(&pb->pair.primary, &counter, &max) || max <= 0) return;
  if (fraction < 0.0) fraction = 0.0;
  if (fraction > 1.0) fraction = 1.0;
  atomic_store(&pb->seek_target_tick, (int64_t)(fraction * (double)max));
  atomic_store(&pb->seek_requested, true);
}

double gui_playback_duration_seconds(const gui_playback* pb) {
  if (!pb->loaded) return 0.0;
  int64_t counter, max;
  if (!player_get_tick_position(&pb->pair.primary, &counter, &max) || max <= 0)
    return 0.0;
  double spt = player_get_seconds_per_tick(&pb->pair.primary);
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
  if (pb->loaded) player_pair_free(&pb->pair);
  memset(pb, 0, sizeof(*pb));
}
