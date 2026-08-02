/* Adapter binding Musashi to ay_emul's 68000 memory/interrupt model - see
 * engine/include/ay_engine/m68k_bus.h for the ported contract and scope. */
#include "ay_engine/m68k_bus.h"

#include <string.h>

#include "m68k.h"

/* Musashi's core memory hooks are fixed-name, link-time functions (not a
 * runtime callback struct) - see m68k_bus.h's file comment. This is the
 * one process-wide "active bus" those functions consult. */
static m68k_bus* g_active_bus = NULL;

void m68k_bus_init(m68k_bus* bus) {
  memset(bus, 0, sizeof(*bus));
  int i;
  for (i = 0; i < M68K_BUS_MAX_REGIONS; i++) {
    bus->regions[i].low = 1;
    bus->regions[i].high = 0; /* low > high: unused slot */
  }
  m68k_init();
  m68k_set_cpu_type(M68K_CPU_TYPE_68000); /* Atari ST: plain 68000 */
}

void m68k_bus_add_flat_region(m68k_bus* bus, uint32_t low, uint32_t high,
                               uint8_t* flat) {
  m68k_bus_region* r = &bus->regions[bus->region_count++];
  r->low = low;
  r->high = high;
  r->flat = flat;
}

void m68k_bus_add_callback_region(m68k_bus* bus, uint32_t low, uint32_t high,
                                   m68k_bus_read_cb read_cb,
                                   m68k_bus_write_cb write_cb,
                                   void* userdata) {
  m68k_bus_region* r = &bus->regions[bus->region_count++];
  r->low = low;
  r->high = high;
  r->read_cb = read_cb;
  r->write_cb = write_cb;
  r->userdata = userdata;
}

static m68k_bus_region* find_region(uint32_t address) {
  int i;
  if (g_active_bus == NULL) return NULL;
  for (i = 0; i < g_active_bus->region_count; i++) {
    m68k_bus_region* r = &g_active_bus->regions[i];
    if (address >= r->low && address <= r->high) return r;
  }
  return NULL;
}

static uint8_t read_byte(uint32_t address) {
  m68k_bus_region* r = find_region(address);
  if (r == NULL) return 0xFF; /* unmapped: matches real-hardware bus-error-ish
                               * "all ones" convention loosely; no in-scope
                               * test relies on unmapped-read behavior yet */
  if (r->flat != NULL) return r->flat[address - r->low];
  if (r->read_cb != NULL) return r->read_cb(r->userdata, address);
  return 0xFF;
}

static void write_byte(uint32_t address, uint8_t value) {
  m68k_bus_region* r = find_region(address);
  if (r == NULL) return;
  if (r->flat != NULL) {
    r->flat[address - r->low] = value;
    return;
  }
  if (r->write_cb != NULL) r->write_cb(r->userdata, address, value);
}

/* Musashi's fixed-name, link-time memory hooks (see m68k_bus.h). Word/long
 * accesses are built from byte accesses in big-endian order, matching real
 * 68000 bus semantics - this sidesteps needing to replicate Starscream's
 * host-native-word "IntelizeMemory" pre-swap trick (Atari.pas:1000-1008):
 * our regions store memory in ordinary big-endian byte order, matching how
 * a real 68000 program image is laid out, with no transform required. */
uint32_t m68k_read_memory_8(uint32_t address) { return read_byte(address); }
uint32_t m68k_read_memory_16(uint32_t address) {
  return ((uint32_t)read_byte(address) << 8) | read_byte(address + 1);
}
uint32_t m68k_read_memory_32(uint32_t address) {
  return (m68k_read_memory_16(address) << 16) |
         m68k_read_memory_16(address + 2);
}
void m68k_write_memory_8(uint32_t address, uint32_t value) {
  write_byte(address, (uint8_t)value);
}
void m68k_write_memory_16(uint32_t address, uint32_t value) {
  write_byte(address, (uint8_t)(value >> 8));
  write_byte(address + 1, (uint8_t)value);
}
void m68k_write_memory_32(uint32_t address, uint32_t value) {
  m68k_write_memory_16(address, value >> 16);
  m68k_write_memory_16(address + 2, value);
}

uint32_t m68k_read_disassembler_8(uint32_t a) { return m68k_read_memory_8(a); }
uint32_t m68k_read_disassembler_16(uint32_t a) {
  return m68k_read_memory_16(a);
}
uint32_t m68k_read_disassembler_32(uint32_t a) {
  return m68k_read_memory_32(a);
}

static int int_ack_trampoline(int level) {
  if (g_active_bus != NULL && g_active_bus->int_ack != NULL) {
    int vector = g_active_bus->int_ack(g_active_bus->int_ack_userdata, level);
    if (vector >= 0) return vector;
  }
  return M68K_INT_ACK_AUTOVECTOR;
}

void m68k_bus_activate(m68k_bus* bus) {
  g_active_bus = bus;
  m68k_set_int_ack_callback(int_ack_trampoline);
}

void m68k_bus_reset(m68k_bus* bus) {
  g_active_bus = bus;
  m68k_pulse_reset();
}

int m68k_bus_exec(m68k_bus* bus, int cycles) {
  g_active_bus = bus;
  return m68k_execute(cycles);
}

uint32_t m68k_bus_get_reg(int reg) {
  return m68k_get_reg(NULL, (m68k_register_t)reg);
}

void m68k_bus_set_reg(int reg, uint32_t value) {
  m68k_set_reg((m68k_register_t)reg, value);
}
