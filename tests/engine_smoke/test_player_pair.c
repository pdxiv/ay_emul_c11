/* MIG-0112: end-to-end regression tests for playlist-level Turbosound
 * pairing (player_pair, engine/src/player.c) - Players.pas's TrModLoaded
 * `else if (TSMode = False) and (PlayListItems[Index]^.Next <> nil)`
 * branch, generalized here across two independently-loaded, possibly
 * DIFFERENT tracker formats sharing one ay_engine's chip/chip2. See
 * README.md. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/formats/stc_file.h"
#include "ay_engine/player.h"

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

/* Pairs two DIFFERENT real corpus files (STC primary, PT1 secondary -
 * TrModLoaded's own Next branch has no format-match requirement, see
 * player.h's player_supports_pairing comment) and confirms: (1) pairing
 * genuinely activates, (2) the mixed output differs from primary-alone
 * output (proving chip 2's contribution is really present, not silently
 * zero - matching MIG-0109's own audible-difference verification
 * rigor), and (3) it isn't just noise - both a paired AND a standalone
 * render produce real audible output. */
static void test_pair_two_different_formats_mixes_both(void) {
  size_t stc_size, pt1_size;
  uint8_t* stc_data = read_whole_file("../../test_corpus_76/AWAY.stc", &stc_size);
  uint8_t* pt1_data = read_whole_file("../../test_corpus_76/DEMON.pt1", &pt1_size);
  assert(stc_data != NULL && "test_corpus_76/AWAY.stc not found");
  assert(pt1_data != NULL && "test_corpus_76/DEMON.pt1 not found");

  /* Primary alone, no pairing. */
  player alone;
  player_status st = player_load_song(&alone, "AWAY.stc", stc_data, stc_size,
                                       STC_FILE_SAMPLE_RATE_DEF, 0, true);
  assert(st == PLAYER_OK);
  assert(player_supports_pairing(&alone));
  int16_t buf_alone[512 * 2];
  int n_alone = player_make_buffer(&alone, buf_alone, 512);
  assert(n_alone == 512);

  /* Same primary file, now paired with a DIFFERENT format (PT1) as the
   * secondary voice. */
  player_pair pair;
  st = player_pair_load_song(&pair, "AWAY.stc", stc_data, stc_size,
                              "DEMON.pt1", pt1_data, pt1_size,
                              STC_FILE_SAMPLE_RATE_DEF, 0, true);
  assert(st == PLAYER_OK);
  assert(pair.active && "expected pairing to activate for two eligible, "
                         "non-self-pairing tracker formats");
  assert(player_ay_engine(&pair.primary)->ts_mode &&
         "pairing must set primary's shared engine to ts_mode");

  int16_t buf_paired[512 * 2];
  int n_paired = player_pair_make_buffer(&pair, buf_paired, 512);
  assert(n_paired == 512);

  bool any_nonzero_alone = false, any_nonzero_paired = false, differs = false;
  for (int i = 0; i < n_paired * 2; i++) {
    if (buf_alone[i] != 0) any_nonzero_alone = true;
    if (buf_paired[i] != 0) any_nonzero_paired = true;
    if (buf_alone[i] != buf_paired[i]) differs = true;
  }
  assert(any_nonzero_alone && "expected audible output from STC alone");
  assert(any_nonzero_paired && "expected audible output from the paired mix");
  assert(differs && "paired output must differ from primary-alone output - "
                     "otherwise chip 2's contribution is silently zero");

  player_free(&alone);
  player_pair_free(&pair);
  free(stc_data);
  free(pt1_data);
  printf("test_pair_two_different_formats_mixes_both: OK\n");
}

/* Players.pas: TrModLoaded only ever consults `Next` `if TSMode =
 * False` - a primary that already self-pairs (PT3's own byte-98 tag,
 * MIG-0109) must always win over any playlist-level Next chain, never
 * double-pair. Alone_Coder_-_PARAM_TS.pt3 is a real, confirmed
 * self-pairing file (used by MIG-0109's own ts_pair_pt3 oracle gate). */
