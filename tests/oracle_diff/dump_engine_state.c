/* Companion to ay_emul/OracleHarness.pas (in the submodule). Drives
 * engine/z80_bus.c + engine/ay.c through the SAME synthetic instruction/
 * register sequences the oracle harness runs through the real Pascal
 * Z80.pas/AY.pas, and dumps comparable output for byte-for-byte diffing
 * via run_diff.sh. See migration_debt.yaml MIG-0003/0005/0006b. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/ay.h"
#include "ay_engine/ay_file.h"
#include "ay_engine/ym_file.h"
#include "ay_engine/pt3_file.h"
#include "ay_engine/pt1_file.h"
#include "ay_engine/gtr_file.h"
#include "ay_engine/fls_file.h"
#include "ay_engine/stc_file.h"
#include "ay_engine/stp_file.h"
#include "ay_engine/pt2_file.h"
#include "ay_engine/fxm_file.h"
#include "ay_engine/psm_file.h"
#include "ay_engine/asc_file.h"
#include "ay_engine/ftc_file.h"
#include "ay_engine/psc_file.h"
#include "ay_engine/sqt_file.h"
#include "ay_engine/vtx_file.h"
#include "ay_engine/z80_bus.h"
#include "ay_engine/m68k_bus.h"
#include "ay_engine/mfp.h"
#include "ay_engine/dma_sound.h"
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
  } else {
    fprintf(stderr, "unknown scenario '%s'\n", scenario);
    return 1;
  }
  return 0;
}
