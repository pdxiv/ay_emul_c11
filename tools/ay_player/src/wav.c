#include "player/wav.h"

#include <string.h>

/* WAV fields are little-endian on disk regardless of host - write them
 * explicitly rather than relying on host byte order. */
static void put_u16le(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

static void put_u32le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* Matches Convs.pas:88-105's TWaveFileHeader field-for-field: rId/rLen/
 * wId/fId/fLen/wFormatTag/nChannels/nSamplesPerSec/nAvgBytesPerSec/
 * nBlockAlign/FormatSpecific/dId/dLen - exactly 44 bytes, no padding. */
#define WAV_HEADER_SIZE 44

static void build_header(uint8_t out[WAV_HEADER_SIZE], int channels,
                          int sample_rate, int bits_per_sample,
                          uint32_t data_len) {
  int block_align = (bits_per_sample / 8) * channels;
  memcpy(out + 0, "RIFF", 4);
  put_u32le(out + 4, (uint32_t)(WAV_HEADER_SIZE + data_len));
  memcpy(out + 8, "WAVE", 4);
  memcpy(out + 12, "fmt ", 4);
  put_u32le(out + 16, 16); /* fLen */
  put_u16le(out + 20, 1);  /* wFormatTag: 1 = PCM */
  put_u16le(out + 22, (uint16_t)channels);
  put_u32le(out + 24, (uint32_t)sample_rate);
  put_u32le(out + 28, (uint32_t)(sample_rate * block_align));
  put_u16le(out + 32, (uint16_t)block_align);
  put_u16le(out + 34, (uint16_t)bits_per_sample);
  memcpy(out + 36, "data", 4);
  put_u32le(out + 40, data_len);
}

bool wav_writer_open(wav_writer* w, const char* path, int channels,
                      int sample_rate, int bits_per_sample) {
  w->fp = fopen(path, "wb");
  if (!w->fp) return false;
  w->channels = channels;
  w->sample_rate = sample_rate;
  w->bits_per_sample = bits_per_sample;
  w->frames_written = 0;

  uint8_t header[WAV_HEADER_SIZE];
  build_header(header, channels, sample_rate, bits_per_sample, 0);
  return fwrite(header, 1, WAV_HEADER_SIZE, w->fp) == WAV_HEADER_SIZE;
}

bool wav_writer_write(wav_writer* w, const int16_t* frames, int frame_count) {
  size_t sample_count = (size_t)frame_count * (size_t)w->channels;
  size_t written = fwrite(frames, sizeof(int16_t), sample_count, w->fp);
  w->frames_written += frame_count;
  return written == sample_count;
}

bool wav_writer_close(wav_writer* w) {
  int block_align = (w->bits_per_sample / 8) * w->channels;
  uint32_t data_len = (uint32_t)(w->frames_written * block_align);

  uint8_t header[WAV_HEADER_SIZE];
  build_header(header, w->channels, w->sample_rate, w->bits_per_sample,
               data_len);

  bool ok = fseek(w->fp, 0, SEEK_SET) == 0 &&
            fwrite(header, 1, WAV_HEADER_SIZE, w->fp) == WAV_HEADER_SIZE;
  if (fclose(w->fp) != 0) ok = false;
  return ok;
}
