/* ay_player - minimal CLI player/WAV exporter for the 5 chiptune formats
 * engine/libayengine.a supports (AY/YM/PT3/SNDH/VTX). Phase 4 of
 * PORTING_TO_C11_LINUX.md's phased approach (§8) - see migration_debt.yaml
 * MIG-0025/0026/0027 for what's validated and how.
 *
 * Usage: ay_player <file> [--wav=<path>] [--seconds=N]
 *   No --wav: plays through the default ALSA device.
 *   --wav=<path>: renders to a WAV file instead (no ALSA dependency on
 *     this path, so it's fully testable in headless/CI environments).
 *   --seconds=N (default 180): upper bound on render duration. AY/PT3
 *     have no natural "song ended" concept (they loop/continue by
 *     design - see ay_file.h/pt3_file.h) so this is the only stop
 *     condition for them; YM/SNDH/VTX may also stop earlier via their
 *     own real_end_all flag.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player/alsa_output.h"
#include "player/format.h"
#include "player/wav.h"

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BUFFER_FRAMES 512
#define DEFAULT_SECONDS 180

static uint8_t* read_whole_file(const char* path, size_t* out_size) {
  FILE* fp = fopen(path, "rb");
  if (!fp) return NULL;
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (sz < 0) {
    fclose(fp);
    return NULL;
  }
  uint8_t* buf = malloc((size_t)sz > 0 ? (size_t)sz : 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
    fclose(fp);
    free(buf);
    return NULL;
  }
  fclose(fp);
  *out_size = (size_t)sz;
  return buf;
}

static int render_to_wav(player* p, const char* wav_path, int max_frames) {
  wav_writer w;
  if (!wav_writer_open(&w, wav_path, CHANNELS, SAMPLE_RATE, 16)) {
    fprintf(stderr, "ay_player: cannot create '%s'\n", wav_path);
    return 1;
  }

  int16_t buf[BUFFER_FRAMES * CHANNELS];
  int frames_done = 0;
  while (frames_done < max_frames && !player_real_end_all(p)) {
    int want = BUFFER_FRAMES;
    if (max_frames - frames_done < want) want = max_frames - frames_done;
    int n = player_make_buffer(p, buf, want);
    if (n <= 0) break;
    if (!wav_writer_write(&w, buf, n)) {
      fprintf(stderr, "ay_player: write error to '%s'\n", wav_path);
      wav_writer_close(&w);
      return 1;
    }
    frames_done += n;
  }

  if (!wav_writer_close(&w)) {
    fprintf(stderr, "ay_player: failed finalizing '%s'\n", wav_path);
    return 1;
  }
  fprintf(stderr, "ay_player: wrote %d frames to '%s'\n", frames_done,
          wav_path);
  return 0;
}

static int play_via_alsa(player* p, int max_frames) {
  alsa_output* out;
  alsa_output_status ast = alsa_output_open(&out, CHANNELS, SAMPLE_RATE);
  if (ast != ALSA_OUTPUT_OK) return 1;

  int16_t buf[BUFFER_FRAMES * CHANNELS];
  int frames_done = 0;
  int result = 0;
  while (frames_done < max_frames && !player_real_end_all(p)) {
    int want = BUFFER_FRAMES;
    if (max_frames - frames_done < want) want = max_frames - frames_done;
    int n = player_make_buffer(p, buf, want);
    if (n <= 0) break;
    if (alsa_output_write(out, buf, n) != ALSA_OUTPUT_OK) {
      fprintf(stderr, "ay_player: ALSA write error\n");
      result = 1;
      break;
    }
    frames_done += n;
  }

  alsa_output_close(out);
  return result;
}

int main(int argc, char** argv) {
  const char* path = NULL;
  const char* wav_path = NULL;
  int seconds = DEFAULT_SECONDS;
  int frames = -1; /* -1 = not given; takes precedence over --seconds when set */

  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--wav=", 6) == 0) {
      wav_path = argv[i] + 6;
    } else if (strncmp(argv[i], "--seconds=", 10) == 0) {
      seconds = atoi(argv[i] + 10);
    } else if (strncmp(argv[i], "--frames=", 9) == 0) {
      frames = atoi(argv[i] + 9);
    } else if (!path) {
      path = argv[i];
    } else {
      fprintf(stderr,
              "usage: %s <file> [--wav=<path>] [--seconds=N] [--frames=N]\n",
              argv[0]);
      return 2;
    }
  }
  if (!path || seconds <= 0 || (frames != -1 && frames <= 0)) {
    fprintf(stderr,
            "usage: %s <file> [--wav=<path>] [--seconds=N] [--frames=N]\n",
            argv[0]);
    return 2;
  }

  size_t size;
  uint8_t* data = read_whole_file(path, &size);
  if (!data) {
    fprintf(stderr, "ay_player: cannot read '%s'\n", path);
    return 1;
  }

  player p;
  player_status pst = player_load(&p, path, data, size, SAMPLE_RATE);
  free(data);
  if (pst == PLAYER_ERR_UNRECOGNIZED) {
    fprintf(stderr,
            "ay_player: '%s' is not a recognized AY/YM/PT3/SNDH/VTX/PT1 file\n",
            path);
    return 1;
  }
  if (pst == PLAYER_ERR_LOAD_FAILED) {
    fprintf(stderr,
            "ay_player: '%s' looks like %s but failed to load "
            "(unsupported sub-variant or malformed content)\n",
            path, player_format_name(p.format));
    return 1;
  }

  fprintf(stderr, "ay_player: '%s' detected as %s\n", path,
          player_format_name(p.format));

  int max_frames = (frames != -1) ? frames : seconds * SAMPLE_RATE;
  int result = wav_path ? render_to_wav(&p, wav_path, max_frames)
                         : play_via_alsa(&p, max_frames);

  player_free(&p);
  return result;
}
