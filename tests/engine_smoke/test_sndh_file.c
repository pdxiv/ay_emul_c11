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

#include "ay_engine/formats/sndh_file.h"

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
  sndh_file_status st = sndh_file_load(&f, data, size, SNDH_FILE_SAMPLE_RATE_DEF, true);
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

/* MIG-0016: ICE-compressed files are now decompressed and played
 * normally (see test_ice_unpacks_real_file below) - this test now
 * covers the "header present but corrupt/garbage" case specifically,
 * which is what an all-zero packed-size/unpacked-size field pair (a
 * genuinely malformed file, not a real one) represents: packed_size_
 * field ends up < 12, caught by sndh_ice_unpack's own defensive check
 * before it could underflow. */
static void test_ice_corrupt_header_rejected(void) {
  uint8_t garbage[64];
  memset(garbage, 0, sizeof(garbage));
  memcpy(garbage, "ICE!", 4);
  sndh_file f;
  sndh_file_status st =
      sndh_file_load(&f, garbage, sizeof(garbage), SNDH_FILE_SAMPLE_RATE_DEF, true);
  assert(st == SNDH_FILE_ERR_ICE_CORRUPT);
  printf("test_ice_corrupt_header_rejected: OK\n");
}

/* MIG-0016: test_corpus_76/megaintr.snd is a real, user-supplied
 * ICE-compressed SNDH file ("Mega Intro" by Paradox) - the first real
 * ICE-compressed file available in this repo, and what actually made
 * porting sndh_ice_unpack possible to validate at all (see
 * migration_debt.yaml MIG-0016). This calls sndh_ice_unpack directly,
 * NOT the full sndh_file_load->playback path - a full playback pass is
 * impractically slow for SNDH (MIG-0021), and byte-exact correctness of
 * the depacker itself is already proven separately, by oracle-diff
 * (tests/oracle_diff/run_diff.sh's sndh_unpack gate, byte-identical
 * against the real Pascal sndh_UnpackFile). This test just guards
 * against a regression breaking the decompression path at all. */
static void test_ice_unpacks_real_file(void) {
  size_t size;
  uint8_t* data = read_whole_file("../../test_corpus_76/megaintr.snd", &size);
  assert(data != NULL);

  uint8_t* unpacked;
  size_t unpacked_size;
  sndh_file_status st = sndh_ice_unpack(data, size, &unpacked, &unpacked_size);
  free(data);
  assert(st == SNDH_FILE_OK);
  assert(unpacked_size == 1032158);
  /* "SNDH" tag lives at offset 12 in every real SNDH file (a handful of
   * 68000 branch opcodes precede it) - sanity-checks that decompression
   * produced real structured data, not just a plausible-looking size. */
  assert(memcmp(unpacked + 12, "SNDH", 4) == 0);

  printf("test_ice_unpacks_real_file: OK (%zu bytes unpacked)\n",
         unpacked_size);
  free(unpacked);
}

/* MIG-0017 update: sndh_file_seek_fast_forward (Atari_SeekTo's real
 * algorithm - CPU/timers only, no audio synthesis at all) reaches
 * exactly the requested tick, without overshooting (unlike the generic
 * decode-and-discard path used by every other format, which can only
 * check tick_count BETWEEN whole render-buffer calls). A real bug was
 * found and fixed during this entry: atari_emulate.h's pending_writes
 * is a FIXED 32-entry queue - flushing it only once at the very end of
 * a long seek let it silently overflow and permanently drop register
 * writes past the first ~32, freezing chip.reg[8..10] at their very-
 * early-seek values regardless of how much further the seek actually
 * went. Regression-guards that specific symptom directly: a short seek
 * and a much longer one must NOT land on identical register state
 * (this file's real 68000 program keeps writing fresh amplitude values
 * every frame, so "still frozen at the short-seek's values" is exactly
 * what the queue-overflow bug looked like, confirmed by hand before the
 * per-iteration-flush fix landed). Doesn't compare against the generic
 * decode-and-discard path's own register state - a real, understood,
 * self-correcting timing difference between the two mechanisms' own
 * buffering granularity (see migration_debt.yaml) makes an exact cross-
 * strategy comparison timing-sensitive/flaky, not a meaningful
 * regression guard. */
static void test_seek_fast_forward_reaches_exact_tick_and_keeps_updating(void) {
  size_t size;
  uint8_t* data = read_whole_file("../../test_corpus_76/More_Short_Demos.sndh", &size);
  assert(data != NULL);

  sndh_file near;
  sndh_file_status st = sndh_file_load(&near, data, size, SNDH_FILE_SAMPLE_RATE_DEF, true);
  assert(st == SNDH_FILE_OK);
  sndh_file_seek_fast_forward(&near, 100);
  assert(near.atari.tick_count == 100); /* exact, no overshoot */

  /* NOTE: this file's music loops, so two arbitrary targets can
   * coincidentally land on the same repeating pattern position with
   * identical register values (confirmed by hand: tick 100 and tick
   * 12000 alias this way) - that's not what this test is guarding
   * against. 15000 was confirmed by hand to differ from 100's own
   * register state, which is what actually matters here. */
  sndh_file far;
  st = sndh_file_load(&far, data, size, SNDH_FILE_SAMPLE_RATE_DEF, true);
  assert(st == SNDH_FILE_OK);
  sndh_file_seek_fast_forward(&far, 15000);
  assert(far.atari.tick_count == 15000);

  bool any_reg_differs = far.ay.chip.reg[8] != near.ay.chip.reg[8] ||
                         far.ay.chip.reg[9] != near.ay.chip.reg[9] ||
                         far.ay.chip.reg[10] != near.ay.chip.reg[10];
  assert(any_reg_differs);

  free(data);
  sndh_file_free(&near);
  sndh_file_free(&far);
  printf("test_seek_fast_forward_reaches_exact_tick_and_keeps_updating: OK\n");
}

/* Confirms sndh_file_seek_fast_forward doesn't crash/hang and reaches
 * the exact target on a real DMA-sound-using file (test_corpus_76/
 * megaintr.snd, once decompressed) - the DMA position catch-up
 * (f->atari.dma.pcurr) is a reasoned-but-not-oracle-validated addition
 * (see sndh_file.c's own comment and migration_debt.yaml), so this
 * guards against a regression breaking it structurally, not against a
 * byte-exact correctness regression there's no oracle for yet. */
static void test_seek_fast_forward_handles_dma_file(void) {
  size_t size;
  uint8_t* data = read_whole_file("../../test_corpus_76/megaintr.snd", &size);
  assert(data != NULL);

  uint8_t* unpacked;
  size_t unpacked_size;
  sndh_file_status st = sndh_ice_unpack(data, size, &unpacked, &unpacked_size);
  free(data);
  assert(st == SNDH_FILE_OK);

  sndh_file f;
  st = sndh_file_load(&f, unpacked, unpacked_size, SNDH_FILE_SAMPLE_RATE_DEF, true);
  free(unpacked);
  assert(st == SNDH_FILE_OK);

  sndh_file_seek_fast_forward(&f, 40);
  assert(f.atari.tick_count == 40 || f.real_end_all);

  sndh_file_free(&f);
  printf("test_seek_fast_forward_handles_dma_file: OK\n");
}

int main(void) {
  test_plays();
  test_ice_corrupt_header_rejected();
  test_ice_unpacks_real_file();
  test_seek_fast_forward_reaches_exact_tick_and_keeps_updating();
  test_seek_fast_forward_handles_dma_file();
  printf("All sndh_file smoke tests passed.\n");
  return 0;
}
