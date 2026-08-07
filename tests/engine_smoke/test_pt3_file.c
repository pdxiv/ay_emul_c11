/* Loads the two real .pt3 files (test_corpus_76/ZAGON_07_remixDJ_EchoMAKROSS.pt3,
 * test_corpus_76/ZELiNAPI.pt3) end-to-end through
 * engine/src/pt3_file.c and confirms both play without error and produce
 * audible output. See README.md. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/formats/pt3_file.h"

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

static void test_plays(const char* path) {
  size_t size;
  uint8_t* data = read_whole_file(path, &size);
  assert(data != NULL);

  pt3_file f;
  pt3_file_status st = pt3_file_load(&f, data, size, PT3_FILE_SAMPLE_RATE_DEF);
  assert(st == PT3_FILE_OK);
  free(data);

  int16_t buf[512 * 2];
  bool any_nonzero = false;
  int frames_total = 0;
  int i;
  for (i = 0; i < 400; i++) {
    int n = pt3_file_make_buffer(&f, buf, 512);
    frames_total += n;
    int j;
    for (j = 0; j < n; j++) {
      if (buf[j * 2] != 0 || buf[j * 2 + 1] != 0) any_nonzero = true;
    }
  }

  assert(frames_total == 400 * 512);
  assert(any_nonzero && "expected audible output from a real .pt3 file");
  printf("test_plays(%s): OK (%d sample frames)\n", path, frames_total);
}

/* NOT testing PT3_FILE_ERR_BAD_HEADER here (on purpose, not an
 * oversight) - pt3_file.h's own enum comment explains why nothing can
 * actually trigger it: PT3's text signature is Tier-C-only in real
 * Pascal, never checked at load time, and real corpus files exist
 * (DIABOLIS_IN_MUSICA.pt3) that fail it entirely yet load and play
 * correctly - a load-time signature check was tried and reverted for
 * exactly that reason. Truncation (PT3_FILE_ERR_TRUNCATED) IS real
 * Pascal-equivalent validation (Players.pas' own buffer-size guards),
 * so that's what this actually tests. */
static void test_truncated_rejected(void) {
  uint8_t garbage[100]; /* below the 202-byte minimum header size */
  memset(garbage, 0, sizeof(garbage));
  pt3_file f;
  pt3_file_status st = pt3_file_load(&f, garbage, sizeof(garbage),
                                      PT3_FILE_SAMPLE_RATE_DEF);
  assert(st == PT3_FILE_ERR_TRUNCATED);
  printf("test_truncated_rejected: OK\n");
}

int main(void) {
  test_plays("../../test_corpus_76/ZAGON_07_remixDJ_EchoMAKROSS.pt3");
  test_plays("../../test_corpus_76/ZELiNAPI.pt3");
  test_truncated_rejected();
  printf("All pt3_file smoke tests passed.\n");
  return 0;
}
