/* Self-consistency smoke tests for engine/src/m68k_bus.c. Real differential
 * validation against the original's Starscream core lives in
 * tests/oracle_diff (the "m68k" scenario) - see README.md. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ay_engine/hw/m68k_bus.h"
#include "m68k.h"

static void test_moveq_add(void) {
  static uint8_t mem[0x10000];
  memset(mem, 0, sizeof(mem));

  mem[0] = 0x00; mem[1] = 0x00; mem[2] = 0x10; mem[3] = 0x00; /* SSP */
  mem[4] = 0x00; mem[5] = 0x00; mem[6] = 0x04; mem[7] = 0x00; /* PC */

  mem[0x400] = 0x70; mem[0x401] = 0x05; /* MOVEQ #5,D0 */
  mem[0x402] = 0x72; mem[0x403] = 0x07; /* MOVEQ #7,D1 */
  mem[0x404] = 0xD0; mem[0x405] = 0x81; /* ADD.L D1,D0 */
  mem[0x406] = 0x4E; mem[0x407] = 0x72; /* STOP #$2700 */
  mem[0x408] = 0x27; mem[0x409] = 0x00;

  m68k_bus bus;
  m68k_bus_init(&bus);
  m68k_bus_add_flat_region(&bus, 0, sizeof(mem) - 1, mem);
  m68k_bus_activate(&bus);
  m68k_bus_reset(&bus);

  assert(m68k_bus_get_reg(M68K_REG_PC) == 0x400);
  assert(m68k_bus_get_reg(M68K_REG_A7) == 0x1000);

  m68k_bus_exec(&bus, 200);

  assert(m68k_bus_get_reg(M68K_REG_D0) == 12);
  printf("test_moveq_add: OK (d0=%u)\n", m68k_bus_get_reg(M68K_REG_D0));
}

static void test_flat_memory_write(void) {
  static uint8_t mem[0x10000];
  memset(mem, 0, sizeof(mem));

  mem[0] = 0x00; mem[1] = 0x00; mem[2] = 0x10; mem[3] = 0x00;
  mem[4] = 0x00; mem[5] = 0x00; mem[6] = 0x04; mem[7] = 0x00;

  mem[0x400] = 0x70; mem[0x401] = 0x2A; /* MOVEQ #42,D0 */
  /* MOVE.L D0,$2000.L */
  mem[0x402] = 0x23; mem[0x403] = 0xC0; mem[0x404] = 0x00; mem[0x405] = 0x00;
  mem[0x406] = 0x20; mem[0x407] = 0x00;
  mem[0x408] = 0x4E; mem[0x409] = 0x72; mem[0x40A] = 0x27; mem[0x40B] = 0x00;

  m68k_bus bus;
  m68k_bus_init(&bus);
  m68k_bus_add_flat_region(&bus, 0, sizeof(mem) - 1, mem);
  m68k_bus_activate(&bus);
  m68k_bus_reset(&bus);
  m68k_bus_exec(&bus, 200);

  uint32_t written = ((uint32_t)mem[0x2000] << 24) | ((uint32_t)mem[0x2001] << 16) |
                      ((uint32_t)mem[0x2002] << 8) | mem[0x2003];
  assert(written == 42);
  printf("test_flat_memory_write: OK (written=%u)\n", written);
}

static uint8_t g_reg_value = 0;
static int g_reg_write_count = 0;

static uint8_t reg_read(void* ud, uint32_t address) {
  (void)ud;
  (void)address;
  return g_reg_value;
}
static void reg_write(void* ud, uint32_t address, uint8_t value) {
  (void)ud;
  (void)address;
  g_reg_value = value;
  g_reg_write_count++;
}

static void test_callback_region(void) {
  static uint8_t mem[0x10000];
  memset(mem, 0, sizeof(mem));

  mem[0] = 0x00; mem[1] = 0x00; mem[2] = 0x10; mem[3] = 0x00;
  mem[4] = 0x00; mem[5] = 0x00; mem[6] = 0x04; mem[7] = 0x00;

  mem[0x400] = 0x70; mem[0x401] = 0x63; /* MOVEQ #99,D0 */
  /* MOVE.B D0,$FF8800.L */
  mem[0x402] = 0x13; mem[0x403] = 0xC0; mem[0x404] = 0x00; mem[0x405] = 0xFF;
  mem[0x406] = 0x88; mem[0x407] = 0x00;
  mem[0x408] = 0x4E; mem[0x409] = 0x72; mem[0x40A] = 0x27; mem[0x40B] = 0x00;

  m68k_bus bus;
  m68k_bus_init(&bus);
  m68k_bus_add_flat_region(&bus, 0, sizeof(mem) - 1, mem);
  m68k_bus_add_callback_region(&bus, 0xFF8800, 0xFF88FF, reg_read, reg_write,
                                NULL);
  m68k_bus_activate(&bus);
  m68k_bus_reset(&bus);
  m68k_bus_exec(&bus, 200);

  assert(g_reg_write_count == 1);
  assert(g_reg_value == 99);
  printf("test_callback_region: OK (value=%u)\n", g_reg_value);
}

int main(void) {
  test_moveq_add();
  test_flat_memory_write();
  test_callback_region();
  printf("All m68k_bus smoke tests passed.\n");
  return 0;
}