static void test_pair_refused_when_primary_already_self_pairs(void) {
  size_t ts_size, pt1_size;
  uint8_t* ts_data = read_whole_file(
      "../../test_corpus_76/Alone_Coder_-_PARAM_TS.pt3", &ts_size);
  uint8_t* pt1_data = read_whole_file("../../test_corpus_76/DEMON.pt1", &pt1_size);
  assert(ts_data != NULL &&
         "test_corpus_76/Alone_Coder_-_PARAM_TS.pt3 not found");
  assert(pt1_data != NULL && "test_corpus_76/DEMON.pt1 not found");

  player_pair pair;
  player_status st = player_pair_load_song(
      &pair, "Alone_Coder_-_PARAM_TS.pt3", ts_data, ts_size, "DEMON.pt1",
      pt1_data, pt1_size, PT3_FILE_SAMPLE_RATE_DEF, 0, true);
  assert(st == PLAYER_OK);
  assert(pair.primary.as.pt3.ts_mode &&
         "PARAM_TS.pt3 is expected to self-pair via its own byte-98 tag");
  assert(!pair.active &&
         "playlist pairing must be refused when primary already self-pairs");
  assert(pair.secondary_loaded && "secondary still loads (just unused)");

  /* Playback must still work correctly, falling back to primary's own
   * (self-paired) rendering untouched by the refused playlist pair. */
  int16_t buf[512 * 2];
  int n = player_pair_make_buffer(&pair, buf, 512);
  assert(n == 512);

  player_pair_free(&pair);
  free(ts_data);
  free(pt1_data);
  printf("test_pair_refused_when_primary_already_self_pairs: OK\n");
}

/* Players.pas: MakeBufferTracker's `Real_End_All := Real_End[0] and
 * Real_End[1]` (when TSMode) - the pair only reaches its own natural
 * end once BOTH sides have. Hand-constructs two tiny, independently
 * short-lived STC/PT1 sessions (via direct struct field pokes, mirroring
 * this project's existing test_ay.c style) rather than relying on a
 * real file's own duration, so the exact tick counts are known and
 * controllable. */
static void test_pair_real_end_all_requires_both_sides(void) {
  size_t stc_size, pt1_size;
  uint8_t* stc_data = read_whole_file("../../test_corpus_76/AWAY.stc", &stc_size);
  uint8_t* pt1_data = read_whole_file("../../test_corpus_76/DEMON.pt1", &pt1_size);
  assert(stc_data != NULL && pt1_data != NULL);

  player_pair pair;
  player_status st = player_pair_load_song(&pair, "AWAY.stc", stc_data, stc_size,
                                            "DEMON.pt1", pt1_data, pt1_size,
                                            STC_FILE_SAMPLE_RATE_DEF, 0, true);
  assert(st == PLAYER_OK && pair.active);

  /* Force primary (STC) to end almost immediately; leave secondary
   * (PT1) with its own real, much longer duration untouched. */
  pair.primary.as.stc.global_tick_max = 1;
  pair.primary.as.stc.global_tick_counter = 1;

  assert(!player_pair_real_end_all(&pair) &&
         "pair must NOT be done yet - secondary is still playing, matching "
         "Real_End_All's AND-of-both semantics");

  int16_t buf[512 * 2];
  player_pair_make_buffer(&pair, buf, 512);
  assert(player_real_end_all(&pair.primary) &&
         "primary should have reached its own forced end");
  assert(!player_pair_real_end_all(&pair) &&
         "pair as a whole must still be alive - secondary keeps it going, "
         "exactly like a real mismatched-length TS pair in the original");

  /* Now also force secondary to end - only then must the pair end. */
  pair.secondary.as.pt1.global_tick_max = 1;
  pair.secondary.as.pt1.global_tick_counter = 1;
  player_pair_make_buffer(&pair, buf, 512);
  assert(player_pair_real_end_all(&pair) &&
         "pair must end once BOTH sides have reached their own natural end");

  player_pair_free(&pair);
  free(stc_data);
  free(pt1_data);
  printf("test_pair_real_end_all_requires_both_sides: OK\n");
}

/* MIG-0114: CheckLoopAndStop's exact Force_Loop semantics (Players.pas:
 * 8730-8746) - `if Do_Loop or Force_Loop then Counter := Max; if not
 * Do_Loop then begin Real_End := True; if not Force_Loop then Exit(True);
 * end;`. Confirms: (1) without force_loop, once the tick budget is
 * reached, step_registers permanently returns false (register generation
 * stops - a voice freezes on its last written registers) and real_end_all
 * latches true; (2) WITH force_loop, real_end_all ALSO latches true
 * (Force_Loop doesn't change that), but step_registers keeps returning
 * true (register generation continues - the voice keeps audibly looping
 * its own pattern data, the real "shorter TS-pair voice" use case) and
 * global_tick_counter stays pinned at global_tick_max rather than
 * drifting upward. */
