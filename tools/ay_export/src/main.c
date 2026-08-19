/* ay_export - CLI register-write-log exporter (MIG-0010): ports
 * Convs.pas's PSG_Converter/VTX_Converter (the VBL2PSG/VBL2VTX branches
 * - see engine/include/ay_engine/psg_export.h and
 * tools/ay_export/include/ay_export/vtx_export.h for the full per-
 * format citations and scope notes). Reaches all formats this port can
 * load a player from (player_step_registers_any), including AY/YM/VTX/
 * SNDH as export SOURCES; only the FT.OUT/FT.ZXAY/FT.EPSG raw-register-
 * trace INPUT formats (no loader at all in this port) remain out of
 * scope (see migration_debt.yaml).
 *
 * Usage: ay_export <file> --psg=<path> | --vtx=<path> [--ay-freq=N]
 *   [--loop-vbl=N] [--title=S] [--author=S] [--programm=S]
 *   [--tracker=S] [--comment=S]
 *   Both --psg and --vtx may be given together to export both formats
 *   from one load. If the loaded file is a real Turbosound pair (.ayl
 *   "ts"), PSG export writes two files (<path> and a "2"-suffixed
 *   sibling, matching PSG_Converter's own GetTSFileName convention) -
 *   VTX export writes only the primary voice (VTX_Converter itself has
 *   no second-chip concept - see vtx_export.h).
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/player.h"
#include "ay_engine/psg_export.h"
#include "ay_export/vtx_export.h"

static uint8_t* read_whole_file(const char* path, size_t* out_size) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* buf = (uint8_t*)malloc((size_t)sz);
  if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *out_size = (size_t)sz;
  return buf;
}

/* Convs.pas's PSG_Converter own GetTSFileName(FN, 2) convention for the
 * second voice's own filename - inserts "2" before the extension (e.g.
 * "song.psg" -> "song2.psg"). */
