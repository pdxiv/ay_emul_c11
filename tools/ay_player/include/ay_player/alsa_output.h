/* Thin ALSA PCM output wrapper. Uses <alsa/asoundlib.h> + -lasound
 * directly, no binding layer, per PORTING_TO_C11_LINUX.md §4.2's decision
 * (the original's hand-maintained ~1854-line fpalsa binding is not needed
 * in C). Must fail gracefully - a clear stderr message and a non-OK
 * return, never a crash - when no ALSA device is available, since that's
 * expected in sandboxed/CI environments (see migration_debt.yaml
 * MIG-0025). */
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

/* Opens the default PCM device for stereo16 output at `sample_rate`.
 * *out is allocated on success; caller must alsa_output_close it. On
 * failure *out is left NULL and a one-line diagnostic is printed to
 * stderr (naming the device and the ALSA error) - the caller should treat
 * this as "no audio available here", not a program bug. */
alsa_output_status alsa_output_open(alsa_output** out, int channels,
                                     int sample_rate);

/* Writes `frame_count` interleaved stereo16 frames, blocking until
 * consumed. Transparently recovers from underrun (-EPIPE) once via
 * snd_pcm_prepare, matching standard ALSA client practice; a second
 * consecutive failure is reported as ALSA_OUTPUT_ERR_WRITE. */
alsa_output_status alsa_output_write(alsa_output* out, const int16_t* frames,
                                      int frame_count);

/* Drains and closes the device. Safe to call with out == NULL. */
void alsa_output_close(alsa_output* out);

#endif /* PLAYER_ALSA_OUTPUT_H */
