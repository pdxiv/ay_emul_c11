/* Companion to ay_emul/OracleHarness.pas (in the submodule). Drives
 * engine/z80_bus.c + engine/ay.c through the SAME synthetic instruction/
 * register sequences the oracle harness runs through the real Pascal
 * Z80.pas/AY.pas, and dumps comparable output for byte-for-byte diffing
 * via run_diff.sh. See migration_debt.yaml MIG-0003/0005/0006b. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/hw/ay.h"
#include "ay_engine/formats/ay_file.h"
#include "ay_engine/formats/ym_file.h"
#include "ay_engine/formats/pt3_file.h"
#include "ay_engine/formats/pt1_file.h"
#include "ay_engine/formats/gtr_file.h"
#include "ay_engine/formats/fls_file.h"
#include "ay_engine/formats/stc_file.h"
#include "ay_engine/formats/stp_file.h"
#include "ay_engine/formats/pt2_file.h"
#include "ay_engine/formats/fxm_file.h"
#include "ay_engine/formats/psm_file.h"
#include "ay_engine/formats/asc_file.h"
#include "ay_engine/formats/ftc_file.h"
#include "ay_engine/formats/psc_file.h"
#include "ay_engine/formats/sqt_file.h"
#include "ay_engine/formats/sndh_file.h"
#include "ay_engine/formats/vtx_file.h"
#include "ay_engine/hw/z80_bus.h"
#include "ay_engine/hw/m68k_bus.h"
#include "ay_engine/hw/mfp.h"
#include "ay_engine/hw/dma_sound.h"
#include "ay_engine/player.h"
#include "ay_engine/psg_export.h"
#include "ay_export/vtx_export.h"
#include "ay_player/wav.h"
#include "m68k.h"

typedef struct {
  uint8_t reg;
  uint8_t data;
  int count;
} recorded_write;

static void on_write(void* ud, uint8_t reg, uint8_t data) {
  recorded_write* r = (recorded_write*)ud;
  r->reg = reg;
  r->data = data;
  r->count++;
}

static int emit_out_c_a(uint8_t* mem, int pc, uint16_t bc, uint8_t a) {
  mem[pc++] = 0x01;
  mem[pc++] = (uint8_t)(bc & 0xFF);
  mem[pc++] = (uint8_t)(bc >> 8);
  mem[pc++] = 0x3E;
  mem[pc++] = a;
  mem[pc++] = 0xED;
  mem[pc++] = 0x79;
  return pc;
}

static void dump_ay_registers(ay_chip* chip, const char* path) {
  FILE* f = fopen(path, "w");
  int i;
  for (i = 0; i < 14; i++) fprintf(f, "%d\n", chip->reg[i]);
  fclose(f);
}

static void run_zx(const char* out_path) {
  z80_bus bus;
  ay_chip chip;
  memset(&chip, 0, sizeof(chip));
  ay_chip_reset(&chip, 1);
  z80_bus_init(&bus, 100000);
  bus.machine = Z80_BUS_MACHINE_ZX;

  int pc = 0;
  pc = emit_out_c_a(bus.ram, pc, 0xFFFD, 8);
  pc = emit_out_c_a(bus.ram, pc, 0xBFFD, 15);
  bus.ram[pc] = 0x76;

  recorded_write rw;
  memset(&rw, 0, sizeof(rw));
  bus.ay_write_userdata = &rw;
  bus.on_ay_write = on_write;

  int i;
  for (i = 0; i < 100 && !bus.cpu.halted; i++) z80_bus_step(&bus);

  if (rw.count > 0) ay_chip_set_ay_register(&chip, rw.reg, rw.data);
  dump_ay_registers(&chip, out_path);
}

static void run_cpc(const char* out_path) {
  z80_bus bus;
  ay_chip chip;
  memset(&chip, 0, sizeof(chip));
  ay_chip_reset(&chip, 1);
  z80_bus_init(&bus, 100000);
  bus.machine = Z80_BUS_MACHINE_CPC;

  recorded_write rw;
  memset(&rw, 0, sizeof(rw));
  bus.ay_write_userdata = &rw;
  bus.on_ay_write = on_write;

  int pc = 0;
  pc = emit_out_c_a(bus.ram, pc, 0xF400, 7);
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0xC0);
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0x00);
  pc = emit_out_c_a(bus.ram, pc, 0xF400, 0x38);
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0x80);
  bus.ram[pc] = 0x76;

  int i;
  for (i = 0; i < 100 && !bus.cpu.halted; i++) z80_bus_step(&bus);

  if (rw.count > 0) ay_chip_set_ay_register(&chip, rw.reg, rw.data);
  dump_ay_registers(&chip, out_path);
}

static void run_immediate(const char* out_path) {
  z80_bus bus;
  ay_chip chip;
  memset(&chip, 0, sizeof(chip));
  ay_chip_reset(&chip, 1);
  z80_bus_init(&bus, 100000);
  bus.machine = Z80_BUS_MACHINE_ZX;

  recorded_write rw;
  memset(&rw, 0, sizeof(rw));
  bus.ay_write_userdata = &rw;
  bus.on_ay_write = on_write;

  int pc = 0;
  pc = emit_out_c_a(bus.ram, pc, 0xFFFD, 9); /* BC-indirect: select reg 9 */
  bus.ram[pc++] = 0x3E; bus.ram[pc++] = 0xBF; /* LD A,$BF */
  bus.ram[pc++] = 0xD3; bus.ram[pc++] = 0xFD; /* OUT ($FD),A -> port $BFFD */
  bus.ram[pc] = 0x76;

  int i;
  for (i = 0; i < 100 && !bus.cpu.halted; i++) {
    /* apply any pending write from the previous instruction before the next
     * one can overwrite `rw` - mirrors how a real caller would flush
     * synchronously inside the on_ay_write callback itself; done here
     * between steps since this harness applies writes after the loop. */
    z80_bus_step(&bus);
    if (rw.count == 1) {
      ay_chip_set_ay_register(&chip, rw.reg, rw.data);
      rw.count = 2; /* mark applied, avoid double-apply */
    }
  }

  dump_ay_registers(&chip, out_path);
}

static uint8_t on_read(void* ud, uint8_t reg) {
  return ((ay_chip*)ud)->reg[reg];
}

/* Closes MIG-0006b's remaining "IN A,(n) has no test" gap - mirrors
 * ay_emul/OracleHarness.pas's RunImmediateInTest exactly: select register
 * 9 and write $2A to it via the BC-indirect form, then read it back via
 * `LD A,$FF; IN A,($FD)` (immediate form, port = (A<<8)|n = $FFFD, the
 * real AY read/select port), dumping the 14 AY registers plus the
 * resulting A value. */