static void make_ts_second_path(const char* path, char* out, size_t out_cap) {
  const char* dot = strrchr(path, '.');
  size_t base_len = dot ? (size_t)(dot - path) : strlen(path);
  if (base_len + 1 + strlen(dot ? dot : "") + 1 > out_cap) {
    out[0] = '\0';
    return;
  }
  memcpy(out, path, base_len);
  out[base_len] = '2';
  strcpy(out + base_len + 1, dot ? dot : "");
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "usage: %s <file> --psg=<path> | --vtx=<path> [--ay-freq=N] "
            "[--loop-vbl=N] [--title=S] [--author=S] [--programm=S] "
            "[--tracker=S] [--comment=S] [--force-loop=0|1]\n",
            argv[0]);
    return 1;
  }

  const char* psg_path = NULL;
  const char* vtx_path = NULL;
  int ay_freq = 0;
  int loop_vbl = 0;
  bool loop_vbl_set = false;
  const char* title = NULL;
  const char* author = NULL;
  const char* programm = NULL;
  const char* tracker = NULL;
  const char* comment = NULL;
  /* settings.pas: `Force_Loop:boolean = True` - the real app's own
   * default (Tools.pas's CBForceLoop starts checked). Only affects a
   * genuine TSMode (Turbosound pair) export of two DIFFERENT-length
   * files: with it on, the shorter voice keeps looping/generating
   * fresh register content (not silence) for the padding duration,
   * matching CheckLoopAndStop's real behavior (Players.pas:8732-8746,
   * already ported for WAV playback pairing by MIG-0114's own
   * force_loop field - this just wires the same mechanism in here). */
  bool force_loop = true;

  int i;
  for (i = 2; i < argc; i++) {
    if (strncmp(argv[i], "--psg=", 6) == 0) psg_path = argv[i] + 6;
    else if (strncmp(argv[i], "--vtx=", 6) == 0) vtx_path = argv[i] + 6;
    else if (strncmp(argv[i], "--ay-freq=", 10) == 0) ay_freq = atoi(argv[i] + 10);
    else if (strncmp(argv[i], "--loop-vbl=", 11) == 0) {
      loop_vbl = atoi(argv[i] + 11);
      loop_vbl_set = true;
    }
    else if (strncmp(argv[i], "--title=", 8) == 0) title = argv[i] + 8;
    else if (strncmp(argv[i], "--author=", 9) == 0) author = argv[i] + 9;
    else if (strncmp(argv[i], "--programm=", 11) == 0) programm = argv[i] + 11;
    else if (strncmp(argv[i], "--tracker=", 10) == 0) tracker = argv[i] + 10;
    else if (strncmp(argv[i], "--comment=", 10) == 0) comment = argv[i] + 10;
    else if (strncmp(argv[i], "--force-loop=", 13) == 0) force_loop = atoi(argv[i] + 13) != 0;
    else {
      fprintf(stderr, "ay_export: unrecognized argument '%s'\n", argv[i]);
      return 1;
    }
  }
  if (!psg_path && !vtx_path) {
    fprintf(stderr, "ay_export: need at least one of --psg=<path> or --vtx=<path>\n");
    return 1;
  }

  size_t size;
  uint8_t* data = read_whole_file(argv[1], &size);
  if (!data) {
    fprintf(stderr, "ay_export: cannot read '%s'\n", argv[1]);
    return 1;
  }

  player_pair pair;
  player_status st = player_pair_load_song(&pair, argv[1], data, size, NULL,
                                            NULL, 0, 44100, 0, true);
  free(data);
  if (st != PLAYER_OK) {
    fprintf(stderr, "ay_export: failed to load '%s' (status %d)\n", argv[1],
            (int)st);
    return 1;
  }
  if (pair.primary.format == PLAYER_FORMAT_UNKNOWN) {
    fprintf(stderr,
            "ay_export: '%s' could not be recognized as any supported "
            "format\n",
            argv[1]);
    player_pair_free(&pair);
    return 1;
  }

  player_pair_set_force_loop(&pair, force_loop);

  /* MIG-0010 update: PlayList.pas:686's `LoopVBL := Loop;` ultimately
   * traces back to a PLAYLIST ITEM's own persisted Loop field (a GUI/
   * playlist concept ay_export, a bare-file CLI tool, has no equivalent
   * of - see migration_debt.yaml for why title/author/programm/tracker-
   * name can't be auto-derived the same way Convs.pas itself does,
   * i.e. from CurItem.Title/Author/Programm and GetEditorString, not
   * from the file's own content at all). VTX/YM are the one case where
   * a freshly-added playlist item's Loop field would, in practice,
   * equal the file's own embedded native loop point (Players.pas's
   * loaders populate LoopVBL directly from it at load time - see
   * OracleHarness.pas's RunYMPSGExportTest/RunVTXPSGExportTest, both
   * `LoopVBL := ...Loop...; if LoopVBL < 0 then LoopVBL := 0;`) - this
   * port's own vtx_file.h/ym_file.h already carry that same value in
   * loop_vbl, so defaulting to it here (only when the user didn't pass
   * --loop-vbl explicitly) is a faithful stand-in for that common case. */
  if (!loop_vbl_set) {
    if (pair.primary.format == PLAYER_FORMAT_VTX)
      loop_vbl = pair.primary.as.vtx.loop_vbl;
    else if (pair.primary.format == PLAYER_FORMAT_YM)
      loop_vbl = pair.primary.as.ym.loop_vbl;
  }

  int rc = 0;

  if (psg_path) {
    bool ok;
    if (pair.active) {
      char path2[4096];
      make_ts_second_path(psg_path, path2, sizeof(path2));
      ok = psg_export_write_pair(psg_path, path2, &pair);
    } else {
      ok = psg_export_write(psg_path, &pair.primary);
    }
    if (ok) {
      printf("ay_export: wrote %s%s\n", psg_path, pair.active ? " (+ TS pair)" : "");
    } else {
      fprintf(stderr, "ay_export: PSG export failed\n");
      rc = 1;
    }
  }

  if (vtx_path) {
    bool ok = pair.active
                  ? vtx_export_write_pair(vtx_path, &pair, ay_freq, title,
                                           author, programm, tracker, comment,
                                           loop_vbl)
                  : vtx_export_write(vtx_path, &pair.primary, ay_freq, title,
                                      author, programm, tracker, comment,
                                      loop_vbl);
    if (ok) {
      printf("ay_export: wrote %s\n", vtx_path);
    } else {
      fprintf(stderr, "ay_export: VTX export failed\n");
      rc = 1;
    }
  }

  player_pair_free(&pair);
  return rc;
}
