/* Loads the synthetic .epsg fixture (tests/oracle_diff/synthetic/
 * test.epsg - no real .epsg file exists anywhere in this project's
 * corpus, see that directory's own gen_out_epsg.py comment) end-to-end
 * through epsg_file.c and confirms it plays without error and produces
 * audible output. Byte-exact correctness against the real Pascal oracle
 * is covered by tests/oracle_diff/run_diff.sh's epsg_file/epsg_psg_export
 * gates. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/formats/epsg_file.h"

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
      read_whole_file("../../tests/oracle_diff/synthetic/test.epsg", &size);
  assert(data != NULL);

  epsg_file f;
  epsg_file_status st =
      epsg_file_load(&f, data, size, EPSG_FILE_AY_FREQ_DEF,
                     EPSG_FILE_FRQ_Z80_DEF, EPSG_FILE_SAMPLE_RATE_DEF);
  assert(st == EPSG_FILE_OK);
  free(data);

  int16_t buf[512 * 2];
  bool any_nonzero = false;
  int frames_total = 0;
  int i;
  for (i = 0; i < 20 && !f.real_end_all; i++) {
    int n = epsg_file_make_buffer(&f, buf, 512);
    frames_total += n;
    int j;
    for (j = 0; j < n; j++) {
      if (buf[j * 2] != 0 || buf[j * 2 + 1] != 0) any_nonzero = true;
    }
  }

  assert(frames_total > 0);
  assert(any_nonzero && "expected audible output from the synthetic .epsg fixture");
  printf("test_plays: OK (%d sample frames, real_end_all=%d)\n", frames_total,
         f.real_end_all);

  epsg_file_free(&f);
}

static void test_bad_header_rejected(void) {
  uint8_t garbage[20];
  memset(garbage, 0, sizeof(garbage));
  memcpy(garbage, "NOPE", 4);
  epsg_file f;
  epsg_file_status st =
      epsg_file_load(&f, garbage, sizeof(garbage), EPSG_FILE_AY_FREQ_DEF,
                     EPSG_FILE_FRQ_Z80_DEF, EPSG_FILE_SAMPLE_RATE_DEF);
  assert(st == EPSG_FILE_ERR_BAD_HEADER);
  printf("test_bad_header_rejected: OK\n");
}

static void test_unsupported_selector_rejected(void) {
  uint8_t hdr[20];
  memset(hdr, 0, sizeof(hdr));
  memcpy(hdr, "EPSG", 4);
  hdr[4] = 0x1A;
  hdr[5] = 2; /* only 0/1/255 are valid selectors */
  epsg_file f;
  epsg_file_status st =
      epsg_file_load(&f, hdr, sizeof(hdr), EPSG_FILE_AY_FREQ_DEF,
                     EPSG_FILE_FRQ_Z80_DEF, EPSG_FILE_SAMPLE_RATE_DEF);
  assert(st == EPSG_FILE_ERR_BAD_HEADER);
  printf("test_unsupported_selector_rejected: OK\n");
}

int main(void) {
  test_plays();
  test_bad_header_rejected();
  test_unsupported_selector_rejected();
  printf("All epsg_file smoke tests passed.\n");
  return 0;
}
