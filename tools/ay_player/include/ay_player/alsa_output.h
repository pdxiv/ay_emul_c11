/* Thin ALSA PCM output wrapper. Uses <alsa/asoundlib.h> + -lasound
 * directly, no binding layer, per PORTING_TO_C11_LINUX.md §4.2's decision
 * (the original's hand-maintained ~1854-line fpalsa binding is not needed
 * in C). Must fail gracefully - a clear stderr message and a non-OK
 * return, never a crash - when no ALSA device is available, since that's
 * expected in sandboxed/CI environments (see migration_debt.yaml
 * MIG-0025).
 *
 * MIG-0130: device/buffer/bit-depth are now real, caller-supplied
 * parameters (Mixer.pas's own GBDevice/GBBuffs/GBBRate, digsound.pas's
 * real ALSA branch) - see alsa_output_open's own comment for the exact
 * digsound_open correspondence.
 */
#ifndef PLAYER_ALSA_OUTPUT_H
#define PLAYER_ALSA_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct alsa_output alsa_output;

typedef enum {
  ALSA_OUTPUT_OK = 0,
  ALSA_OUTPUT_ERR_NO_DEVICE,   /* snd_pcm_open failed - no device/permission */
  ALSA_OUTPUT_ERR_PARAMS,      /* device doesn't support the requested params */
  ALSA_OUTPUT_ERR_WRITE,       /* unrecoverable write failure mid-stream */
} alsa_output_status;

/* Opens `device` (NULL/"" -> "default") for `channels`-channel output at
 * `sample_rate`, `bits_per_sample` (8 or 16 - matches ay_synthesizer_
 * {stereo,mono}{8,16}'s own real PCM width, see player_set_sample_bits'
 * own doc comment; anything else falls back to 16), buffered as
 * `num_buffers` periods of `buf_len_ms` each (Mixer.pas: GBBuffs'
 * TBBufLen/TBNumBuf, digsound.pas:232-400's real ALSA branch -
 * `snd_pcm_hw_params_set_buffer_time_near` on the TOTAL `buf_len_ms *
 * num_buffers` microseconds, then `snd_pcm_hw_params_set_period_time_
 * near` on `total/num_buffers` - ported here as the same two-step
 * rather than the single opaque snd_pcm_set_params latency figure this
 * function used before MIG-0130, so real per-period/period-count
 * control actually reaches the device, matching the original's own
 * GBBuffs semantics instead of only approximating their end result).
 * *out is allocated on success; caller must alsa_output_close it. On
 * failure *out is left NULL and a one-line diagnostic is printed to
 * stderr (naming the device and the ALSA error) - the caller should
 * treat this as "no audio available here", not a program bug. */
alsa_output_status alsa_output_open(alsa_output** out, const char* device,
                                     int channels, int sample_rate,
                                     int bits_per_sample, int buf_len_ms,
                                     int num_buffers);

/* Writes `frame_count` interleaved frames, blocking until consumed -
 * `frames` is int16_t-typed samples for a 16-bit-per-sample device
 * (opened via alsa_output_open's own `bits_per_sample=16`), but
 * REINTERPRETED as packed uint8_t samples (half as many total bytes)
 * for an 8-bit-per-sample device - matching ay_synthesizer_stereo16's
 * own doc comment on how ay_engine::buf is aliased between the two
 * widths (AY.pas's PS16/PM16/PS8/PM8 same-BufP-pointer convention) -
 * the caller (gui/src/playback.c) passes the SAME buffer pointer
 * either way, this function reads it according to whichever width the
 * device was actually opened with. Transparently recovers from
 * underrun (-EPIPE) once via snd_pcm_prepare, matching standard ALSA
 * client practice; a second consecutive failure is reported as
 * ALSA_OUTPUT_ERR_WRITE. */
alsa_output_status alsa_output_write(alsa_output* out, const int16_t* frames,
                                      int frame_count);

/* Drains and closes the device. Safe to call with out == NULL. */
void alsa_output_close(alsa_output* out);

/* Mixer.pas: GBDevice's own cbWODeviceChange, populated at form-create
 * time via digsound_getdevices -> prepare_device_list's real ALSA
 * enumeration (digsound.pas:701-749, snd_device_name_hint(-1, "pcm",
 * ...)) - MIG-0130's direct equivalent, same ALSA API, same "pcm"
 * hint-type, just queried on demand here instead of once at startup
 * into a cached global list. Returns a newly g_strdup'd/g_new'd (see
 * gui_alsa_mixer_enumerate's own identical contract, alsa_mixer.h) NULL-
 * free array of `*out_count` device-name strings (free with
 * alsa_output_free_device_names), or NULL with *out_count = 0 if the
 * hint enumeration itself fails entirely - matching prepare_device_
 * list's own `if snd_device_name_hint(...) fails, add_device('default')`
 * fallback, this function's own caller (gui/src/mixer_win.c) falls back
 * to offering just "default" in that case, not silently offering
 * nothing at all. */
char** alsa_output_enumerate_devices(int* out_count);
void alsa_output_free_device_names(char** names, int count);

#endif /* PLAYER_ALSA_OUTPUT_H */
