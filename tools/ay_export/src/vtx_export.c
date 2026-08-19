#include "ay_export/vtx_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lh.h"

/* Convs.pas:897-1064's VTX_Converter. Ported: the header build (1003-
 * 1039), VTX_Save_Registers (902-915), VBL2VTX (958-969), and the LZH
 * compression step (Encode_Buffer_To_File(p), 1053 - via LhASsA's
 * lh_compress_lh5 instead, see this file's own header comment). Not
 * ported: OUT2VTX/ZXAY2VTX/EPSG2VTX and the GUI-entangled setup around
 * it (FrmPLst.GetVarsForSave, VTX_Header_Editor's interactive dialog,
 * ShowProgress, Application.ProcessMessages, LongProcessPrepare/Done -
 * none of which affect the bytes actually written). */

static void put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static bool write_str(FILE* f, const char* s) {
  size_t n = s ? strlen(s) : 0;
  if (n > 0 && fwrite(s, 1, n, f) != n) return false;
  return fputc(0, f) != EOF;
}

/* Convs.pas:1003-1022's `with VTX_Hdr do begin ... end` - the 16-byte
 * TVTXFileHeader (Players.pas:261-269), little-endian throughout
 * (matches this port's existing vtx_file.c loader, whose own file
 * comment already confirms the LE convention by direct reading of the
 * original). */
static void build_header(uint8_t out[16], ay_chip_type chip_type,
                          uint16_t loop_vbl, uint32_t ay_freq,
                          uint8_t inter_frq, uint32_t unpack_size) {
  put16(out + 0, chip_type == AY_CHIP_TYPE_YM ? 0x6d79u : 0x7961u);
  out[2] = 1; /* Mode - always 1 (mono/interleaved AY output; this
               * port's own VTX loader never reads Mode at all, so its
               * exact value is cosmetic, but 1 matches what a real
               * mono AY/YM source always produces). */
  put16(out + 3, loop_vbl);
  put32(out + 5, ay_freq);
  out[9] = inter_frq;
  put16(out + 10, 0); /* Year - Convs.pas always writes 0 for a fresh
                        * conversion (no source date to carry over). */
  put32(out + 12, unpack_size);
}

/* Shared core: fills `regs` (14*total_frames bytes, column-major -
 * regs[reg*total_frames + frame], matching vtx_file.c's own loader-side
 * `k += number_of_vbls` indexing exactly) by driving `p`/`p2` forward
 * via player_step_registers, then writes the header+strings+compressed
 * body to `path`. `p2` may be NULL (single-voice export). */
static bool build_regs(player* p, player* p2, uint8_t** out_regs,
                        size_t* out_regs_size) {
  int64_t counter, max, counter2, max2, total;
  uint8_t* regs;
  ay_chip* chip;
  int64_t frame;

  if (p->format == PLAYER_FORMAT_UNKNOWN) return false;
  if (!player_get_tick_position(p, &counter, &max)) return false;
  total = max;
  if (p2) {
    if (p2->format == PLAYER_FORMAT_UNKNOWN) return false;
    if (!player_get_tick_position(p2, &counter2, &max2)) return false;
    if (max2 > total) total = max2;
  }
  if (total <= 0) return false;

  *out_regs_size = (size_t)total * 14;
  regs = (uint8_t*)calloc(1, *out_regs_size);
  if (!regs) return false;

  chip = &player_ay_engine(p)->chip;

  for (frame = 0; frame < total; frame++) {
    int r;
    if (player_step_registers_any(p)) {
      for (r = 0; r < 14; r++)
        regs[(size_t)r * (size_t)total + (size_t)frame] = chip->reg[r];
    }
    /* p2 is stepped purely to keep it in sync tick-for-tick with p,
     * mirroring MakeBufferTracker's own unconditional All_GetRegisters
     * [1](1) call whenever TSMode - its own register output is never
     * recorded (see this file's header comment on why VTX export has
     * no genuine second-chip concept, matching the real Convs.pas). */
    if (p2) player_step_registers_any(p2);
  }
  *out_regs = regs;
  return true;
}

