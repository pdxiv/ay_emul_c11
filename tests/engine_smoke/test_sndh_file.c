/* Loads a real .sndh file (test_corpus_76/More_Short_Demos.sndh) end-to-end
 * through engine/src/sndh_file.c. MIG-0045 fixed a systematic byte-swap
 * bug in the hand-assembled bootstrap code that previously caused the
 * CPU to run away into unmapped memory without ever writing a real AY
 * register (MIG-0021's original "no register write observed" symptom -
 * that was NOT a timing/scheduling question as first suspected, it was
 * corrupted opcodes). Audio is now confirmed audible (asserted below) -
 * STILL KNOWN INCOMPLETE per MIG-0045: not yet byte-identical to the
 * real Pascal engine's own output (a smaller, separate, not-yet-
 * root-caused divergence in exactly when audio starts). See README.md. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/sndh_file.h"

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
  uint8_t* data = read_whole_file("../../test_corpus_76/More_Short_Demos.sndh", &size);
  assert(data != NULL);

  sndh_file f;
  sndh_file_status st = sndh_file_load(&f, data, size, SNDH_FILE_SAMPLE_RATE_DEF);
  assert(st == SNDH_FILE_OK);
  free(data);

  int16_t buf[512 * 2];
  bool any_nonzero = false;
  int frames_total = 0;
  int i;
  for (i = 0; i < 400 && !f.real_end_all; i++) {
    int n = sndh_file_make_buffer(&f, buf, 512);
    frames_total += n;
    int j;
    for (j = 0; j < n; j++) {
      if (buf[j * 2] != 0 || buf[j * 2 + 1] != 0) any_nonzero = true;
    }
  }

  assert(frames_total > 0);
  /* MIG-0045: audio is now genuinely confirmed, not just "runs without
   * crashing" - a real regression guard against the byte-swap bug
   * recurring. Still not byte-identical to the real Pascal engine's own
   * output (see migration_debt.yaml MIG-0045) - that gap isn't checked
   * here since this test has no oracle-diff fixture, only ay_player's
   * own manual WAV-export comparison does. */
  assert(any_nonzero);
  printf("test_plays: loads and runs with audible output (%d sample "
         "frames, real_end_all=%d) - see MIG-0045 for the remaining "
         "not-yet-byte-identical gap\n",
         frames_total, f.real_end_all);

  sndh_file_free(&f);
}

static void test_ice_compressed_rejected(void) {
  uint8_t garbage[64];
  memset(garbage, 0, sizeof(garbage));
  memcpy(garbage, "ICE!", 4);
  sndh_file f;
  sndh_file_status st =
      sndh_file_load(&f, garbage, sizeof(garbage), SNDH_FILE_SAMPLE_RATE_DEF);
  assert(st == SNDH_FILE_ERR_ICE_COMPRESSED);
  printf("test_ice_compressed_rejected: OK\n");
}

int main(void) {
  test_plays();
  test_ice_compressed_rejected();
  printf("All sndh_file smoke tests passed.\n");
  return 0;
}
