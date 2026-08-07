/* Synthetic-program tests for engine/src/z80_bus.c's port-decode logic.
 * See README.md for what this does and doesn't validate. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ay_engine/hw/z80_bus.h"

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

static unsigned long recorded_freq;
static void on_freq(void* ud, unsigned long hz) {
  (void)ud;
  recorded_freq = hz;
}

static void run_until_halt(z80_bus* bus, int max_steps) {
  int i;
  for (i = 0; i < max_steps && !bus->cpu.halted; i++) {
    z80_bus_step(bus);
  }
  assert(bus->cpu.halted && "test program did not halt in time");
}

/* LD BC,nnnn ; LD A,n ; OUT (C),A  =  01 lo hi | 3E n | ED 79 */
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

static void test_zx_protocol(void) {
  z80_bus bus;
  recorded_write rw;
  memset(&rw, 0, sizeof(rw));
  z80_bus_init(&bus, 100000);
  bus.machine = Z80_BUS_MACHINE_ZX;
  bus.on_ay_write = on_write;
  bus.ay_write_userdata = &rw;

  int pc = 0;
  pc = emit_out_c_a(bus.ram, pc, 0xFFFD, 8);  /* select register 8 */
  pc = emit_out_c_a(bus.ram, pc, 0xBFFD, 15); /* write value 15 */
  bus.ram[pc] = 0x76;                          /* HALT */

  run_until_halt(&bus, 100);

  assert(rw.count == 1);
  assert(rw.reg == 8);
  assert(rw.data == 15);
  printf("test_zx_protocol: OK (reg=%u data=%u)\n", rw.reg, rw.data);
}

static void test_cpc_protocol_explicit(void) {
  z80_bus bus;
  recorded_write rw;
  memset(&rw, 0, sizeof(rw));
  z80_bus_init(&bus, 100000);
  bus.machine = Z80_BUS_MACHINE_CPC;
  bus.on_ay_write = on_write;
  bus.ay_write_userdata = &rw;

  /* Real CPC PSG-via-PPI protocol (matches Z80.pas's live CPCCheckPIO case
   * labels $C0=address/select, $80=write): select the register, then
   * return the control latch to "inactive" (0) *before* caching the data
   * byte on $F4 - CPCOutProc re-runs CPCCheckPIO against whatever
   * cpc_switch currently is on *every* $F4 write (Z80.pas:11026-11033), so
   * skipping the inactive step would re-trigger "address" mode with the
   * data byte misread as a register number. */
  int pc = 0;
  pc = emit_out_c_a(bus.ram, pc, 0xF400, 7);    /* cache register number 7 */
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0xC0); /* latch: ay_cur_reg := 7 */
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0x00); /* back to inactive */
  pc = emit_out_c_a(bus.ram, pc, 0xF400, 0x38); /* cache value 0x38 */
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0x80); /* commit write */
  bus.ram[pc] = 0x76;

  run_until_halt(&bus, 100);

  assert(rw.count == 1);
  assert(rw.reg == 7);
  assert(rw.data == 0x38);
  printf("test_cpc_protocol_explicit: OK (reg=%u data=0x%02x)\n", rw.reg,
         rw.data);
}

static void test_initial_autodetect_cpc(void) {
  z80_bus bus;
  recorded_write rw;
  memset(&rw, 0, sizeof(rw));
  z80_bus_init(&bus, 100000);
  bus.machine = Z80_BUS_MACHINE_INITIAL;
  bus.ay_file_enable_auto_switch = true;
  bus.on_ay_write = on_write;
  bus.ay_write_userdata = &rw;
  bus.on_chip_freq_change = on_freq;
  recorded_freq = 0;

  int pc = 0;
  pc = emit_out_c_a(bus.ram, pc, 0xF400, 7);
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0xC0);
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0x00);
  pc = emit_out_c_a(bus.ram, pc, 0xF400, 0x38);
  pc = emit_out_c_a(bus.ram, pc, 0xF600, 0x80);
  bus.ram[pc] = 0x76;

  run_until_halt(&bus, 100);

  assert(bus.machine == Z80_BUS_MACHINE_CPC);
  assert(recorded_freq == 1000000UL);
  assert(rw.count == 1);
  assert(rw.reg == 7 && rw.data == 0x38);
  printf("test_initial_autodetect_cpc: OK (locked machine=CPC, freq=%lu)\n",
         recorded_freq);
}

static void test_interrupt_once_per_frame(void) {
  /* A tight loop of NOPs long enough to cross max_tstates several times;
   * confirm z80_gen_int is asserted-and-either-accepted-or-withdrawn each
   * frame, not accumulated/stuck across frames (Z80.pas:21027's one-shot
   * per-frame pulse model - see z80_bus.c's int_pending_this_frame). */
  z80_bus bus;
  z80_bus_init(&bus, 40); /* deliberately tiny frame length */
  bus.machine = Z80_BUS_MACHINE_ZX;
  memset(bus.ram, 0x00, sizeof(bus.ram)); /* NOP forever */
  bus.cpu.pc = 0;
  bus.cpu.iff1 = 0; /* interrupts disabled: every pulse should be missed */

  int i;
  for (i = 0; i < 2000; i++) {
    z80_bus_step(&bus);
    /* With IFF1 disabled, int_pending must never remain "stuck" set once
     * its 32-T window closes each frame. */
    if (bus.int_pending_this_frame) {
      assert(bus.current_tact < bus.int_length);
    }
  }
  printf("test_interrupt_once_per_frame: OK\n");
}

int main(void) {
  test_zx_protocol();
  test_cpc_protocol_explicit();
  test_initial_autodetect_cpc();
  test_interrupt_once_per_frame();
  printf("All z80_bus smoke tests passed.\n");
  return 0;
}