static bool vtx_export_core(const char* path, player* p, player* p2,
                             int ay_freq, const char* title,
                             const char* author, const char* programm,
                             const char* tracker, const char* comment,
                             int loop_vbl) {
  uint8_t* regs;
  size_t regs_size;
  FILE* f;
  uint8_t header[16];
  double secs_per_tick;
  int inter_frq;
  bool ok;

  if (!build_regs(p, p2, &regs, &regs_size)) return false;

  if (ay_freq <= 0) ay_freq = PLAYER_AY_FREQ_DEF;
  secs_per_tick = player_get_seconds_per_tick(p);
  inter_frq = (secs_per_tick > 0.0)
                  ? (int)(1.0 / secs_per_tick + 0.5)
                  : 50;
  if (inter_frq > 255) inter_frq = 255;

  build_header(header, player_ay_engine(p)->chip_type,
               (loop_vbl < 0 || loop_vbl > 65535) ? 0 : (uint16_t)loop_vbl,
               (uint32_t)ay_freq, (uint8_t)inter_frq, (uint32_t)regs_size);

  f = fopen(path, "wb");
  if (!f) {
    free(regs);
    return false;
  }
  ok = fwrite(header, 1, sizeof(header), f) == sizeof(header) &&
       write_str(f, title) && write_str(f, author) &&
       write_str(f, programm) && write_str(f, tracker) &&
       write_str(f, comment);

  if (ok) {
    size_t comp_cap = regs_size + regs_size / 8 + 4096; /* generous -
        * LhASsA's own encoder declines (LH_ERR_UNSUPPORTED) rather than
        * ever writing past the true worst case, so this is a safety
        * margin, not a tight bound. */
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    if (!comp) {
      ok = false;
    } else {
      size_t comp_len = comp_cap;
      lh_status st = lh_compress(LH_METHOD_LH5, regs, regs_size, comp, &comp_len);
      if (st != LH_OK) {
        /* Real register-write logs are always repetitive enough to
         * compress (confirmed empirically - see migration_debt.yaml's
         * round-trip investigation); LH_ERR_UNSUPPORTED in practice
         * only means "wouldn't shrink", which shouldn't happen for a
         * genuine multi-second recording - treated as a hard failure
         * rather than silently falling back to an uncompressed/wrong-
         * format file. */
        ok = false;
      } else {
        ok = fwrite(comp, 1, comp_len, f) == comp_len;
      }
      free(comp);
    }
  }

  fclose(f);
  free(regs);
  return ok;
}

bool vtx_export_write(const char* path, player* p, int ay_freq,
                       const char* title, const char* author,
                       const char* programm, const char* tracker,
                       const char* comment, int loop_vbl) {
  return vtx_export_core(path, p, NULL, ay_freq, title, author, programm,
                          tracker, comment, loop_vbl);
}

bool vtx_export_write_pair(const char* path, player_pair* pair, int ay_freq,
                            const char* title, const char* author,
                            const char* programm, const char* tracker,
                            const char* comment, int loop_vbl) {
  player* p2 = (pair->active && pair->secondary_loaded) ? &pair->secondary : NULL;
  return vtx_export_core(path, &pair->primary, p2, ay_freq, title, author,
                          programm, tracker, comment, loop_vbl);
}

bool vtx_export_debug_write_raw_regs(const char* path, player* p) {
  uint8_t* regs;
  size_t regs_size;
  FILE* f;
  bool ok;
  if (!build_regs(p, NULL, &regs, &regs_size)) return false;
  f = fopen(path, "wb");
  if (!f) {
    free(regs);
    return false;
  }
  ok = fwrite(regs, 1, regs_size, f) == regs_size;
  fclose(f);
  free(regs);
  return ok;
}
