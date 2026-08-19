/* alsa/asoundlib.h's global.h redeclares `struct timespec` unless glibc's
 * own feature-test macros are visible - under strict `-std=c11` those
 * aren't defined by default, causing a hard redefinition error on some
 * distros (confirmed on this build). _GNU_SOURCE restores them; this is
 * a compilation-environment workaround for a third-party header, not a
 * relaxation of this file's own C11 conformance. Must come before ANY
 * other include (even our own headers), since once glibc's feature-test
 * macros are implicitly locked in by an earlier include, defining
 * _GNU_SOURCE afterwards has no effect. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <alsa/asoundlib.h>

#include "ay_player/alsa_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct alsa_output {
  snd_pcm_t* pcm;
  int channels;
  int bits_per_sample;
};

/* Mixer.pas: digsound.pas's real ALSA branch (digsound_open, 232-400) -
 * MIG-0130's own doc comment (alsa_output.h) has the full GBBuffs/
 * bits_per_sample citation. */
alsa_output_status alsa_output_open(alsa_output** out, const char* device,
                                     int channels, int sample_rate,
                                     int bits_per_sample, int buf_len_ms,
                                     int num_buffers) {
  *out = NULL;
  const char* dev = (device && device[0]) ? device : "default";
  snd_pcm_t* pcm = NULL;
  int err = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    fprintf(stderr, "ay_player: cannot open ALSA device '%s': %s\n", dev,
            snd_strerror(err));
    return ALSA_OUTPUT_ERR_NO_DEVICE;
  }

  snd_pcm_format_t fmt =
      (bits_per_sample == 8) ? SND_PCM_FORMAT_U8 : SND_PCM_FORMAT_S16_LE;

  snd_pcm_hw_params_t* hw;
  snd_pcm_hw_params_alloca(&hw);
  err = snd_pcm_hw_params_any(pcm, hw);
  if (err >= 0)
    err = snd_pcm_hw_params_set_access(pcm, hw,
                                        SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err >= 0) err = snd_pcm_hw_params_set_format(pcm, hw, fmt);
  if (err >= 0)
    err = snd_pcm_hw_params_set_channels(pcm, hw, (unsigned int)channels);
  unsigned int rate = (unsigned int)sample_rate;
  if (err >= 0)
    err = snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, NULL);

  /* digsound.pas:232-400's own two-step: total buffer time first
   * (BufLen_ms * NumberOfBuffers, in µs), THEN period time as
   * total/NumberOfBuffers - matches Mixer.pas's own GBBuffs semantics
   * (see alsa_output.h's own doc comment) rather than the single
   * opaque latency figure this function used before MIG-0130. */
  if (buf_len_ms < 1) buf_len_ms = 1;
  if (num_buffers < 1) num_buffers = 1;
  unsigned int buffer_time_us =
      (unsigned int)buf_len_ms * (unsigned int)num_buffers * 1000u;
  if (err >= 0)
    err = snd_pcm_hw_params_set_buffer_time_near(pcm, hw, &buffer_time_us,
                                                  NULL);
  unsigned int period_time_us = buffer_time_us / (unsigned int)num_buffers;
  if (err >= 0)
    err = snd_pcm_hw_params_set_period_time_near(pcm, hw, &period_time_us,
                                                  NULL);
  if (err >= 0) err = snd_pcm_hw_params(pcm, hw);

  if (err < 0) {
    fprintf(stderr, "ay_player: ALSA device rejected params: %s\n",
            snd_strerror(err));
    snd_pcm_close(pcm);
    return ALSA_OUTPUT_ERR_PARAMS;
  }
  err = snd_pcm_prepare(pcm);
  if (err < 0) {
    fprintf(stderr, "ay_player: ALSA device prepare failed: %s\n",
            snd_strerror(err));
    snd_pcm_close(pcm);
    return ALSA_OUTPUT_ERR_PARAMS;
  }

  alsa_output* a = malloc(sizeof(*a));
  if (!a) {
    snd_pcm_close(pcm);
    return ALSA_OUTPUT_ERR_NO_DEVICE;
  }
  a->pcm = pcm;
  a->channels = channels;
  a->bits_per_sample = (bits_per_sample == 8) ? 8 : 16;
  *out = a;
  return ALSA_OUTPUT_OK;
}

alsa_output_status alsa_output_write(alsa_output* out, const int16_t* frames,
                                      int frame_count) {
  /* MIG-0130: an 8-bit-per-sample device reads `frames` as packed
   * uint8_t samples (see alsa_output.h's own doc comment) - snd_pcm_
   * writei itself counts in FRAMES regardless of sample width once the
   * device is opened with the matching format, so frame_count/recovery
   * logic below is identical either way; only the pointer type read
   * from `p` differs. */
  const uint8_t* p8 = (const uint8_t*)frames;
  const int16_t* p16 = frames;
  int remaining = frame_count;
  bool recovered_once = false;

  while (remaining > 0) {
    snd_pcm_sframes_t written =
        (out->bits_per_sample == 8)
            ? snd_pcm_writei(out->pcm, p8, (snd_pcm_uframes_t)remaining)
            : snd_pcm_writei(out->pcm, p16, (snd_pcm_uframes_t)remaining);
    if (written == -EPIPE) {
      if (recovered_once) return ALSA_OUTPUT_ERR_WRITE;
      snd_pcm_prepare(out->pcm);
      recovered_once = true;
      continue;
    }
    if (written == -EAGAIN) continue;
    if (written < 0) {
      int recovered = snd_pcm_recover(out->pcm, (int)written, 1);
      if (recovered < 0 || recovered_once) return ALSA_OUTPUT_ERR_WRITE;
      recovered_once = true;
      continue;
    }
    if (out->bits_per_sample == 8) {
      p8 += (size_t)written * (size_t)out->channels;
    } else {
      p16 += (size_t)written * (size_t)out->channels;
    }
    remaining -= (int)written;
  }
  return ALSA_OUTPUT_OK;
}

void alsa_output_close(alsa_output* out) {
  if (!out) return;
  snd_pcm_drain(out->pcm);
  snd_pcm_close(out->pcm);
  free(out);
}

/* Mixer.pas: digsound.pas's prepare_device_list (701-740) - see
 * alsa_output.h's own doc comment. */
char** alsa_output_enumerate_devices(int* out_count) {
  *out_count = 0;
  void** hints;
  if (snd_device_name_hint(-1, "pcm", &hints) != 0) return NULL;

  char** names = NULL;
  int count = 0;
  for (void** n = hints; *n != NULL; n++) {
    char* name = snd_device_name_get_hint(*n, "NAME");
    if (name && strcmp(name, "null") != 0) {
      char** grown = realloc(names, sizeof(char*) * (size_t)(count + 1));
      if (grown) {
        names = grown;
        names[count] = strdup(name);
        count++;
      }
    }
    free(name);
  }
  snd_device_name_free_hint(hints);
  *out_count = count;
  return names;
}

void alsa_output_free_device_names(char** names, int count) {
  if (!names) return;
  for (int i = 0; i < count; i++) free(names[i]);
  free(names);
}
