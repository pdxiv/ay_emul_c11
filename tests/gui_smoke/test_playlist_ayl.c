/* MIG-0112: regression tests for gui/src/playlist.c's .ayl loader
 * against two real playlist files - test_corpus_76/Cmnd.ayl (a real,
 * user-supplied .ayl exactly as authored by a real user, exercising
 * quirks a synthetic fixture wouldn't: a "v1.6" version tag and
 * Windows-style `\` path separators) and test_corpus_76/ts_pair_test.ayl
 * (a small, hand-authored-but-grammar-verified fixture with a genuine
 * "ts" Turbosound-pairing block, since no real-world example of that
 * specific feature was available). See README.md. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/player.h"
#include "gui/playlist.h"

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

/* Cmnd.ayl is a real, in-the-wild playlist file: version tag "v1.6"
 * (this port's AYL_VERSION_STRING check must accept any "v1.*", not
 * just the "v1.6" this port itself writes) and 19 Windows-style
 * (`\`-separated) relative paths, none of which exist in this repo's
 * corpus (they reference an external, unavailable "Authors\..." tree).
 * Confirms: (1) the loader doesn't crash or reject the file outright
 * on an unfamiliar-but-compatible version tag, (2) ayl_check_path's
 * backslash-to-forward-slash fallback (Players.pas:315-320's CheckPath,
 * `{$IFNDEF Windows}`-guarded in the real Linux build) is exercised
 * without crashing even when the ultimately-resolved path still doesn't
 * exist, (3) missing referenced files are skipped gracefully (0 items
 * added), matching CheckAndAddFromPLFile's own real "file not found"
 * behavior - never a hard failure for the whole playlist. */
static void test_real_ayl_loads_without_crashing(void) {
  gui_playlist pl;
  gui_playlist_init(&pl);
  int added = gui_playlist_load_ayl(&pl, "../../test_corpus_76/Cmnd.ayl", NULL);
  assert(added == 0 && "none of Cmnd.ayl's referenced files exist in this "
                        "repo - 0 added is the correct, graceful outcome");
  assert(pl.count == 0);
  gui_playlist_free(&pl);
  printf("test_real_ayl_loads_without_crashing: OK\n");
}

/* ts_pair_test.ayl: AWAY.stc (a real corpus file) with two consecutive
 * `<...>` blocks - PlayList.pas's LoadPLItem/SavePLItem confirm a real
 * "ts" pair always reuses the SAME path for both voices (see
 * gui_playlist_entry's own comment for the full trace), so the second
 * block's own distinct ChipFrequency override is what proves this is
 * genuinely a SEPARATE override set, not a mis-parsed duplicate of the
 * first block. */
static void test_real_ts_pair_ayl_parses_and_plays(void) {
  gui_playlist pl;
  gui_playlist_init(&pl);
  int added =
      gui_playlist_load_ayl(&pl, "../../test_corpus_76/ts_pair_test.ayl", NULL);
  assert(added == 1);
  assert(pl.count == 1);

  gui_playlist_entry* e = &pl.items[0];
  assert(e->has_ts_pair);
  assert(e->overrides.ay_freq == 1750000);
  assert(e->ts_pair_overrides.ay_freq == 2000000);
  assert(strstr(e->display, "[x2]") != NULL &&
         "playlist display must show the pairing indicator");

  /* Drive it through the real production entry point (player_pair_
   * load_song), exactly like gui/src/mainwin.c's do_load_song does,
   * and confirm it produces genuinely audible, genuinely dual-chip
   * output - the deeper byte-for-byte oracle proof that THIS mechanism
   * (two independently-loaded voices sharing one engine) matches the
   * real Pascal exactly lives in tests/oracle_diff's own stc_pair gate
   * (run_diff.sh); this test instead proves the REAL .ayl file, loaded
   * through the REAL gui_playlist_load_ayl, correctly drives that same
   * mechanism end to end. */
  size_t size;
  uint8_t* data = read_whole_file(e->path, &size);
  assert(data != NULL);

  player_pair pair;
  player_status st = player_pair_load_song(
      &pair, e->path, data, size, e->has_ts_pair ? e->path : NULL,
      e->has_ts_pair ? data : NULL, e->has_ts_pair ? size : 0, 48000, 0, true);
  assert(st == PLAYER_OK);
  assert(pair.active);

  player alone;
  st = player_load_song(&alone, e->path, data, size, 48000, 0, true);
  assert(st == PLAYER_OK);

  int16_t buf_alone[512 * 2], buf_paired[512 * 2];
  int n_alone = player_make_buffer(&alone, buf_alone, 512);
  int n_paired = player_pair_make_buffer(&pair, buf_paired, 512);
  assert(n_alone == 512 && n_paired == 512);

  bool any_nonzero = false, differs = false;
  for (int i = 0; i < 512 * 2; i++) {
    if (buf_paired[i] != 0) any_nonzero = true;
    if (buf_alone[i] != buf_paired[i]) differs = true;
  }
  assert(any_nonzero && "expected audible output");
  assert(differs && "paired output must differ from primary-alone output");

  free(data);
  player_free(&alone);
  player_pair_free(&pair);
  gui_playlist_free(&pl);
  printf("test_real_ts_pair_ayl_parses_and_plays: OK\n");
}

