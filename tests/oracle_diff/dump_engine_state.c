/* Companion to ay_emul/OracleHarness.pas (in the submodule). Drives
 * engine/z80_bus.c + engine/ay.c through the SAME synthetic instruction/
 * register sequences the oracle harness runs through the real Pascal
 * Z80.pas/AY.pas, and dumps comparable output for byte-for-byte diffing
 * via run_diff.sh. See migration_debt.yaml MIG-0003/0005/0006b. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/ay.h"
#include "ay_engine/z80_bus.h"
#include "ay_engine/m68k_bus.h"
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

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <zx|cpc|immediate|pcm> <output-path>\n", argv[0]);
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
  else {
    fprintf(stderr, "unknown scenario '%s'\n", scenario);
    return 1;
  }
  return 0;
}
