/* Adapter binding Musashi to ay_emul's 68000 memory/interrupt model - see
 * engine/include/ay_engine/m68k_bus.h for the ported contract and scope. */
#include "ay_engine/hw/m68k_bus.h"

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

void m68k_bus_set_irq(m68k_bus* bus, int level) {
  g_active_bus = bus;
  m68k_set_irq((unsigned int)level);
}

int m68k_bus_cycles_run(void) { return m68k_cycles_run(); }

void m68k_bus_end_timeslice(void) { m68k_end_timeslice(); }

/* MIG-0056: hook-based Starscream-timing override - see m68k_bus.h's
 * declaration comment for the policy/scope context. Exact opcode
 * patterns and their target cycle costs, cross-derived from two
 * independent sources per pattern (kept in sync, not guessed):
 *  - Musashi's own declared per-opcode cost, engine/third_party/musashi/
 *    m68k_in.c's opcode table (the "68000" column).
 *  - Starscream's own cost-generator formulas, ay_emul/star027b/star.c
 *    (addsuba() for ADDA, i_clr() for CLR, op_to_dn() for ADD), which
 *    compute a base cost plus a per-addressing-mode "main_ea_cycles()"
 *    add-on - evaluated here for the ONE specific addressing submode each
 *    pattern below matches (register-direct source for ADDA.L, (An)
 *    memory-indirect for CLR.W, immediate source for ADD.W). Other
 *    submodes of these same opcodes are NOT covered (register/absolute/
 *    displacement/indexed forms of CLR.W, memory-indirect/pc-relative
 *    forms of ADDA.L/ADD.W, etc.) - deliberately narrow, tracked as such
 *    in migration_debt.yaml MIG-0056, not a general per-opcode cost model. */
typedef struct {
  uint16_t mask, match;
  int target_cycles; /* Starscream's cost for this exact pattern */
} timing_pattern;

static const timing_pattern g_timing_patterns[] = {
  /* ADDA.L Dn,An - "1101 aaa 111 000 ddd". Musashi: 6 (flat, m68k_in.c
   * line "adda 32 . d ... 6 6 2 2 2"). Starscream: addsuba() base=6,
   * +2 register/immediate penalty on 68000 for dreg source = 8. */
  { 0xF1F8, 0xD1C0, 8 },
  /* ADDA.L An,An - "1101 aaa 111 001 aaa". Same costs as above (areg
   * source gets the identical +2 penalty). */
  { 0xF1F8, 0xD1C8, 8 },
  /* CLR.W (An) - "0100 0010 01 001 nnn" (aind addressing mode only).
   * Musashi: 8 (m68k_in.c "clr 16 . . ... 8 4 4 4 4", matches the real,
   * published Motorola CLR.W (An) timing exactly). Starscream: i_clr()
   * base=6 + main_ea_cycles(aind,word)=4 = 10 - Starscream is the
   * non-standard one here, not Musashi (see m68k_bus.h's policy note -
   * this override target-matches Starscream anyway, per explicit user
   * directive, despite Musashi already being correct for this case). */
  { 0xFFF8, 0x4248, 10 },
  /* ADD.W #imm,Dn - "1101 ddd 001 111 100" (immediate source only).
   * Musashi: m68k_in.c "add 16 er . ... 4 4 2 2 2" (base table entry;
   * measured empirically via this same mechanism against the real
   * running program to confirm the actual total, since Musashi's table
   * doesn't show whether immediate-fetch overhead is folded in - see
   * MIG-0056's verification notes). Starscream: op_to_dn() base=4 (word
   * size, no long-penalty) + main_ea_cycles(immd,word)=4 = 8. */
  { 0xF1FF, 0xD03C, 8 },
};
#define TIMING_PATTERN_COUNT \
  (sizeof(g_timing_patterns) / sizeof(g_timing_patterns[0]))

static bool g_timing_have_prev = false;
static int g_timing_prev_cycles_run = 0;
static int g_timing_prev_pattern = -1; /* index into g_timing_patterns, -1 none */
static int64_t g_timing_pending_correction = 0;

static int match_timing_pattern(uint16_t opcode) {
  size_t i;
  for (i = 0; i < TIMING_PATTERN_COUNT; i++) {
    if ((opcode & g_timing_patterns[i].mask) == g_timing_patterns[i].match) {
      return (int)i;
    }
  }
  return -1;
}

static void timing_instr_hook(unsigned int pc) {
  if (g_timing_have_prev) {
    /* The gap between the PREVIOUS hook call and this one is exactly how
     * many cycles Musashi charged for the PREVIOUS instruction (the hook
     * fires before fetch/dispatch, so m68k_cycles_run() at this call
     * reflects everything charged for instructions before the one about
     * to run now). */
    int actual = m68k_cycles_run() - g_timing_prev_cycles_run;
    if (g_timing_prev_pattern >= 0) {
      int target = g_timing_patterns[g_timing_prev_pattern].target_cycles;
      g_timing_pending_correction += (target - actual);
    }
  }
  uint16_t opcode = (uint16_t)((read_byte(pc) << 8) | read_byte(pc + 1));
  g_timing_prev_pattern = match_timing_pattern(opcode);
  g_timing_prev_cycles_run = m68k_cycles_run();
  g_timing_have_prev = true;
}

void m68k_bus_enable_starscream_timing_override(bool enable) {
  g_timing_have_prev = false;
  g_timing_prev_pattern = -1;
  g_timing_pending_correction = 0;
  m68k_set_instr_hook_callback(enable ? timing_instr_hook : NULL);
}

int64_t m68k_bus_take_timing_correction(void) {
  int64_t c = g_timing_pending_correction;
  g_timing_pending_correction = 0;
  return c;
}