static void run_immediate_in(const char* out_path) {
  z80_bus bus;
  ay_chip chip;
  memset(&chip, 0, sizeof(chip));
  ay_chip_reset(&chip, 1);
  z80_bus_init(&bus, 100000);
  bus.machine = Z80_BUS_MACHINE_ZX;

  recorded_write rw;
  memset(&rw, 0, sizeof(rw));
  bus.ay_write_userdata = &rw;
  bus.on_ay_write = on_write;
  bus.ay_read_userdata = &chip;
  bus.on_ay_read = on_read;

  int pc = 0;
  pc = emit_out_c_a(bus.ram, pc, 0xFFFD, 9);   /* BC-indirect: select reg 9 */
  pc = emit_out_c_a(bus.ram, pc, 0xBFFD, 0x2A); /* BC-indirect: write $2A */
  bus.ram[pc++] = 0x3E; bus.ram[pc++] = 0xFF; /* LD A,$FF */
  bus.ram[pc++] = 0xDB; bus.ram[pc++] = 0xFD; /* IN A,($FD) -> port $FFFD */
  bus.ram[pc] = 0x76;

  int i;
  for (i = 0; i < 100 && !bus.cpu.halted; i++) {
    z80_bus_step(&bus);
    if (rw.count == 1) {
      ay_chip_set_ay_register(&chip, rw.reg, rw.data);
      rw.count = 2;
    }
  }

  FILE* f = fopen(out_path, "w");
  for (i = 0; i < 14; i++) fprintf(f, "%d\n", chip.reg[i]);
  fprintf(f, "%d\n", bus.cpu.a);
  fclose(f);
}

static void run_pcm(const char* out_path) {
  ay_engine e;
  int16_t buf[512 * 2];
  ay_engine_init(&e);

  ay_chip_set_ay_register(&e.chip, 0, 64);
  ay_chip_set_ay_register(&e.chip, 1, 0);
  ay_chip_set_ay_register(&e.chip, 7, 0x3E);
  ay_chip_set_ay_register(&e.chip, 8, 15);

  e.chip_type = AY_CHIP_TYPE_AY;
  ay_engine_calculate_level_tables(&e);

  e.delay_in_tiks = 0x10000;
  e.tik_re = e.delay_in_tiks;
  e.number_of_tiks = ((int64_t)5000) << 32;
  e.buf = buf;
  e.buf_len = 0;
  e.buffer_length = 512;

  ay_synthesizer_stereo16(&e);

  FILE* f = fopen(out_path, "wb");
  fwrite(buf, sizeof(buf), 1, f);
  fclose(f);
}

/* AY_FreqDef/SampleRateDef from ay_emul/settings.pas, matching
 * OracleHarness.pas's RunPCMFilteredTest. */
#define AY_FREQ_DEF 1773400
#define SAMPLE_RATE_DEF 48000

static void run_pcm_filtered(const char* out_path) {
  ay_engine e;
  int16_t buf[512 * 2];
  ay_engine_init(&e);

  ay_engine_set_filter(&e, 1, AY_FREQ_DEF, SAMPLE_RATE_DEF);

  ay_chip_set_ay_register(&e.chip, 0, 64);
  ay_chip_set_ay_register(&e.chip, 1, 0);
  ay_chip_set_ay_register(&e.chip, 7, 0x3E);
  ay_chip_set_ay_register(&e.chip, 8, 15);

  e.chip_type = AY_CHIP_TYPE_AY;
  ay_engine_calculate_level_tables(&e);

  e.delay_in_tiks = 0x10000;
  e.tik_re = e.delay_in_tiks;
  e.number_of_tiks = ((int64_t)5000) << 32;
  e.buf = buf;
  e.buf_len = 0;
  e.buffer_length = 512;

  ay_synthesizer_stereo16(&e);

  FILE* f = fopen(out_path, "wb");
  fwrite(buf, sizeof(buf), 1, f);
  fclose(f);

  ay_engine_free_filter(&e);
}

static void run_pcm8(const char* out_path) {
  ay_engine e;
  uint8_t buf[512 * 2];
  ay_engine_init(&e);

  ay_chip_set_ay_register(&e.chip, 0, 64);
  ay_chip_set_ay_register(&e.chip, 1, 0);
  ay_chip_set_ay_register(&e.chip, 7, 0x3E);
  ay_chip_set_ay_register(&e.chip, 8, 15);

  e.chip_type = AY_CHIP_TYPE_AY;
  e.sample_bits = 8;
  ay_engine_calculate_level_tables(&e);

  e.delay_in_tiks = 0x10000;
  e.tik_re = e.delay_in_tiks;
  e.number_of_tiks = ((int64_t)5000) << 32;
  e.buf = buf;
  e.buf_len = 0;
  e.buffer_length = 512;

  ay_synthesizer_stereo8(&e);

  FILE* f = fopen(out_path, "wb");
  fwrite(buf, sizeof(buf), 1, f);
  fclose(f);
}

/* Matches OracleHarness.pas's RunM68kTest exactly: same reset vectors,
 * same MOVEQ/MOVEQ/ADD.L/MOVE.L/STOP program, same flat memory region -
 * no byte-swap transform needed on this side (see m68k_bus.c's file
 * comment: words are always composed from big-endian byte reads). */
static void run_m68k(const char* out_path) {
  static uint8_t mem[0x10000];
  memset(mem, 0, sizeof(mem));

  mem[0] = 0x00; mem[1] = 0x00; mem[2] = 0x10; mem[3] = 0x00;
  mem[4] = 0x00; mem[5] = 0x00; mem[6] = 0x04; mem[7] = 0x00;

  mem[0x400] = 0x70; mem[0x401] = 0x05;
  mem[0x402] = 0x72; mem[0x403] = 0x07;
  mem[0x404] = 0xD0; mem[0x405] = 0x81;
  mem[0x406] = 0x23; mem[0x407] = 0xC0; mem[0x408] = 0x00; mem[0x409] = 0x00;
  mem[0x40A] = 0x20; mem[0x40B] = 0x00;
  mem[0x40C] = 0x4E; mem[0x40D] = 0x72; mem[0x40E] = 0x27; mem[0x40F] = 0x00;

  m68k_bus bus;
  m68k_bus_init(&bus);
  m68k_bus_add_flat_region(&bus, 0, sizeof(mem) - 1, mem);
  m68k_bus_activate(&bus);
  m68k_bus_reset(&bus);
  m68k_bus_exec(&bus, 200);

  uint32_t written = ((uint32_t)mem[0x2000] << 24) | ((uint32_t)mem[0x2001] << 16) |
                      ((uint32_t)mem[0x2002] << 8) | mem[0x2003];

  FILE* f = fopen(out_path, "w");
  fprintf(f, "%u\n", m68k_bus_get_reg(M68K_REG_D0));
  fprintf(f, "%u\n", m68k_bus_get_reg(M68K_REG_PC));
  fprintf(f, "%u\n", written);
  fclose(f);
}

/* Matches OracleHarness.pas's RunMFPTest exactly - see that procedure's
 * comment for why IMRA is deliberately left at 0 (isolates the countdown/
 * reload timing and the IPR-bit-set side effect from Starscream's
 * immediate-delivery-and-auto-clear behavior, which engine/mfp.c's
 * level-triggered/Musashi-driven design doesn't replicate - see
 * engine/include/ay_engine/mfp.h's file comment and MIG-0013). */
