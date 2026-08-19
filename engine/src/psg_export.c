#include "ay_engine/psg_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Convs.pas:672-895's PSG_Converter. Ported: the VBL2PSG branch (778-
 * 809) - the header write, PSG_Save_Registers (708-723), and Psg_Save_
 * Ostatok (681-706). Not ported: OUT2PSG/ZXAY2PSG/EPSG2PSG (see
 * psg_export.h's own file comment), the GUI-entangled setup around it
 * (FrmPLst.GetVarsForSave, ShowProgress, Application.ProcessMessages,
 * LongProcessPrepare/Done - none of which affect the bytes actually
 * written), and the EPSG-in-place-rename special case (Convs.pas:855-
 * 892 - only relevant when converting a file to itself, not a genuine
 * export scenario). */

typedef struct {
  FILE* f;
  int prev_regs[14];
  int ff_counter;
} psg_stream;

static const uint8_t PSG_MAGIC[16] = {0x50, 0x53, 0x47, 0x1a, 0, 0, 0, 0,
                                       0,    0,    0,    0,    0, 0, 0, 0};

static void psg_stream_init(psg_stream* s, FILE* f) {
  int i;
  s->f = f;
  for (i = 0; i < 13; i++) s->prev_regs[i] = -1;
  s->prev_regs[13] = 255;
  s->ff_counter = 0;
}

/* Psg_Save_Ostatok(n) - flushes the pending "idle frames" run using the
 * PSG format's own $FE(x4 frames)/$FF(x1 frame) run-length encoding. */
static void psg_save_ostatok(psg_stream* s) {
  long j;
  if (s->ff_counter <= 0) return;
  j = s->ff_counter / 4;
  if (j > 0) {
    while (j > 255) {
      j -= 255;
      fputc(0xFE, s->f);
      fputc(0xFF, s->f);
    }
    if (j > 0) {
      fputc(0xFE, s->f);
      fputc((int)j, s->f);
    }
  }
  {
    int rem = s->ff_counter % 4;
    int k;
    for (k = 0; k < rem; k++) fputc(0xFF, s->f);
  }
  s->ff_counter = 0;
}

/* PSG_Save_Registers(n) - called once per output tick with that tick's
 * final register state (14 registers, 0-13; register 8/9/10's envelope
 * bit is part of the raw byte here, matching AY.pas's own RegisterAY.
 * Index[8..10] - no separate masking, this records the SAME raw byte
 * ay_chip_set_ay_register_fast last stored). */
static void psg_save_registers(psg_stream* s, const ay_chip* chip) {
  int i;
  s->ff_counter++;
  for (i = 0; i < 14; i++) {
    int v = chip->reg[i];
    if (s->prev_regs[i] != v) {
      psg_save_ostatok(s);
      s->prev_regs[i] = v;
      fputc(i, s->f);
      fputc(v, s->f);
    }
  }
  s->prev_regs[13] = 255; /* register 13 (envelope shape) always
                            * "looks changed" next frame - writing it
                            * always restarts the envelope on real
                            * hardware, so it can never be silently
                            * skipped as "unchanged". */
}

static bool psg_write_one(FILE* f, player* p, int64_t target_tick) {
  psg_stream s;
  ay_chip* chip = &player_ay_engine(p)->chip;
  int64_t counter, max;
  (void)max;
  psg_stream_init(&s, f);
  if (fwrite(PSG_MAGIC, 1, sizeof(PSG_MAGIC), f) != sizeof(PSG_MAGIC))
    return false;
  for (;;) {
    if (!player_get_tick_position(p, &counter, &max)) break;
    if (counter >= target_tick) break;
    if (player_step_registers_any(p)) psg_save_registers(&s, chip);
  }
  psg_save_ostatok(&s);
  return true;
}

bool psg_export_write(const char* path, player* p) {
  int64_t counter, max;
  FILE* f;
  bool ok;
  if (p->format == PLAYER_FORMAT_UNKNOWN) return false;
  if (!player_get_tick_position(p, &counter, &max)) return false;
  f = fopen(path, "wb");
  if (!f) return false;
  ok = psg_write_one(f, p, max);
  fclose(f);
  return ok;
}

bool psg_export_write_pair(const char* path, const char* path2,
                            player_pair* pair) {
  int64_t counter1, max1, counter2, max2, target;
  FILE* f1;
  FILE* f2 = NULL;
  psg_stream s1, s2;
  bool has_second;

  if (pair->primary.format == PLAYER_FORMAT_UNKNOWN) return false;
  if (!player_get_tick_position(&pair->primary, &counter1, &max1))
    return false;

  /* pair->active can only ever be true for the 14 pairing-eligible
   * tracker formats (player_pair_load_song's own player_supports_
   * pairing checks on both sides before setting it) - AY/YM/VTX/SNDH
   * always fall through to the single-voice path below, exactly as
   * they should (Convs.pas's own PSG_Converter has no TSMode concept
   * for those four either, since real Pascal can't pair them). */
  has_second = pair->active && pair->secondary_loaded;
  if (!has_second) return psg_export_write(path, &pair->primary);

  if (!player_get_tick_position(&pair->secondary, &counter2, &max2))
    return false;
  /* VBL2PSG: nMax picks whichever side has the LARGER Global_Tick_Max -
   * the loop runs to THAT bound regardless of Force_Loop (Force_Loop
   * only affects whether the shorter side keeps being RECORDED during
   * that time, not how long the export itself runs). */
  target = (max2 > max1) ? max2 : max1;

  f1 = fopen(path, "wb");
  if (!f1) return false;
  f2 = fopen(path2, "wb");
  if (!f2) {
    fclose(f1);
    return false;
  }

  psg_stream_init(&s1, f1);
  psg_stream_init(&s2, f2);
  if (fwrite(PSG_MAGIC, 1, sizeof(PSG_MAGIC), f1) != sizeof(PSG_MAGIC) ||
      fwrite(PSG_MAGIC, 1, sizeof(PSG_MAGIC), f2) != sizeof(PSG_MAGIC)) {
    fclose(f1);
    fclose(f2);
    return false;
  }

  {
    bool nmax_is_secondary = (max2 > max1);
    ay_chip* chip1 = &player_ay_engine(&pair->primary)->chip;
    ay_chip* chip2 = &player_ay_engine(&pair->secondary)->chip;
    for (;;) {
      int64_t nmax_counter;
      if (nmax_is_secondary) {
        if (!player_get_tick_position(&pair->secondary, &counter2, &max2))
          break;
        nmax_counter = counter2;
      } else {
        if (!player_get_tick_position(&pair->primary, &counter1, &max1))
          break;
        nmax_counter = counter1;
      }
      if (nmax_counter >= target) break;

      if (player_step_registers_any(&pair->primary))
        psg_save_registers(&s1, chip1);
      if (player_step_registers_any(&pair->secondary))
        psg_save_registers(&s2, chip2);
    }
  }
  psg_save_ostatok(&s1);
  psg_save_ostatok(&s2);
  fclose(f1);
  fclose(f2);
  return true;
}
