/* Hand-rolled PCM WAV writer, matching ay_emul/Convs.pas's TWaveFileHeader
 * layout exactly (Convs.pas:88-105): a standard 44-byte RIFF/WAVE/fmt /
 * data header, written as a placeholder first, then rewritten once the
 * real frame count is known (Convs.pas:525,551-555's seek-write-seek-
 * rewrite pattern) - matching that exactly is what lets
 * tests/oracle_diff's wav_export scenario byte-compare against it (see
 * migration_debt.yaml MIG-0026). No library involvement, per
 * PORTING_TO_C11_LINUX.md §4.3/§7.3 (explicitly rejects even libsndfile
 * as overkill for a format this simple). */
#ifndef PLAYER_WAV_H
#define PLAYER_WAV_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct wav_writer {
  FILE* fp;
  int channels;
  int sample_rate;
  int bits_per_sample;
  int64_t frames_written;
} wav_writer;

/* Opens `path` and writes a placeholder header. Returns false on any I/O
 * error (path unwritable, etc). */
bool wav_writer_open(wav_writer* w, const char* path, int channels,
                      int sample_rate, int bits_per_sample);

/* Streams `frame_count` interleaved stereo16 frames (2 * frame_count
 * int16_t values) as raw PCM. Returns false on a short write. */
bool wav_writer_write(wav_writer* w, const int16_t* frames, int frame_count);

/* Rewrites the header with the real frame count and closes the file.
 * Returns false if either the seek-back or the final write failed. */
bool wav_writer_close(wav_writer* w);

#endif /* PLAYER_WAV_H */
