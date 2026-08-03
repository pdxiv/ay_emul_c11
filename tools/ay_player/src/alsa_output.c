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

#include "player/alsa_output.h"

#include <stdio.h>
#include <stdlib.h>

struct alsa_output {
  snd_pcm_t* pcm;
  int channels;
};

alsa_output_status alsa_output_open(alsa_output** out, int channels,
                                     int sample_rate) {
  *out = NULL;
  snd_pcm_t* pcm = NULL;
  int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    fprintf(stderr, "ay_player: cannot open ALSA device 'default': %s\n",
            snd_strerror(err));
    return ALSA_OUTPUT_ERR_NO_DEVICE;
  }

  err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                            SND_PCM_ACCESS_RW_INTERLEAVED, channels,
                            (unsigned int)sample_rate, 1 /* allow resample */,
                            200000 /* 200ms latency */);
  if (err < 0) {
    fprintf(stderr, "ay_player: ALSA device rejected params: %s\n",
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
  *out = a;
  return ALSA_OUTPUT_OK;
}

alsa_output_status alsa_output_write(alsa_output* out, const int16_t* frames,
                                      int frame_count) {
  const int16_t* p = frames;
  int remaining = frame_count;
  bool recovered_once = false;

  while (remaining > 0) {
    snd_pcm_sframes_t written =
        snd_pcm_writei(out->pcm, p, (snd_pcm_uframes_t)remaining);
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
    p += (size_t)written * (size_t)out->channels;
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
