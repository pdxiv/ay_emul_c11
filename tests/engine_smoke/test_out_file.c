/* Loads the synthetic .out fixture (tests/oracle_diff/synthetic/test.out
 * - no real .out file exists anywhere in this project's corpus, see that
 * directory's own gen_out_epsg.py comment) end-to-end through out_file.c
 * and confirms it plays without error and produces audible output.
 * Byte-exact correctness against the real Pascal oracle is covered by
 * tests/oracle_diff/run_diff.sh's out_file/out_psg_export gates. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/formats/out_file.h"

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

static void test_plays(void) {
  size_t size;
  uint8_t* data =
      read_whole_file("../../tests/oracle_diff/synthetic/test.out", &size);
  assert(data != NULL);

  out_file f;
  out_file_status st =
      out_file_load(&f, data, size, OUT_FILE_AY_FREQ_DEF,
                    OUT_FILE_FRQ_Z80_DEF, OUT_FILE_MAX_TSTATES_DEF,
                    OUT_FILE_SAMPLE_RATE_DEF);
  assert(st == OUT_FILE_OK);
  free(data);

  int16_t buf[512 * 2];
  bool any_nonzero = false;
  int frames_total = 0;
  int i;
  for (i = 0; i < 20 && !f.real_end_all; i++) {
    int n = out_file_make_buffer(&f, buf, 512);
    frames_total += n;
    int j;
    for (j = 0; j < n; j++) {
      if (buf[j * 2] != 0 || buf[j * 2 + 1] != 0) any_nonzero = true;
    }
  }

  assert(frames_total > 0);
  assert(any_nonzero && "expected audible output from the synthetic .out fixture");
  printf("test_plays: OK (%d sample frames, real_end_all=%d)\n", frames_total,
         f.real_end_all);

  out_file_free(&f);
}

static void test_truncated_rejected(void) {
  uint8_t garbage[4]; /* fewer than 5 bytes - not even one record */
  out_file f;
  out_file_status st =
      out_file_load(&f, garbage, sizeof(garbage), OUT_FILE_AY_FREQ_DEF,
                    OUT_FILE_FRQ_Z80_DEF, OUT_FILE_MAX_TSTATES_DEF,
                    OUT_FILE_SAMPLE_RATE_DEF);
  assert(st == OUT_FILE_ERR_TRUNCATED);
  printf("test_truncated_rejected: OK\n");
}

int main(void) {
  test_plays();
  test_truncated_rejected();
  printf("All out_file smoke tests passed.\n");
  return 0;
}