static void run_mfp(const char* out_path) {
  mfp m;
  mfp_init(&m);

  mfp_write_byte_at(&m, 0xFFFA1F, 10, 0); /* TAD = 10 */
  mfp_write_byte_at(&m, 0xFFFA19, 1, 0);  /* TACR mode 1 */
  mfp_write_byte_at(&m, 0xFFFA07, 32, 0); /* IERA enable A */

  int64_t next1 = mfp_emulate_timer(&m, 0, 1);
  uint8_t ipa_before = mfp_read_byte_at(&m, 0xFFFA0B, 1);

  int64_t next2 = mfp_emulate_timer(&m, 0, 130);
  uint8_t ipa_after = mfp_read_byte_at(&m, 0xFFFA0B, 130);

  FILE* f = fopen(out_path, "w");
  fprintf(f, "%lld\n", (long long)next1);
  fprintf(f, "%u\n", ipa_before);
  fprintf(f, "%lld\n", (long long)next2);
  fprintf(f, "%u\n", ipa_after);
  fclose(f);
}

/* Matches OracleHarness.pas's RunDMATest exactly. */
static void run_dma(const char* out_path) {
  dma_sound d;
  dma_sound_init(&d);

  uint8_t mem[0x10000];
  memset(mem, 0, sizeof(mem));
  mem[0x1000] = (uint8_t)(int8_t)100;
  mem[0x1001] = (uint8_t)(int8_t)-50;

  dma_sound_write_byte_at(&d, 0xFF8903, 0x00, 0);
  dma_sound_write_byte_at(&d, 0xFF8905, 0x10, 0);
  dma_sound_write_byte_at(&d, 0xFF8907, 0x00, 0);
  dma_sound_write_byte_at(&d, 0xFF890F, 0x00, 0);
  dma_sound_write_byte_at(&d, 0xFF8911, 0x10, 0);
  dma_sound_write_byte_at(&d, 0xFF8913, 0x10, 0);
  dma_sound_write_byte_at(&d, 0xFF8921, 0x80, 0);
  dma_sound_write_byte_at(&d, 0xFF8901, 0x01, 0);

  int lev_l = 0, lev_r = 0;
  int atari_dma_level = 128;
  int i;
  bool saw_nonzero = false;
  for (i = 0; i < 2000; i++) {
    /* Matches RunDMATest's loop: check play state (via the play field
     * directly, mirroring Oracle_DMA_IsPlaying) before each mix call. */
    if (!d.play) break;
    /* AyFreq here is atari.pas's OWN global (Atari_MainClockFreqDef/16 =
     * 2000000.0, set by Atari_SetDefault) - the Atari ST's AY/YM clock,
     * NOT settings.pas's AY_Freq (ZX Spectrum's 1773400) which is a
     * different unit-level global entirely despite the near-identical
     * name. Mixing them up here is exactly the kind of bug this oracle
     * comparison exists to catch. */
    dma_sound_mix(&d, mem, sizeof(mem), atari_dma_level, 8000000.0,
                  2000000.0, &lev_l, &lev_r);
    if (lev_l != 0) saw_nonzero = true;
  }

  FILE* f = fopen(out_path, "w");
  fprintf(f, "%d\n", lev_l);
  fprintf(f, "%d\n", lev_r);
  fprintf(f, "%d\n", saw_nonzero ? 1 : 0);
  fclose(f);
}

/* Matches OracleHarness.pas's RunAYFileTest exactly: same defaults, same
 * 4x512-stereo16-frame buffer sequence, driving engine/src/ay_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_ay_file(const char* out_path, const char* ay_path) {
  FILE* in = fopen(ay_path, "rb");
  if (!in) {
    fprintf(stderr, "run_ay_file: cannot open %s\n", ay_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_ay_file: short read on %s\n", ay_path);
    exit(1);
  }
  fclose(in);

  ay_file f;
  ay_file_status st = ay_file_load(&f, data, (size_t)sz, 0,
                                    AY_FILE_AY_FREQ_DEF, AY_FILE_FRQ_Z80_DEF,
                                    AY_FILE_SAMPLE_RATE_DEF);
  if (st != AY_FILE_OK) {
    fprintf(stderr, "run_ay_file: ay_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* OracleHarness.pas's RunAYFileTest can't safely replicate Z80.pas:11112's
   * `if AYFileEnableAutoSwitch then FrmMain.Set_Chip_Frq(1000000)` call -
   * FrmMain is never instantiated in oracle mode (Application.CreateForm
   * doesn't run), so calling it would crash. The harness therefore leaves
   * AYFileEnableAutoSwitch at its default False, meaning the CPC-protocol
   * chip-frequency auto-switch path (real, and smoke-tested via
   * test_ay_file.c against this exact file, which does trigger it) isn't
   * exercised by this particular comparison - disable it here too so both
   * sides run the identical code path. See migration_debt.yaml. */
  f.bus.ay_file_enable_auto_switch = false;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 4 && !f.real_end_all; n++) {
    ay_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
  free(data);
}

/* Matches OracleHarness.pas's RunYMFileTest exactly: same defaults, same
 * 4x512-stereo16-frame buffer sequence, driving engine/src/lh5.c +
 * ym_file.c against the real test file (path supplied as argv[3]). */
