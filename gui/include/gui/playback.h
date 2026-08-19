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
  /* MIG-0112: playlist-level Turbosound pairing - `pair.primary` is what
   * every "the loaded player" reference throughout this file/mainwin.c
   * used to mean when this was a plain `player p`; `pair.active` is
   * true iff `has_ts_pair` (below) was set at load time AND pairing
   * actually activated (see player_pair_load_song's own comment on when
   * it doesn't, e.g. a self-pairing PT3 file). Every render/query call
   * in this file now goes through player_pair_* (a plain passthrough to
   * `pair.primary` alone when `pair.active` is false, so non-paired
   * playback is unaffected). */
  player_pair pair;
  bool has_ts_pair; /* the REQUEST, from gui_playlist_entry::has_ts_pair -
                      * kept here (distinct from pair.active, the
                      * OUTCOME) so do_seek's backward-seek reload path
                      * knows whether to re-request pairing too. */
  bool is_ste; /* Mixer.pas: GBSNDH's STRB/STeRB (MIG-0121) - SNDH-only,
                * see player.h's player_load_song comment; remembered so
                * a backward-seek reload (playback.c's do_seek) re-opens
                * with the same setting the file was originally loaded
                * with. */
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
  int channels; /* MainWin.pas: NumberOfChannels, 1 or 2 - set at load
                 * time only (see gui_playback_load_song's `channels`
                 * parameter and player_set_number_of_channels's own
                 * comment on why this port, like the original, never
                 * hot-reconfigures it mid-playback) */
  int bits_per_sample; /* Mixer.pas: SampleBit, 8 or 16 (MIG-0130) - same
                         * load-time-only convention as `channels` above,
                         * see player_set_sample_bits's own comment. */

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

  /* Seeking (Players.pas: RerollMusic, MIG-0079/MIG-0100/MIG-0101/
   * MIG-0103/MIG-0104) - only meaningful for formats
   * player_get_tick_position() supports (AY/YM/VTX/SNDH/PT3 plus
   * PT1/PT2/GTR/FLS/STC/STP/FXM/PSM/ASC/ASC0/FTC/PSC/SQT - see its own
   * comment for the full trace). `seek_target_tick`
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
 * ButPrev (MainWin.pas: ButNextClick/ButPrevClick).
 *
 * `channels` (1 or 2 - anything else is treated as 2/stereo, matching
 * MainWin.pas:1795's `St in [1,2]` guard on Set_Stereo, which this
 * loads through to via player_set_number_of_channels) selects the
 * output channel count for the freshly-loaded file: opens the ALSA
 * device with that channel count AND configures the engine to emit
 * MONO16 or STEREO16 frames accordingly (see player_make_buffer's own
 * comment on the two frame shapes) - load-time only, matching the
 * original's own not-while-playing restriction (Set_Stereo:
 * `if IsPlaying then exit`), which is naturally satisfied here since
 * this is only ever called before gui_playback_play.
 *
 * `ts_pair` (MIG-0112) requests playlist-level Turbosound pairing,
 * mirroring gui_playlist_entry::has_ts_pair - since this port's own
 * .ayl support only ever produces "pair `path` with itself" (see that
 * field's own comment for the full trace against PlayList.pas's
 * LoadPLItem/SavePLItem), this just re-loads the SAME path as the
 * pair's secondary voice; pass false for ordinary, unpaired playback.
 * Pairing may still end up inactive even when requested (e.g. `path` is
 * a self-pairing PT3 file) - check gui_playback_is_ts_paired(pb) after
 * a successful load if the caller needs to know for sure.
 *
 * `is_ste` (MIG-0121): Mixer.pas's GBSNDH STRB/STeRB radio pair -
 * SNDH-only (no-op for every other format, passed straight through to
 * player_pair_load_song/player_load_song); pass true unless the caller
 * has a specific reason to force plain-ST (no DMA-sound hardware)
 * behavior.
 *
 * `device`/`bits_per_sample`/`buf_len_ms`/`num_buffers` (MIG-0130):
 * Mixer.pas's own GBDevice/GBBRate/GBBuffs - ALSA output-device
 * configuration, passed straight through to alsa_output_open (see its
 * own doc comment for the exact digsound_open correspondence).
 * `device` NULL/"" opens ALSA's own "default". `bits_per_sample` (8 or
 * 16) is ALSO applied to the loaded player via player_set_sample_bits
 * (real 8-bit-WIDTH PCM, not just a smaller amplitude scale - see that
 * function's own doc comment) - unlike `channels`, which only affects
 * the engine's own synthesizer selection, this one setting has to stay
 * consistent on BOTH sides (engine output shape AND the ALSA device's
 * own configured sample format) or the two would produce mismatched
 * byte widths. */
bool gui_playback_load_song(gui_playback* pb, const char* path,
                             int sample_rate, int song_index, int channels,
                             bool ts_pair, bool is_ste, const char* device,
                             int bits_per_sample, int buf_len_ms,
                             int num_buffers);

/* True iff the currently-loaded file is genuinely playing as a
 * Turbosound pair (player_pair::active) - false for ordinary playback
 * OR a requested-but-refused pairing (see gui_playback_load_song's own
 * `ts_pair` comment). */
bool gui_playback_is_ts_paired(const gui_playback* pb);

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
 * (player_get_tick_position() - see its own comment for the full list
 * of formats covered). False otherwise - the caller should fall back
 * to a cosmetic sweep (see gui/src/mainwin.c's on_timer, unchanged for
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

/* Total song duration in real seconds, or 0.0 if unknown (same all-18-
 * formats scope as gui_playback_get_progress_fraction - uses
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
