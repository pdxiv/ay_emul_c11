/* MIG-0010: engine/src/psg_export.c smoke tests. Byte-exact correctness
 * against the real Pascal oracle is covered by tests/oracle_diff/
 * run_diff.sh's stc_psg_export gate - this file guards structural
 * behavior (header, format-support gating, TS-pair writing two files)
 * that gate doesn't exercise. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/player.h"
#include "ay_engine/psg_export.h"

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

static void test_writes_valid_header_and_content(void) {
  size_t size;
  uint8_t* data = read_whole_file("../../test_corpus_76/AWAY.stc", &size);
  assert(data != NULL);

  player p;
  player_status st = player_load(&p, "AWAY.stc", data, size, 44100);
  free(data);
  assert(st == PLAYER_OK);

  const char* path = "/tmp/test_psg_export_smoke.psg";
  assert(psg_export_write(path, &p));
  player_free(&p);

  size_t out_size;
  uint8_t* out = read_whole_file(path, &out_size);
  assert(out != NULL);
  assert(out_size > 16);
  /* "PSG\x1a" magic + 12 zero bytes (Convs.pas's PSG constant). */
  static const uint8_t expected_magic[16] = {0x50, 0x53, 0x47, 0x1a, 0, 0, 0, 0,
                                              0,    0,    0,    0,    0, 0, 0, 0};
  assert(memcmp(out, expected_magic, 16) == 0);
  free(out);
  printf("test_writes_valid_header_and_content: OK (%zu bytes)\n", out_size);
}

/* player_step_registers_any now reaches every loadable format, so the
 * only genuinely unsupported player is one that never went through
 * player_load at all (format left at its zero-value PLAYER_FORMAT_
 * UNKNOWN) - psg_export_write must still fail cleanly on that, not
 * crash or write a bogus/empty file. */
static void test_unsupported_format_rejected(void) {
  player p;
  memset(&p, 0, sizeof(p));
  assert(p.format == PLAYER_FORMAT_UNKNOWN);

  assert(!psg_export_write("/tmp/test_psg_export_should_not_exist.psg", &p));
  printf("test_unsupported_format_rejected: OK\n");
}

/* A real Turbosound pair (same file loaded as both voices, matching
 * this port's own confirmed same-file-twice real .ayl "ts" pairing
 * semantics - see gui/include/gui/playlist.h) must write TWO files via
 * psg_export_write_pair, each independently valid. Not oracle-diff
 * validated (unlike the single-voice case) - see migration_debt.yaml. */
static void test_pair_writes_two_files(void) {
  size_t size;
  uint8_t* data = read_whole_file("../../test_corpus_76/AWAY.stc", &size);
  assert(data != NULL);

  player_pair pair;
  player_status st =
      player_pair_load_song(&pair, "AWAY.stc", data, size, "AWAY.stc", data,
                             size, 44100, 0, true);
  free(data);
  assert(st == PLAYER_OK);
  assert(pair.active);

  const char* path1 = "/tmp/test_psg_export_pair1.psg";
  const char* path2 = "/tmp/test_psg_export_pair2.psg";
  assert(psg_export_write_pair(path1, path2, &pair));
  player_pair_free(&pair);

  size_t s1, s2;
  uint8_t* d1 = read_whole_file(path1, &s1);
  uint8_t* d2 = read_whole_file(path2, &s2);
  assert(d1 != NULL && d2 != NULL);
  assert(s1 > 16 && s2 > 16);
  /* Same file paired with itself -> both streams identical. */
  assert(s1 == s2 && memcmp(d1, d2, s1) == 0);
  free(d1);
  free(d2);
  printf("test_pair_writes_two_files: OK (%zu bytes each)\n", s1);
}

/* AY/YM/VTX/SNDH as PSG export sources (player_step_registers_any) -
 * byte-exact correctness for AY/YM/VTX is covered by run_diff.sh's
 * ay_psg_export/ym_psg_export/vtx_psg_export gates; this just guards
 * that all four produce a valid, non-trivial file end to end. */
static void test_extended_format_sources_write(void) {
  static const char* files[] = {
      "../../test_corpus_76/MetalMania.ay",
      "../../test_corpus_76/Batman_Journey.ym",
      "../../test_corpus_76/GB2_5.vtx",
      /* MIG-0010 update: FT.OUT/FT.EPSG - byte-exact correctness is
       * covered by run_diff.sh's out_psg_export/epsg_psg_export gates
       * (against the same synthetic fixtures, no real .out/.epsg file
       * exists anywhere in this project's corpus). */
      "../../tests/oracle_diff/synthetic/test.out",
      "../../tests/oracle_diff/synthetic/test.epsg",
  };
  size_t i;
  for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    size_t size;
    uint8_t* data = read_whole_file(files[i], &size);
    assert(data != NULL);

    player p;
    player_status st = player_load(&p, files[i], data, size, 44100);
    free(data);
    assert(st == PLAYER_OK);

    const char* path = "/tmp/test_psg_export_extended.psg";
    assert(psg_export_write(path, &p));
    player_free(&p);

    size_t out_size;
    uint8_t* out = read_whole_file(path, &out_size);
    assert(out != NULL);
    assert(out_size > 16);
    free(out);
  }
  printf("test_extended_format_sources_write: OK\n");
}

int main(void) {
  test_writes_valid_header_and_content();
  test_unsupported_format_rejected();
  test_pair_writes_two_files();
  test_extended_format_sources_write();
  printf("All psg_export smoke tests passed.\n");
  return 0;
}
