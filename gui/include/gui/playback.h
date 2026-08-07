/* Background-thread playback driver wrapping engine/ay_engine/player.h +
 * the existing ALSA output wrapper (tools/ay_player/include/player/
 * alsa_output.h, reused as-is - the GUI's Makefile compiles that same
 * .c file directly, no code duplicated, no promotion needed since it
 * has zero engine/ dependency).
 *
 * Pause/resume/position/volume are ALL implemented here, at the
 * caller-driving-loop level, not inside engine/ - see the approved plan
 * (please-port-ay-emul-to-vectorized-sutton.md): the engine's own
 * player_make_buffer is a pure pull-based decode call with no concept
 * of pause or playback rate, so a background thread that calls it
 * repeatedly is the natural place for all of this, requiring zero
 * changes to any of the 18 already oracle-validated format decoders.
 *
 * Thread-safety: `paused`/`stop_requested`/`finished`/`frames_played`
 * use C11 atomics (single-writer-per-field: the GTK thread writes
 * paused/stop_requested, the playback thread writes finished/
 * frames_played, both threads only ever read the other's fields) - no
 * mutex needed for these. `volume` is a plain double, written only by
 * the GTK thread (slider drag) and read once per buffer by the
 * playback thread - a benign, accepted data race for a live UI
 * parameter with no correctness requirement on individual buffers
 * (same practice many real audio applications use for a volume knob;
 * not audio-correctness-critical data the way register state is).
 */
#ifndef GUI_PLAYBACK_H
#define GUI_PLAYBACK_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "ay_engine/player.h"

typedef struct gui_playback {
  player p;
  bool loaded;
  int sample_rate;
  char title[256]; /* filename, always set - falls back to this when
                     * meta_title is empty (most formats have no
                     * metadata at all - see player_get_metadata_raw) */
  char meta_author[256]; /* UTF-8, converted from the engine's raw
                           * CP1251 (player_get_metadata_raw) via
                           * g_convert - empty if the format/file has
                           * none, or if the conversion failed (a
                           * malformed/non-CP1251 string; falls back to
                           * empty rather than raw untranscoded bytes,
                           * since GTK widgets require valid UTF-8) */
  char meta_title[256];   /* per-song title, same conversion */
  char meta_comment[256];
  char path[1024]; /* kept so ButNext/ButPrev (subsong navigation) can
                     * reload the same file at a different song_index -
                     * player itself has no "reload with new song_index
                     * in place" call, only load-from-scratch */
  int song_index;
  int song_count; /* player_song_count() as of the last successful load */

  void* alsa_out; /* alsa_output*, opaque here to avoid pulling ALSA
                    * headers into every gui/ translation unit that
                    * includes playback.h */

  pthread_t thread;
  bool thread_started;

  atomic_bool paused;
  atomic_bool stop_requested;
  atomic_bool finished; /* natural end-of-song or decode error */
  atomic_int_least64_t frames_played;
  double volume; /* 0.0-1.0, see file comment on its threading model */

  /* Seeking (Players.pas: RerollMusic, MIG-0079/MIG-0100/MIG-0101) -
   * only meaningful for formats player_get_tick_position() supports
   * (AY/YM/VTX/SNDH/PT3 - see its own comment for exactly why only
   * these five so far). `seek_target_tick`
   * is set by the GTK thread before `seek_requested`; the playback
   * thread picks it up at the top of its loop (so a seek works whether
   * or not playback is currently paused) and performs the actual
   * decode-and-discard there - never on the GTK thread, since it and
   * the playback thread must never both call player_make_buffer
   * concurrently on the same `player`. */
  atomic_bool seek_requested;
  atomic_int_least64_t seek_target_tick;
} gui_playback;

/* Loads `path` and opens the default ALSA device at `sample_rate`.
 * Playback does not start until gui_playback_play. Returns false (gui_
 * playback left zeroed) if the file can't be loaded OR no ALSA device
 * is available - both are reported to stderr by the underlying
 * player_load/alsa_output_open calls already, nothing additional here. */
bool gui_playback_load(gui_playback* pb, const char* path, int sample_rate);

/* Same as gui_playback_load, but selects subsong `song_index` (0-based,
 * must be < player_song_count() for the loaded format - see player.h's
 * player_load_song). Ignored by every format except multi-song .ay
 * files, same as the underlying player_load_song. Added for ButNext/
 * ButPrev (MainWin.pas: ButNextClick/ButPrevClick). */
bool gui_playback_load_song(gui_playback* pb, const char* path,
                             int sample_rate, int song_index);

/* Starts the background decode/output thread if not already running;
 * clears the pause flag either way. */
void gui_playback_play(gui_playback* pb);

void gui_playback_pause(gui_playback* pb);
bool gui_playback_is_paused(const gui_playback* pb);
bool gui_playback_is_finished(const gui_playback* pb);

/* Signals the thread to stop and joins it. Safe to call whether or not
 * playback was ever started. */
void gui_playback_stop(gui_playback* pb);

void gui_playback_set_volume(gui_playback* pb, double volume /* 0.0-1.0 */);
double gui_playback_position_seconds(const gui_playback* pb);

/* True (with `*fraction` in [0,1]) if the loaded format has a known
 * fixed duration to compute real playback progress from
 * (player_get_tick_position() - AY/YM/VTX/SNDH/PT3 so far, see its own
 * comment). False otherwise - the caller should fall back to a
 * cosmetic sweep (see gui/src/mainwin.c's on_timer, unchanged for
 * every other format). */
bool gui_playback_get_progress_fraction(const gui_playback* pb,
                                         double* fraction);

/* Requests a seek to `fraction` (0.0-1.0) of the song's total duration
 * - a no-op if gui_playback_get_progress_fraction would return false
 * (nothing to seek within). Non-blocking: the actual decode-and-discard
 * happens on the playback thread (see this struct's own comment on
 * seek_requested/seek_target_tick) - returns immediately, the seek is
 * usually complete well within one on_timer tick (~200ms) for realistic
 * song lengths, since chip decode alone (no ALSA writes) runs far
 * faster than realtime. */
void gui_playback_request_seek(gui_playback* pb, double fraction);

/* Total song duration in real seconds, or 0.0 if unknown (same AY/YM/
 * VTX-only scope as gui_playback_get_progress_fraction - uses
 * player_get_seconds_per_tick() on top of the tick_max that function
 * already reads). Added for JmpTime.pas's "Track length:" label. */
double gui_playback_duration_seconds(const gui_playback* pb);

/* JmpTime.pas: Rewind(time*1000, Time_ms) - seeks to an absolute time
 * in seconds rather than a 0.0-1.0 fraction (gui_playback_request_seek's
 * own unit). Returns false (no-op) if duration is unknown, same as
 * gui_playback_request_seek's own silent no-op in that case - the
 * caller (gui/dialogs/jmptime.c) checks gui_playback_duration_seconds
 * up front instead of relying on this return value for UI purposes. */
bool gui_playback_request_seek_seconds(gui_playback* pb, double seconds);

/* Stops (if running), closes the ALSA device, frees the loaded format. */
void gui_playback_free(gui_playback* pb);

#endif /* GUI_PLAYBACK_H */