static void test_force_loop_keeps_generating_past_natural_end(void) {
  size_t stc_size;
  uint8_t* stc_data = read_whole_file("../../test_corpus_76/AWAY.stc", &stc_size);
  assert(stc_data != NULL);

  /* Without force_loop: step_registers freezes once ended. */
  {
    stc_file f;
    stc_file_status st =
        stc_file_load(&f, stc_data, stc_size, STC_FILE_SAMPLE_RATE_DEF);
    assert(st == STC_FILE_OK);
    f.global_tick_max = 5;
    f.global_tick_counter = 5;
    assert(!f.force_loop);

    bool ok = stc_file_step_registers(&f, &f.ay.chip);
    assert(!ok && "must stop generating once ended, with force_loop off");
    assert(f.real_end_all);
    assert(f.global_tick_counter == 5 && "counter stays pinned at max");

    /* Idempotent - stays stopped on repeat calls. */
    ok = stc_file_step_registers(&f, &f.ay.chip);
    assert(!ok);
    assert(f.global_tick_counter == 5);
  }

  /* With force_loop: step_registers keeps generating past the end. */
  {
    stc_file f;
    stc_file_status st =
        stc_file_load(&f, stc_data, stc_size, STC_FILE_SAMPLE_RATE_DEF);
    assert(st == STC_FILE_OK);
    f.global_tick_max = 5;
    f.global_tick_counter = 5;
    f.force_loop = true;

    bool ok = stc_file_step_registers(&f, &f.ay.chip);
    assert(ok && "must KEEP generating once ended, with force_loop on");
    assert(f.real_end_all && "real_end_all still latches true regardless "
                              "of force_loop");
    /* CheckLoopAndStop's own clamp (`Counter := Max`) happens at the TOP
     * of the NEXT check, BEFORE that call's own stc_get_registers
     * increments it again - so immediately after any one call the
     * counter reads max+1, not max; the important invariant is that it
     * never drifts past max+1 (i.e. gets reclamped every time, not left
     * to grow unboundedly). */
    assert(f.global_tick_counter == 6);

    /* Repeat a few more times - must keep generating (not get stuck),
     * and the counter must stay bounded at max+1 every time, never
     * drifting upward. */
    for (int i = 0; i < 5; i++) {
      ok = stc_file_step_registers(&f, &f.ay.chip);
      assert(ok);
      assert(f.global_tick_counter == 6);
    }
  }

  free(stc_data);
  printf("test_force_loop_keeps_generating_past_natural_end: OK\n");
}

/* player_pair_set_force_loop must reach both sides (Force_Loop is a
 * single shared setting in the original too, consulted identically by
 * both CheckLoopAndStop(0)/(1) calls). */
static void test_pair_set_force_loop_reaches_both_sides(void) {
  size_t stc_size, pt1_size;
  uint8_t* stc_data = read_whole_file("../../test_corpus_76/AWAY.stc", &stc_size);
  uint8_t* pt1_data = read_whole_file("../../test_corpus_76/DEMON.pt1", &pt1_size);
  assert(stc_data != NULL && pt1_data != NULL);

  player_pair pair;
  player_status st = player_pair_load_song(&pair, "AWAY.stc", stc_data, stc_size,
                                            "DEMON.pt1", pt1_data, pt1_size,
                                            STC_FILE_SAMPLE_RATE_DEF, 0, true);
  assert(st == PLAYER_OK && pair.active);
  assert(!pair.primary.as.stc.force_loop && !pair.secondary.as.pt1.force_loop);

  player_pair_set_force_loop(&pair, true);
  assert(pair.primary.as.stc.force_loop);
  assert(pair.secondary.as.pt1.force_loop);

  player_pair_free(&pair);
  free(stc_data);
  free(pt1_data);
  printf("test_pair_set_force_loop_reaches_both_sides: OK\n");
}

int main(void) {
  test_pair_two_different_formats_mixes_both();
  test_pair_refused_when_primary_already_self_pairs();
  test_pair_real_end_all_requires_both_sides();
  test_force_loop_keeps_generating_past_natural_end();
  test_pair_set_force_loop_reaches_both_sides();
  printf("All player_pair smoke tests passed.\n");
  return 0;
}
