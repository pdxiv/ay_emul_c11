/* Loads a real .ym file (test_corpus_76/Batman_Journey.ym, LHA-compressed YM5!
 * extended format) end-to-end through engine/src/lh5.c + ym_file.c and
 * confirms it plays without error and produces audible output. See
 * README.md. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/formats/ym_file.h"

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

static void test_ym_plays(void) {
  size_t size;
  uint8_t* data = read_whole_file("../../test_corpus_76/Batman_Journey.ym", &size);
  assert(data != NULL && "test_corpus_76/Batman_Journey.ym not found");

  ym_file f;
  ym_file_status st = ym_file_load(&f, data, size, YM_FILE_SAMPLE_RATE_DEF);
  assert(st == YM_FILE_OK);
  free(data);

  int16_t buf[512 * 2];
  bool any_nonzero = false;
  int frames_total = 0;
  int i;
  for (i = 0; i < 400 && !f.real_end_all; i++) {
    int n = ym_file_make_buffer(&f, buf, 512);
    frames_total += n;
    int j;
    for (j = 0; j < n; j++) {
      if (buf[j * 2] != 0 || buf[j * 2 + 1] != 0) any_nonzero = true;
    }
  }

  assert(frames_total > 0);
  assert(any_nonzero && "expected audible output from a real .ym file");
  printf("test_ym_plays: OK (%d sample frames, real_end_all=%d)\n",
         frames_total, f.real_end_all);

  ym_file_free(&f);
}

static void test_bad_header_rejected(void) {
  uint8_t garbage[64];
  memset(garbage, 0, sizeof(garbage));
  ym_file f;
  ym_file_status st = ym_file_load(&f, garbage, sizeof(garbage),
                                    YM_FILE_SAMPLE_RATE_DEF);
  assert(st == YM_FILE_ERR_UNSUPPORTED_TYPE || st == YM_FILE_ERR_BAD_HEADER);
  printf("test_bad_header_rejected: OK\n");
}

int main(void) {
  test_ym_plays();
  test_bad_header_rejected();
  printf("All ym_file smoke tests passed.\n");
  return 0;
}
