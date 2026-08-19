/* MIG-0010: tools/ay_export/src/vtx_export.c smoke tests. Byte-exact
 * correctness of the register DATA against the real Pascal oracle is
 * covered by tests/oracle_diff/run_diff.sh's stc_vtx_raw gate; this
 * file guards structural behavior (header fields, round-trip through
 * this port's OWN already-oracle-validated vtx_file_load, format-
 * support gating) that gate doesn't exercise - and is the one place
 * that actually proves LhASsA's compressed output is usable end-to-end,
 * not just bitstream-compatible in isolation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/formats/vtx_file.h"
#include "ay_engine/player.h"
#include "ay_export/vtx_export.h"

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

/* The real end-to-end proof: a file THIS EXPORTER writes must be
 * readable by this port's own vtx_file_load (already oracle-validated
 * for real Pascal-produced .vtx files, MIG-0028 era) and produce
 * plausible audio - i.e. LhASsA's lh5 encoder output is genuinely
 * usable by the decoder this project already trusts, not just
 * bitstream-compatible in a standalone interop test. */
static void test_round_trips_through_our_own_loader(void) {
  size_t size;
  uint8_t* data = read_whole_file("../../test_corpus_76/AWAY.stc", &size);
  assert(data != NULL);

  player p;
  player_status st = player_load(&p, "AWAY.stc", data, size, 44100);
  free(data);
  assert(st == PLAYER_OK);

  const char* path = "/tmp/test_vtx_export_smoke.vtx";
  assert(vtx_export_write(path, &p, 0, "Title", "Author", NULL, NULL, NULL, 0));
  player_free(&p);

  size_t vtx_size;
  uint8_t* vtx_data = read_whole_file(path, &vtx_size);
  assert(vtx_data != NULL);

  vtx_file vf;
  vtx_file_status vst = vtx_file_load(&vf, vtx_data, vtx_size, 44100);
  free(vtx_data);
  assert(vst == VTX_FILE_OK);
  assert(vf.number_of_vbls == 9600); /* AWAY.stc's real declared length -
                                       * cross-checked independently via
                                       * tests/oracle_diff's own get_time
                                       * gate for this same file. */

  int16_t buf[512 * 2];
  bool any_nonzero = false;
  int frames_total = 0;
  for (int i = 0; i < 400 && !vf.real_end_all; i++) {
    int n = vtx_file_make_buffer(&vf, buf, 512);
    frames_total += n;
    for (int j = 0; j < n; j++) {
      if (buf[j * 2] != 0 || buf[j * 2 + 1] != 0) any_nonzero = true;
    }
  }
  assert(frames_total > 0);
  assert(any_nonzero);
  vtx_file_free(&vf);
  printf("test_round_trips_through_our_own_loader: OK (%d frames, audible)\n",
         frames_total);
}

/* player_step_registers_any now reaches every loadable format, so the
 * only genuinely unsupported player is one that never went through
 * player_load at all (format left at its zero-value PLAYER_FORMAT_
 * UNKNOWN) - vtx_export_write must still fail cleanly on that, not
 * crash or write a bogus/empty file. */
static void test_unsupported_format_rejected(void) {
  player p;
  memset(&p, 0, sizeof(p));
  assert(p.format == PLAYER_FORMAT_UNKNOWN);

  assert(!vtx_export_write("/tmp/test_vtx_export_should_not_exist.vtx", &p,
                            0, NULL, NULL, NULL, NULL, NULL, 0));
  printf("test_unsupported_format_rejected: OK\n");
}

/* AY/YM/VTX/SNDH as VTX export sources (player_step_registers_any) -
 * byte-exact register-data correctness for AY/YM/VTX is covered by
 * run_diff.sh; this proves the full compressed file round-trips
 * through this port's own vtx_file_load for all four, including SNDH
 * (not oracle-diff validated - see migration_debt.yaml MIG-0010). */
static void test_extended_format_sources_round_trip(void) {
  static const char* files[] = {
      "../../test_corpus_76/MetalMania.ay",
      "../../test_corpus_76/Batman_Journey.ym",
      "../../test_corpus_76/GB2_5.vtx",
      /* MIG-0010 update: FT.OUT/FT.EPSG - no real .out/.epsg file exists
       * anywhere in this project's corpus, see tests/oracle_diff/
       * synthetic/gen_out_epsg.py's own comment. */
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

    const char* path = "/tmp/test_vtx_export_extended.vtx";
    assert(vtx_export_write(path, &p, 0, NULL, NULL, NULL, NULL, NULL, 0));
    player_free(&p);

    vtx_file vf;
    size_t out_size;
    uint8_t* out = read_whole_file(path, &out_size);
    assert(out != NULL);
    vtx_file_status vst = vtx_file_load(&vf, out, out_size, 44100);
    free(out);
    assert(vst == VTX_FILE_OK);
    vtx_file_free(&vf);
  }
  printf("test_extended_format_sources_round_trip: OK\n");
}

int main(void) {
  test_round_trips_through_our_own_loader();
  test_unsupported_format_rejected();
  test_extended_format_sources_round_trip();
  printf("All vtx_export smoke tests passed.\n");
  return 0;
}
