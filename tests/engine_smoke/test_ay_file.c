/* Loads a real .ay file (songs/cpc/Discmac20_0.ay) end-to-end through
 * engine/src/ay_file.c and confirms it plays without error and produces
 * audible output - a first for this project's smoke tests, which have
 * otherwise all used synthetic programs. See README.md. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/ay_file.h"

static uint8_t* read_whole_file(const char* path, size_t* out_size) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* buf = (uint8_t*)malloc((size_t)sz);
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *out_size = (size_t)sz;
  return buf;
}

static void test_discmac_plays(void) {
  size_t size;
  uint8_t* data = read_whole_file("../../songs/cpc/Discmac20_0.ay", &size);
  assert(data != NULL && "songs/cpc/Discmac20_0.ay not found");

  ay_file f;
  ay_file_status st =
      ay_file_load(&f, data, size, 0, AY_FILE_AY_FREQ_DEF,
                   AY_FILE_FRQ_Z80_DEF, AY_FILE_SAMPLE_RATE_DEF);
  assert(st == AY_FILE_OK);

  int16_t buf[512 * 2];
  bool any_nonzero = false;
  int frames_total = 0;
  int i;
  for (i = 0; i < 200 && !f.real_end_all; i++) {
    int n = ay_file_make_buffer(&f, buf, 512);
    frames_total += n;
    int j;
    for (j = 0; j < n; j++) {
      if (buf[j * 2] != 0 || buf[j * 2 + 1] != 0) any_nonzero = true;
    }
  }

  assert(frames_total > 0);
  assert(any_nonzero && "expected audible output from a real .ay file");
  printf("test_discmac_plays: OK (%d sample frames, real_end_all=%d)\n",
         frames_total, f.real_end_all);

  free(data);
}

static void test_bad_header_rejected(void) {
  uint8_t garbage[64];
  memset(garbage, 0, sizeof(garbage));
  ay_file f;
  ay_file_status st = ay_file_load(&f, garbage, sizeof(garbage), 0,
                                    AY_FILE_AY_FREQ_DEF, AY_FILE_FRQ_Z80_DEF,
                                    AY_FILE_SAMPLE_RATE_DEF);
  assert(st == AY_FILE_ERR_BAD_HEADER);
  printf("test_bad_header_rejected: OK\n");
}

int main(void) {
  test_discmac_plays();
  test_bad_header_rejected();
  printf("All ay_file smoke tests passed.\n");
  return 0;
}