static void run_ym_file(const char* out_path, const char* ym_path) {
  FILE* in = fopen(ym_path, "rb");
  if (!in) {
    fprintf(stderr, "run_ym_file: cannot open %s\n", ym_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_ym_file: short read on %s\n", ym_path);
    exit(1);
  }
  fclose(in);

  ym_file f;
  ym_file_status st = ym_file_load(&f, data, (size_t)sz, YM_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != YM_FILE_OK) {
    fprintf(stderr, "run_ym_file: ym_file_load failed (%d)\n", (int)st);
    exit(1);
  }

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 4 && !f.real_end_all; n++) {
    ym_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
  ym_file_free(&f);
}

/* Matches OracleHarness.pas's RunPT3FileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/pt3_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_pt3_file(const char* out_path, const char* pt3_path) {
  FILE* in = fopen(pt3_path, "rb");
  if (!in) {
    fprintf(stderr, "run_pt3_file: cannot open %s\n", pt3_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_pt3_file: short read on %s\n", pt3_path);
    exit(1);
  }
  fclose(in);

  pt3_file f;
  pt3_file_status st = pt3_file_load(&f, data, (size_t)sz, PT3_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != PT3_FILE_OK) {
    fprintf(stderr, "run_pt3_file: pt3_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0101: pt3_file_load now computes a real global_tick_max (see
   * pt3_file.h) and pt3_file_make_buffer can return short of the
   * requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunPT3FileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    pt3_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunPT1FileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/pt1_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_pt1_file(const char* out_path, const char* pt1_path) {
  FILE* in = fopen(pt1_path, "rb");
  if (!in) {
    fprintf(stderr, "run_pt1_file: cannot open %s\n", pt1_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_pt1_file: short read on %s\n", pt1_path);
    exit(1);
  }
  fclose(in);

  pt1_file f;
  pt1_file_status st = pt1_file_load(&f, data, (size_t)sz, PT1_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != PT1_FILE_OK) {
    fprintf(stderr, "run_pt1_file: pt1_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: pt1_file_load now computes a real global_tick_max
   * (see pt1_file.h) and pt1_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunPT1FileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    pt1_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunGTRFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/gtr_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_gtr_file(const char* out_path, const char* gtr_path) {
  FILE* in = fopen(gtr_path, "rb");
  if (!in) {
    fprintf(stderr, "run_gtr_file: cannot open %s\n", gtr_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_gtr_file: short read on %s\n", gtr_path);
    exit(1);
  }
  fclose(in);

  gtr_file f;
  gtr_file_status st = gtr_file_load(&f, data, (size_t)sz, GTR_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != GTR_FILE_OK) {
    fprintf(stderr, "run_gtr_file: gtr_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: gtr_file_load now computes a real global_tick_max
   * (see gtr_file.h) and gtr_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunGTRFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    gtr_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunFLSFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/fls_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_fls_file(const char* out_path, const char* fls_path) {
  FILE* in = fopen(fls_path, "rb");
  if (!in) {
    fprintf(stderr, "run_fls_file: cannot open %s\n", fls_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_fls_file: short read on %s\n", fls_path);
    exit(1);
  }
  fclose(in);

  fls_file f;
  fls_file_status st = fls_file_load(&f, data, (size_t)sz, FLS_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != FLS_FILE_OK) {
    fprintf(stderr, "run_fls_file: fls_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: fls_file_load now computes a real global_tick_max
   * (see fls_file.h) and fls_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunFLSFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    fls_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunSTCFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/stc_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_stc_file(const char* out_path, const char* stc_path) {
  FILE* in = fopen(stc_path, "rb");
  if (!in) {
    fprintf(stderr, "run_stc_file: cannot open %s\n", stc_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_stc_file: short read on %s\n", stc_path);
    exit(1);
  }
  fclose(in);

  stc_file f;
  stc_file_status st = stc_file_load(&f, data, (size_t)sz, STC_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != STC_FILE_OK) {
    fprintf(stderr, "run_stc_file: stc_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: stc_file_load now computes a real global_tick_max
   * (see stc_file.h) and stc_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunSTCFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    stc_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunSTPFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/stp_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_stp_file(const char* out_path, const char* stp_path) {
  FILE* in = fopen(stp_path, "rb");
  if (!in) {
    fprintf(stderr, "run_stp_file: cannot open %s\n", stp_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_stp_file: short read on %s\n", stp_path);
    exit(1);
  }
  fclose(in);

  stp_file f;
  stp_file_status st = stp_file_load(&f, data, (size_t)sz, STP_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != STP_FILE_OK) {
    fprintf(stderr, "run_stp_file: stp_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: stp_file_load now computes a real global_tick_max
   * (see stp_file.h) and stp_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunSTPFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    stp_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunPT2FileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/pt2_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_pt2_file(const char* out_path, const char* pt2_path) {
  FILE* in = fopen(pt2_path, "rb");
  if (!in) {
    fprintf(stderr, "run_pt2_file: cannot open %s\n", pt2_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_pt2_file: short read on %s\n", pt2_path);
    exit(1);
  }
  fclose(in);

  pt2_file f;
  pt2_file_status st = pt2_file_load(&f, data, (size_t)sz, PT2_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != PT2_FILE_OK) {
    fprintf(stderr, "run_pt2_file: pt2_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: pt2_file_load now computes a real global_tick_max
   * (see pt2_file.h) and pt2_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunPT2FileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    pt2_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunFXMFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/fxm_file.c
 * against the real test file (path supplied as argv[3]). Note fxm_file_
 * load takes the RAW file bytes (it does its own 6-byte-skip/address
 * handling internally), unlike every other X_file_load in this project. */
static void run_fxm_file(const char* out_path, const char* fxm_path) {
  FILE* in = fopen(fxm_path, "rb");
  if (!in) {
    fprintf(stderr, "run_fxm_file: cannot open %s\n", fxm_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_fxm_file: short read on %s\n", fxm_path);
    exit(1);
  }
  fclose(in);

  fxm_file f;
  fxm_file_status st = fxm_file_load(&f, data, (size_t)sz, FXM_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != FXM_FILE_OK) {
    fprintf(stderr, "run_fxm_file: fxm_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: fxm_file_load now computes a real global_tick_max
   * (see fxm_file.h) and fxm_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunFXMFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    fxm_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunPSMFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/psm_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_psm_file(const char* out_path, const char* psm_path) {
  FILE* in = fopen(psm_path, "rb");
  if (!in) {
    fprintf(stderr, "run_psm_file: cannot open %s\n", psm_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_psm_file: short read on %s\n", psm_path);
    exit(1);
  }
  fclose(in);

  psm_file f;
  psm_file_status st = psm_file_load(&f, data, (size_t)sz, PSM_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != PSM_FILE_OK) {
    fprintf(stderr, "run_psm_file: psm_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: psm_file_load now computes a real global_tick_max
   * (see psm_file.h) and psm_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunPSMFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    psm_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunASCFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/asc_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_asc_file(const char* out_path, const char* asc_path, bool is_asc0) {
  FILE* in = fopen(asc_path, "rb");
  if (!in) {
    fprintf(stderr, "run_asc_file: cannot open %s\n", asc_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_asc_file: short read on %s\n", asc_path);
    exit(1);
  }
  fclose(in);

  asc_file f;
  asc_file_status st = asc_file_load(&f, data, (size_t)sz, is_asc0, ASC_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != ASC_FILE_OK) {
    fprintf(stderr, "run_asc_file: asc_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: asc_file_load now computes a real global_tick_max
   * (see asc_file.h) and asc_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunASCFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    asc_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunFTCFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/ftc_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_ftc_file(const char* out_path, const char* ftc_path) {
  FILE* in = fopen(ftc_path, "rb");
  if (!in) {
    fprintf(stderr, "run_ftc_file: cannot open %s\n", ftc_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_ftc_file: short read on %s\n", ftc_path);
    exit(1);
  }
  fclose(in);

  ftc_file f;
  ftc_file_status st = ftc_file_load(&f, data, (size_t)sz, FTC_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != FTC_FILE_OK) {
    fprintf(stderr, "run_ftc_file: ftc_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: ftc_file_load now computes a real global_tick_max
   * (see ftc_file.h) and ftc_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunFTCFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    ftc_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunPSCFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/psc_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_psc_file(const char* out_path, const char* psc_path) {
  FILE* in = fopen(psc_path, "rb");
  if (!in) {
    fprintf(stderr, "run_psc_file: cannot open %s\n", psc_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_psc_file: short read on %s\n", psc_path);
    exit(1);
  }
  fclose(in);

  psc_file f;
  psc_file_status st = psc_file_load(&f, data, (size_t)sz, PSC_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != PSC_FILE_OK) {
    fprintf(stderr, "run_psc_file: psc_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: psc_file_load now computes a real global_tick_max
   * (see psc_file.h) and psc_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunPSCFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    psc_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunSQTFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/sqt_file.c
 * against the real test file (path supplied as argv[3]). */
static void run_sqt_file(const char* out_path, const char* sqt_path) {
  FILE* in = fopen(sqt_path, "rb");
  if (!in) {
    fprintf(stderr, "run_sqt_file: cannot open %s\n", sqt_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_sqt_file: short read on %s\n", sqt_path);
    exit(1);
  }
  fclose(in);

  sqt_file f;
  sqt_file_status st = sqt_file_load(&f, data, (size_t)sz, SQT_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != SQT_FILE_OK) {
    fprintf(stderr, "run_sqt_file: sqt_file_load failed (%d)\n", (int)st);
    exit(1);
  }
  /* MIG-0108: sqt_file_load now computes a real global_tick_max
   * (see sqt_file.h) and sqt_file_make_buffer can return short of
   * the requested 512 frames once it's reached - the oracle side
   * (OracleHarness.pas's RunSQTFileTest) deliberately runs with a
   * sentinel Global_Tick_Max instead (never edited to keep pace, see
   * this repo's own standing rule on the Pascal oracle), always
   * producing exactly 400*512 frames; do_loop=true matches that same
   * "never stop early" behavior here (see player_set_do_loop's own
   * comment for the equivalent ay_player --ignore-end fix). */
  f.do_loop = true;

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  for (n = 0; n < 400; n++) {
    sqt_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
}

/* Matches OracleHarness.pas's RunVTXFileTest exactly: same defaults, same
 * 400x512-stereo16-frame buffer sequence, driving engine/src/lh5.c +
 * vtx_file.c against the real test file (path supplied as argv[3]). */
static void run_vtx_file(const char* out_path, const char* vtx_path) {
  FILE* in = fopen(vtx_path, "rb");
  if (!in) {
    fprintf(stderr, "run_vtx_file: cannot open %s\n", vtx_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_vtx_file: short read on %s\n", vtx_path);
    exit(1);
  }
  fclose(in);

  vtx_file f;
  vtx_file_status st = vtx_file_load(&f, data, (size_t)sz, VTX_FILE_SAMPLE_RATE_DEF);
  free(data);
  if (st != VTX_FILE_OK) {
    fprintf(stderr, "run_vtx_file: vtx_file_load failed (%d)\n", (int)st);
    exit(1);
  }

  FILE* out = fopen(out_path, "wb");
  int16_t buf[512 * 2];
  int n;
  /* Mirrors OracleHarness.pas's RunVTXFileTest: `if Real_End_All then
   * break;` before each MakeBufferVTX call - a fixed 400-buffer loop
   * with no such check silently over-runs past the real song's end for
   * any file whose NumberOfVBLs/LoopVBL causes real_end_all to go true
   * within 400 buffers (unlike songs/vtx/Intro.vtx, which never did). */
  for (n = 0; n < 400 && !f.real_end_all; n++) {
    vtx_file_make_buffer(&f, buf, 512);
    fwrite(buf, sizeof(buf), 1, out);
  }
  fclose(out);
  vtx_file_free(&f);
}

/* Companion to ay_emul/OracleHarness.pas's RunGetTimeTest (MIG-0103's own
 * IntegrityCheck oracle, not previously reused for THIS purpose): loads a
 * tracker-format file through the same format-specific loader used by
 * this file's other run_*_file scenarios above, then prints its
 * precomputed global_tick_max/loop_tick in the exact "time=N\nloop=M\n"
 * text format RunGetTimeTest itself writes, so run_diff.sh can text-diff
 * them directly. This closes MIG-0103/MIG-0104's own remaining oracle-
 * validation gap: those entries ported each format's GetTimeXXX
 * duration-precompute math into engine/, and it clearly RUNS and returns
 * plausible-looking non-zero values (confirmed by identify_ay_file's
 * IntegrityCheck, which already reused these same fields) - but, unlike
 * PT3's own GetTimePT3 port (MIG-0101, cross-checked against the real
 * oracle's exact tick count), none of the other 12 formats' computed
 * durations had ever been checked against the real Pascal GetTimeXXX
 * output for an exact tick-count match. STC and FLS's own GetTimeSTC/
 * GetTimeFLS have no `Lp` (loop) output parameter at all (see their
 * Players.pas signatures - only `Tm`), matching this port's own structs,
 * which correspondingly have no loop_tick field for these two formats
 * either - lp is left at 0 for them here, exactly matching
 * RunGetTimeTest's own Lp-stays-0 behavior for the same two formats. */
static void run_get_time(const char* out_path, const char* format,
                          const char* file_path) {
  FILE* in = fopen(file_path, "rb");
  if (!in) {
    fprintf(stderr, "run_get_time: cannot open %s\n", file_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_get_time: short read on %s\n", file_path);
    exit(1);
  }
  fclose(in);

  int64_t tm = 0, lp = 0;
  bool ok = false;

  if (strcmp(format, "pt1") == 0) {
    pt1_file f;
    ok = pt1_file_load(&f, data, (size_t)sz, PT1_FILE_SAMPLE_RATE_DEF) == PT1_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "gtr") == 0) {
    gtr_file f;
    ok = gtr_file_load(&f, data, (size_t)sz, GTR_FILE_SAMPLE_RATE_DEF) == GTR_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "fls") == 0) {
    fls_file f;
    ok = fls_file_load(&f, data, (size_t)sz, FLS_FILE_SAMPLE_RATE_DEF) == FLS_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = 0; /* GetTimeFLS has no Lp */ }
  } else if (strcmp(format, "stc") == 0) {
    stc_file f;
    ok = stc_file_load(&f, data, (size_t)sz, STC_FILE_SAMPLE_RATE_DEF) == STC_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = 0; /* GetTimeSTC has no Lp */ }
  } else if (strcmp(format, "stp") == 0) {
    stp_file f;
    ok = stp_file_load(&f, data, (size_t)sz, STP_FILE_SAMPLE_RATE_DEF) == STP_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "pt2") == 0) {
    pt2_file f;
    ok = pt2_file_load(&f, data, (size_t)sz, PT2_FILE_SAMPLE_RATE_DEF) == PT2_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "pt3") == 0) {
    pt3_file f;
    ok = pt3_file_load(&f, data, (size_t)sz, PT3_FILE_SAMPLE_RATE_DEF) == PT3_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "fxm") == 0) {
    fxm_file f;
    ok = fxm_file_load(&f, data, (size_t)sz, FXM_FILE_SAMPLE_RATE_DEF) == FXM_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "psm") == 0) {
    psm_file f;
    ok = psm_file_load(&f, data, (size_t)sz, PSM_FILE_SAMPLE_RATE_DEF) == PSM_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "asc") == 0) {
    asc_file f;
    ok = asc_file_load(&f, data, (size_t)sz, false, ASC_FILE_SAMPLE_RATE_DEF) == ASC_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "asc0") == 0) {
    asc_file f;
    ok = asc_file_load(&f, data, (size_t)sz, true, ASC_FILE_SAMPLE_RATE_DEF) == ASC_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "ftc") == 0) {
    ftc_file f;
    ok = ftc_file_load(&f, data, (size_t)sz, FTC_FILE_SAMPLE_RATE_DEF) == FTC_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "psc") == 0) {
    psc_file f;
    ok = psc_file_load(&f, data, (size_t)sz, PSC_FILE_SAMPLE_RATE_DEF) == PSC_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else if (strcmp(format, "sqt") == 0) {
    sqt_file f;
    ok = sqt_file_load(&f, data, (size_t)sz, SQT_FILE_SAMPLE_RATE_DEF) == SQT_FILE_OK;
    if (ok) { tm = f.global_tick_max; lp = f.loop_tick; }
  } else {
    fprintf(stderr, "run_get_time: unknown format '%s'\n", format);
    free(data);
    exit(1);
  }
  free(data);

  if (!ok) {
    fprintf(stderr, "run_get_time: %s_file_load failed for %s\n", format, file_path);
    exit(1);
  }

  FILE* out = fopen(out_path, "w");
  fprintf(out, "time=%lld\n", (long long)tm);
  fprintf(out, "loop=%lld\n", (long long)lp);
  fclose(out);
}

/* MIG-0016: real oracle coverage companion to OracleHarness.pas's
 * RunSNDHUnpackTest - calls sndh_ice_unpack directly (not through a full
 * playback pass, which is impractically slow for SNDH - see
 * migration_debt.yaml MIG-0021) and dumps the raw depacked buffer for a
 * direct byte-for-byte comparison. */
static void run_sndh_unpack(const char* out_path, const char* sndh_path) {
  FILE* in = fopen(sndh_path, "rb");
  if (!in) {
    fprintf(stderr, "run_sndh_unpack: cannot open %s\n", sndh_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_sndh_unpack: short read on %s\n", sndh_path);
    exit(1);
  }
  fclose(in);

  uint8_t* unpacked;
  size_t unpacked_size;
  sndh_file_status st = sndh_ice_unpack(data, (size_t)sz, &unpacked, &unpacked_size);
  free(data);
  if (st != SNDH_FILE_OK) {
    fprintf(stderr, "run_sndh_unpack: sndh_ice_unpack failed (%d) for %s\n",
            (int)st, sndh_path);
    exit(1);
  }

  FILE* out = fopen(out_path, "wb");
  if (!out) {
    fprintf(stderr, "run_sndh_unpack: cannot open %s for writing\n", out_path);
    exit(1);
  }
  fwrite(unpacked, 1, unpacked_size, out);
  fclose(out);
  free(unpacked);
}

/* MIG-0010: real oracle coverage companion to OracleHarness.pas's
 * RunSTCPSGExportTest - loads the file via the real production entry
 * point (player_load) and calls psg_export_write directly, matching
 * how that Pascal scenario replicates Convs.pas's VBL2PSG loop and
 * writes the same "PSG\x1a" + register-diff-log format. */
static void run_psg_export(const char* out_path, const char* in_path) {
  FILE* in = fopen(in_path, "rb");
  if (!in) {
    fprintf(stderr, "run_psg_export: cannot open %s\n", in_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_psg_export: short read on %s\n", in_path);
    exit(1);
  }
  fclose(in);

  player p;
  player_status st = player_load(&p, in_path, data, (size_t)sz, 44100);
  free(data);
  if (st != PLAYER_OK) {
    fprintf(stderr, "run_psg_export: player_load failed (%d)\n", (int)st);
    exit(1);
  }
  if (!psg_export_write(out_path, &p)) {
    fprintf(stderr, "run_psg_export: psg_export_write failed\n");
    exit(1);
  }
  player_free(&p);
}

/* MIG-0010: real oracle coverage companion to OracleHarness.pas's
 * RunSTCVTXRawTest/RunAYVTXRawTest/RunYMVTXRawTest/RunVTXVTXRawTest -
 * see vtx_export.h's own vtx_export_debug_write_raw_regs comment for
 * why this compares the PRE-COMPRESSION register buffer rather than a
 * real, LZH-compressed .vtx file (compression has no single byte-exact
 * target, unlike the register data feeding it). Generic over any
 * player_load-able format (works for AY/YM/VTX/STC alike via
 * player_step_registers_any). */
static void run_vtx_raw(const char* out_path, const char* in_path) {
  FILE* in = fopen(in_path, "rb");
  if (!in) {
    fprintf(stderr, "run_vtx_raw: cannot open %s\n", in_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_vtx_raw: short read on %s\n", in_path);
    exit(1);
  }
  fclose(in);

  player p;
  player_status st = player_load(&p, in_path, data, (size_t)sz, 44100);
  free(data);
  if (st != PLAYER_OK) {
    fprintf(stderr, "run_vtx_raw: player_load failed (%d)\n", (int)st);
    exit(1);
  }
  if (!vtx_export_debug_write_raw_regs(out_path, &p)) {
    fprintf(stderr, "run_vtx_raw: vtx_export_debug_write_raw_regs failed\n");
    exit(1);
  }
  player_free(&p);
}

/* MIG-0010 update: real oracle coverage companion to OracleHarness.pas's
 * RunSTCPairPSGExportTest - loads TWO GENUINELY DIFFERENT, DIFFERENT-
 * LENGTH files as the two player_pair voices (unlike run_stc_pair below,
 * which - matching this port's real .ayl "ts" semantics - always loads
 * the same file twice) specifically to exercise VBL2PSG's own nMax-
 * selection and Force_Loop gating, neither of which is reachable when
 * both voices share one length. Sets force_loop=true, matching
 * settings.pas's own Force_Loop default (and ay_export's own default). */
static void run_stc_pair_psg_export(const char* out_path1,
                                     const char* out_path2,
                                     const char* stc_path1,
                                     const char* stc_path2) {
  size_t sz1, sz2;
  FILE* in1 = fopen(stc_path1, "rb");
  if (!in1) {
    fprintf(stderr, "run_stc_pair_psg_export: cannot open %s\n", stc_path1);
    exit(1);
  }
  fseek(in1, 0, SEEK_END);
  sz1 = (size_t)ftell(in1);
  fseek(in1, 0, SEEK_SET);
  uint8_t* data1 = (uint8_t*)malloc(sz1);
  if (fread(data1, 1, sz1, in1) != sz1) {
    fprintf(stderr, "run_stc_pair_psg_export: short read on %s\n", stc_path1);
    exit(1);
  }
  fclose(in1);

  FILE* in2 = fopen(stc_path2, "rb");
  if (!in2) {
    fprintf(stderr, "run_stc_pair_psg_export: cannot open %s\n", stc_path2);
    exit(1);
  }
  fseek(in2, 0, SEEK_END);
  sz2 = (size_t)ftell(in2);
  fseek(in2, 0, SEEK_SET);
  uint8_t* data2 = (uint8_t*)malloc(sz2);
  if (fread(data2, 1, sz2, in2) != sz2) {
    fprintf(stderr, "run_stc_pair_psg_export: short read on %s\n", stc_path2);
    exit(1);
  }
  fclose(in2);

  player_pair pair;
  player_status st = player_pair_load_song(
      &pair, stc_path1, data1, sz1, stc_path2, data2, sz2,
      STC_FILE_SAMPLE_RATE_DEF, 0, true);
  free(data1);
  free(data2);
  if (st != PLAYER_OK) {
    fprintf(stderr, "run_stc_pair_psg_export: player_pair_load_song failed (%d)\n",
            (int)st);
    exit(1);
  }
  if (!pair.active) {
    fprintf(stderr, "run_stc_pair_psg_export: pairing did not activate\n");
    exit(1);
  }
  player_pair_set_force_loop(&pair, true);
  if (!psg_export_write_pair(out_path1, out_path2, &pair)) {
    fprintf(stderr, "run_stc_pair_psg_export: psg_export_write_pair failed\n");
    exit(1);
  }
  player_pair_free(&pair);
}

/* MIG-0112: real oracle coverage companion to OracleHarness.pas's
 * RunSTCPairWAVExportTest - loads the SAME file as both player_pair
 * voices (matching this port's real .ayl "ts" pairing semantics - see
 * gui/include/gui/playlist.h's own gui_playlist_entry comment) via the
 * exact production entry point gui/src/playback.c uses
 * (player_pair_load_song/player_pair_make_buffer), and renders the
 * same WaveExportNumBuffers*WaveExportBufferLen (862*512) frame window
 * every other wav_export_<fmt> gate in this project already uses, so
 * it can be byte-compared directly against the Pascal side's own
 * WriteTrackerWAV output. */
static void run_stc_pair(const char* out_path, const char* stc_path) {
  FILE* in = fopen(stc_path, "rb");
  if (!in) {
    fprintf(stderr, "run_stc_pair: cannot open %s\n", stc_path);
    exit(1);
  }
  fseek(in, 0, SEEK_END);
  long sz = ftell(in);
  fseek(in, 0, SEEK_SET);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
    fprintf(stderr, "run_stc_pair: short read on %s\n", stc_path);
    exit(1);
  }
  fclose(in);

  player_pair pair;
  player_status st =
      player_pair_load_song(&pair, stc_path, data, (size_t)sz, stc_path, data,
                             (size_t)sz, STC_FILE_SAMPLE_RATE_DEF, 0, true);
  free(data);
  if (st != PLAYER_OK) {
    fprintf(stderr, "run_stc_pair: player_pair_load_song failed (%d)\n", (int)st);
    exit(1);
  }
  if (!pair.active) {
    fprintf(stderr, "run_stc_pair: pairing did not activate for %s\n", stc_path);
    exit(1);
  }

  wav_writer w;
  if (!wav_writer_open(&w, out_path, 2, STC_FILE_SAMPLE_RATE_DEF, 16)) {
    fprintf(stderr, "run_stc_pair: cannot open %s for writing\n", out_path);
    exit(1);
  }
  int16_t buf[512 * 2];
  for (int n = 0; n < 862; n++) {
    if (player_pair_real_end_all(&pair)) break;
    int frames = player_pair_make_buffer(&pair, buf, 512);
    wav_writer_write(&w, buf, frames);
    if (frames < 512) break;
  }
  wav_writer_close(&w);
  player_pair_free(&pair);
}

/* MIG-0114: same player_pair_load_song/player_pair_make_buffer harness as
 * run_stc_pair above (see its own comment for the full citation), one
 * per pairing-eligible tracker format still needing real oracle coverage.
 * FXM is deliberately excluded - Players.pas's FXM_StekA/B/C stek arrays
 * are UNIT-LEVEL globals, not indexed by CNum, so a genuine dual-FXM
 * Turbosound pair has a real cross-voice shared-mutable-state
 * interaction in the original that this port's per-instance fxm_file
 * structs don't (and shouldn't) replicate - see migration_debt.yaml. */
#define DEFINE_PAIR_RUNNER(name, sample_rate_def)                            \
  static void run_##name##_pair(const char* out_path, const char* path) {    \
    FILE* in = fopen(path, "rb");                                            \
    if (!in) {                                                               \
      fprintf(stderr, "run_" #name "_pair: cannot open %s\n", path);         \
      exit(1);                                                               \
    }                                                                        \
    fseek(in, 0, SEEK_END);                                                  \
    long sz = ftell(in);                                                     \
    fseek(in, 0, SEEK_SET);                                                  \
    uint8_t* data = (uint8_t*)malloc((size_t)sz);                            \
    if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {                      \
      fprintf(stderr, "run_" #name "_pair: short read on %s\n", path);       \
      exit(1);                                                               \
    }                                                                        \
    fclose(in);                                                              \
                                                                               \
    player_pair pair;                                                        \
    player_status st = player_pair_load_song(                                \
        &pair, path, data, (size_t)sz, path, data, (size_t)sz,               \
        sample_rate_def, 0, true);                                           \
    free(data);                                                              \
    if (st != PLAYER_OK) {                                                   \
      fprintf(stderr, "run_" #name "_pair: player_pair_load_song failed (%d)\n", \
              (int)st);                                                      \
      exit(1);                                                               \
    }                                                                        \
    if (!pair.active) {                                                      \
      fprintf(stderr, "run_" #name "_pair: pairing did not activate for %s\n", \
              path);                                                         \
      exit(1);                                                               \
    }                                                                        \
                                                                               \
    wav_writer w;                                                            \
    if (!wav_writer_open(&w, out_path, 2, sample_rate_def, 16)) {            \
      fprintf(stderr, "run_" #name "_pair: cannot open %s for writing\n",    \
              out_path);                                                     \
      exit(1);                                                               \
    }                                                                        \
    int16_t buf[512 * 2];                                                    \
    for (int n = 0; n < 862; n++) {                                          \
      if (player_pair_real_end_all(&pair)) break;                            \
      int frames = player_pair_make_buffer(&pair, buf, 512);                 \
      wav_writer_write(&w, buf, frames);                                     \
      if (frames < 512) break;                                               \
    }                                                                        \
    wav_writer_close(&w);                                                    \
    player_pair_free(&pair);                                                 \
  }

DEFINE_PAIR_RUNNER(pt1, PT1_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(gtr, GTR_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(fls, FLS_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(stp, STP_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(pt2, PT2_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(psm, PSM_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(asc, ASC_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(asc0, ASC_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(ftc, FTC_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(psc, PSC_FILE_SAMPLE_RATE_DEF)
DEFINE_PAIR_RUNNER(sqt, SQT_FILE_SAMPLE_RATE_DEF)

#undef DEFINE_PAIR_RUNNER

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <zx|cpc|immediate|pcm|ay_file> <output-path> [extra-arg]\n", argv[0]);
    return 1;
  }
  const char* scenario = argv[1];
  const char* out_path = argv[2];

  if (strcmp(scenario, "zx") == 0) run_zx(out_path);
  else if (strcmp(scenario, "cpc") == 0) run_cpc(out_path);
  else if (strcmp(scenario, "immediate") == 0) run_immediate(out_path);
  else if (strcmp(scenario, "immediate_in") == 0) run_immediate_in(out_path);
  else if (strcmp(scenario, "pcm") == 0) run_pcm(out_path);
  else if (strcmp(scenario, "pcm_filtered") == 0) run_pcm_filtered(out_path);
  else if (strcmp(scenario, "pcm8") == 0) run_pcm8(out_path);
  else if (strcmp(scenario, "m68k") == 0) run_m68k(out_path);
  else if (strcmp(scenario, "mfp") == 0) run_mfp(out_path);
  else if (strcmp(scenario, "dma") == 0) run_dma(out_path);
  else if (strcmp(scenario, "ay_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s ay_file <output-path> <ay-file-path>\n", argv[0]);
      return 1;
    }
    run_ay_file(out_path, argv[3]);
  } else if (strcmp(scenario, "ym_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s ym_file <output-path> <ym-file-path>\n", argv[0]);
      return 1;
    }
    run_ym_file(out_path, argv[3]);
  } else if (strcmp(scenario, "pt3_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s pt3_file <output-path> <pt3-file-path>\n", argv[0]);
      return 1;
    }
    run_pt3_file(out_path, argv[3]);
  } else if (strcmp(scenario, "pt1_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s pt1_file <output-path> <pt1-file-path>\n", argv[0]);
      return 1;
    }
    run_pt1_file(out_path, argv[3]);
  } else if (strcmp(scenario, "gtr_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s gtr_file <output-path> <gtr-file-path>\n", argv[0]);
      return 1;
    }
    run_gtr_file(out_path, argv[3]);
  } else if (strcmp(scenario, "fls_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s fls_file <output-path> <fls-file-path>\n", argv[0]);
      return 1;
    }
    run_fls_file(out_path, argv[3]);
  } else if (strcmp(scenario, "stc_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s stc_file <output-path> <stc-file-path>\n", argv[0]);
      return 1;
    }
    run_stc_file(out_path, argv[3]);
  } else if (strcmp(scenario, "stp_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s stp_file <output-path> <stp-file-path>\n", argv[0]);
      return 1;
    }
    run_stp_file(out_path, argv[3]);
  } else if (strcmp(scenario, "pt2_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s pt2_file <output-path> <pt2-file-path>\n", argv[0]);
      return 1;
    }
    run_pt2_file(out_path, argv[3]);
  } else if (strcmp(scenario, "fxm_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s fxm_file <output-path> <fxm-file-path>\n", argv[0]);
      return 1;
    }
    run_fxm_file(out_path, argv[3]);
  } else if (strcmp(scenario, "psm_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s psm_file <output-path> <psm-file-path>\n", argv[0]);
      return 1;
    }
    run_psm_file(out_path, argv[3]);
  } else if (strcmp(scenario, "asc_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s asc_file <output-path> <asc-file-path>\n", argv[0]);
      return 1;
    }
    run_asc_file(out_path, argv[3], false);
  } else if (strcmp(scenario, "asc0_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s asc0_file <output-path> <asc0-file-path>\n", argv[0]);
      return 1;
    }
    run_asc_file(out_path, argv[3], true);
  } else if (strcmp(scenario, "ftc_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s ftc_file <output-path> <ftc-file-path>\n", argv[0]);
      return 1;
    }
    run_ftc_file(out_path, argv[3]);
  } else if (strcmp(scenario, "psc_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s psc_file <output-path> <psc-file-path>\n", argv[0]);
      return 1;
    }
    run_psc_file(out_path, argv[3]);
  } else if (strcmp(scenario, "sqt_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s sqt_file <output-path> <sqt-file-path>\n", argv[0]);
      return 1;
    }
    run_sqt_file(out_path, argv[3]);
  } else if (strcmp(scenario, "vtx_file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s vtx_file <output-path> <vtx-file-path>\n", argv[0]);
      return 1;
    }
    run_vtx_file(out_path, argv[3]);
  } else if (strcmp(scenario, "get_time") == 0) {
    if (argc < 5) {
      fprintf(stderr, "usage: %s get_time <output-path> <format> <file-path>\n", argv[0]);
      return 1;
    }
    run_get_time(out_path, argv[3], argv[4]);
  } else if (strcmp(scenario, "sndh_unpack") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s sndh_unpack <output-path> <sndh-file-path>\n", argv[0]);
      return 1;
    }
    run_sndh_unpack(out_path, argv[3]);
  } else if (strcmp(scenario, "stc_psg_export") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s stc_psg_export <output-path> <stc-file-path>\n", argv[0]);
      return 1;
    }
    run_psg_export(out_path, argv[3]);
  } else if (strcmp(scenario, "ay_psg_export") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s ay_psg_export <output-path> <ay-file-path>\n", argv[0]);
      return 1;
    }
    run_psg_export(out_path, argv[3]);
  } else if (strcmp(scenario, "ym_psg_export") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s ym_psg_export <output-path> <ym-file-path>\n", argv[0]);
      return 1;
    }
    run_psg_export(out_path, argv[3]);
  } else if (strcmp(scenario, "vtx_psg_export") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s vtx_psg_export <output-path> <vtx-file-path>\n", argv[0]);
      return 1;
    }
    run_psg_export(out_path, argv[3]);
  } else if (strcmp(scenario, "out_psg_export") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s out_psg_export <output-path> <out-file-path>\n", argv[0]);
      return 1;
    }
    run_psg_export(out_path, argv[3]);
  } else if (strcmp(scenario, "epsg_psg_export") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s epsg_psg_export <output-path> <epsg-file-path>\n", argv[0]);
      return 1;
    }
    run_psg_export(out_path, argv[3]);
  } else if (strcmp(scenario, "stc_vtx_raw") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s stc_vtx_raw <output-path> <stc-file-path>\n", argv[0]);
      return 1;
    }
    run_vtx_raw(out_path, argv[3]);
  } else if (strcmp(scenario, "ay_vtx_raw") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s ay_vtx_raw <output-path> <ay-file-path>\n", argv[0]);
      return 1;
    }
    run_vtx_raw(out_path, argv[3]);
  } else if (strcmp(scenario, "ym_vtx_raw") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s ym_vtx_raw <output-path> <ym-file-path>\n", argv[0]);
      return 1;
    }
    run_vtx_raw(out_path, argv[3]);
  } else if (strcmp(scenario, "stc_pair_psg_export") == 0) {
    if (argc < 6) {
      fprintf(stderr,
              "usage: %s stc_pair_psg_export <out1-path> <out2-path> "
              "<stc1-path> <stc2-path>\n",
              argv[0]);
      return 1;
    }
    run_stc_pair_psg_export(out_path, argv[3], argv[4], argv[5]);
  } else if (strcmp(scenario, "vtx_vtx_raw") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s vtx_vtx_raw <output-path> <vtx-file-path>\n", argv[0]);
      return 1;
    }
    run_vtx_raw(out_path, argv[3]);
  } else if (strcmp(scenario, "stc_pair") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: %s stc_pair <output-path> <stc-file-path>\n", argv[0]);
      return 1;
    }
    run_stc_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "pt1_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s pt1_pair <output-path> <pt1-file-path>\n", argv[0]); return 1; }
    run_pt1_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "gtr_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s gtr_pair <output-path> <gtr-file-path>\n", argv[0]); return 1; }
    run_gtr_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "fls_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s fls_pair <output-path> <fls-file-path>\n", argv[0]); return 1; }
    run_fls_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "stp_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s stp_pair <output-path> <stp-file-path>\n", argv[0]); return 1; }
    run_stp_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "pt2_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s pt2_pair <output-path> <pt2-file-path>\n", argv[0]); return 1; }
    run_pt2_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "psm_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s psm_pair <output-path> <psm-file-path>\n", argv[0]); return 1; }
    run_psm_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "asc_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s asc_pair <output-path> <asc-file-path>\n", argv[0]); return 1; }
    run_asc_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "asc0_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s asc0_pair <output-path> <as0-file-path>\n", argv[0]); return 1; }
    run_asc0_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "ftc_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s ftc_pair <output-path> <ftc-file-path>\n", argv[0]); return 1; }
    run_ftc_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "psc_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s psc_pair <output-path> <psc-file-path>\n", argv[0]); return 1; }
    run_psc_pair(out_path, argv[3]);
  } else if (strcmp(scenario, "sqt_pair") == 0) {
    if (argc < 4) { fprintf(stderr, "usage: %s sqt_pair <output-path> <sqt-file-path>\n", argv[0]); return 1; }
    run_sqt_pair(out_path, argv[3]);
  } else {
    fprintf(stderr, "unknown scenario '%s'\n", scenario);
    return 1;
  }
  return 0;
}