/* MIG-0118: PLDef global-defaults header round-trip - save with a real
 * gui_playlist_defaults set, reload, confirm the SAME values come back
 * via out_defaults, AND confirm a per-item override that matches the
 * saved PLDef was correctly OMITTED from that item's own block (so
 * reloading shows it as unset - the diffing is genuinely load-bearing,
 * not just a header round-trip with everything re-duplicated per item
 * anyway). */
static void test_pldef_header_round_trips(void) {
  /* Absolute path - the .ayl this test writes lands in /tmp, so a
   * relative "../../test_corpus_76/..." path (correct from THIS test
   * binary's own cwd) would resolve wrong once re-read relative to
   * /tmp's own directory. */
  char abs_path[4096];
  assert(realpath("../../test_corpus_76/AWAY.stc", abs_path) != NULL);

  gui_playlist pl;
  gui_playlist_init(&pl);
  int added = gui_playlist_add_file(&pl, abs_path);
  assert(added == 1);
  assert(pl.count == 1);

  gui_playlist_defaults defaults;
  gui_playlist_defaults_init(&defaults);
  defaults.has_chip_type = true;
  defaults.chip_type = AY_CHIP_TYPE_YM;
  defaults.channel_mode = 3; /* BAC */
  defaults.ay_freq = 1750000;
  defaults.int_freq = 50000;

  /* This item's own chip_type override matches the PLDef default -
   * SavePLItem's own `<> PLDef_X` guard means this should NOT appear
   * in the item's own block at all. ay_freq deliberately DIFFERS, so
   * it should still be written per-item. */
  pl.items[0].overrides.has_chip_type = true;
  pl.items[0].overrides.chip_type = AY_CHIP_TYPE_YM;
  pl.items[0].overrides.ay_freq = 2000000;

  const char* path = "/tmp/test_pldef_header.ayl";
  assert(gui_playlist_save_ayl(&pl, path, &defaults));
  gui_playlist_free(&pl);

  gui_playlist pl2;
  gui_playlist_init(&pl2);
  gui_playlist_defaults loaded_defaults;
  gui_playlist_defaults_init(&loaded_defaults);
  int added2 = gui_playlist_load_ayl(&pl2, path, &loaded_defaults);
  assert(added2 == 1);
  assert(pl2.count == 1);

  assert(loaded_defaults.has_chip_type);
  assert(loaded_defaults.chip_type == AY_CHIP_TYPE_YM);
  assert(loaded_defaults.channel_mode == 3);
  assert(loaded_defaults.ay_freq == 1750000);
  assert(loaded_defaults.int_freq == 50000);

  /* The item's own chip_type matched PLDef and was omitted on save, so
   * it comes back unset on reload (has_chip_type == false) - a real
   * consequence of the diffing, not just "the header round-trips". */
  assert(!pl2.items[0].overrides.has_chip_type);
  assert(pl2.items[0].overrides.ay_freq == 2000000);

  gui_playlist_free(&pl2);
  printf("test_pldef_header_round_trips: OK\n");
}

/* MIG-0118: multi-song .ay round-trip - MetalMania.ay (a real 12-
 * subsong file) saved and reloaded must come back as its own full 12
 * entries (not collapsed to 1), each with the correct song_index, and
 * NOT as 12*12=144 entries (confirming add_single_song_entry is really
 * loading one subsong per FormatSpec line, not re-expanding). */
static void test_multi_song_ay_round_trips(void) {
  char abs_path[4096];
  assert(realpath("../../test_corpus_76/MetalMania.ay", abs_path) != NULL);

  gui_playlist pl;
  gui_playlist_init(&pl);
  int added = gui_playlist_add_file(&pl, abs_path);
  assert(added == 12);
  assert(pl.count == 12);

  const char* path = "/tmp/test_multi_song.ayl";
  assert(gui_playlist_save_ayl(&pl, path, NULL));
  gui_playlist_free(&pl);

  gui_playlist pl2;
  gui_playlist_init(&pl2);
  int added2 = gui_playlist_load_ayl(&pl2, path, NULL);
  assert(added2 == 12 && "must round-trip to exactly 12, not 1 (the old "
                          "collapse-to-song-0 bug) or 144 (N*song_count)");
  assert(pl2.count == 12);
  for (int i = 0; i < 12; i++) {
    assert(pl2.items[i].song_index == i);
    assert(pl2.items[i].song_count == 12);
  }
  gui_playlist_free(&pl2);
  printf("test_multi_song_ay_round_trips: OK\n");
}

int main(void) {
  test_real_ayl_loads_without_crashing();
  test_real_ts_pair_ayl_parses_and_plays();
  test_pldef_header_round_trips();
  test_multi_song_ay_round_trips();
  printf("All playlist .ayl smoke tests passed.\n");
  return 0;
}
