/* Adapter binding Musashi (engine/third_party/musashi) to the memory/
 * interrupt model ay_emul/Starcpu.inc + mc68000.pas/atari.pas expose,
 * mirroring engine/z80_bus.h's role for the Z80 core. See the approved
 * plan and migration_debt.yaml for scope: this milestone ports the bare
 * 68000 core and a generic memory-region mechanism only - MFP timers, DMA
 * sound, and the YM2149 region wiring are follow-on work.
 *
 * Musashi's core memory-access hooks (m68k_read_memory_8/16/32,
 * m68k_write_memory_8/16/32) are fixed-name functions resolved at LINK
 * TIME, not a runtime callback struct - Musashi is a singleton core (its
 * own file-scope internal state), unlike superzazu/z80's per-instance
 * struct. This adapter therefore holds one process-wide "active bus"
 * pointer (m68k_bus_activate()) that those fixed-name functions consult -
 * acceptable because ay_emul never runs two 68000s concurrently, but a
 * real constraint worth stating plainly rather than discovering later.
 *
 * Memory is modeled as an ordered table of address-ranged regions, mirroring
 * Starcpu.inc's TSTARSCREAM_DATAREGION/PROGRAMREGION (lowaddr, highaddr,
 * memorycall-or-direct-offset): each region is either a flat buffer (direct
 * offset access, like atari.pas's `bank0` RAM region) or a read/write
 * callback pair (like atari.pas's soundchip_readbyte/mfp_readbyte/
 * stedac_readbyte hooks) - this milestone registers only a flat RAM region
 * for the fidelity gate; the real YM2149/MFP/DMA-sound callback regions are
 * follow-on work (see migration_debt.yaml).
 */
#ifndef AY_ENGINE_M68K_BUS_H
#define AY_ENGINE_M68K_BUS_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t (*m68k_bus_read_cb)(void* userdata, uint32_t address);
typedef void (*m68k_bus_write_cb)(void* userdata, uint32_t address,
                                   uint8_t value);

#define M68K_BUS_MAX_REGIONS 8

typedef struct {
  uint32_t low, high; /* inclusive; low > high marks an unused slot */
  uint8_t* flat;       /* non-NULL: direct flat-buffer region */
  m68k_bus_read_cb read_cb;   /* non-NULL: callback region (read) */
  m68k_bus_write_cb write_cb; /* non-NULL: callback region (write) */
  void* userdata;
} m68k_bus_region;

typedef struct m68k_bus {
  m68k_bus_region regions[M68K_BUS_MAX_REGIONS];
  int region_count;

  /* Invoked when Musashi acknowledges a vectored interrupt (requires
   * M68K_EMULATE_INT_ACK enabled in m68kconf.h - see that file's comment),
   * mirroring Starcpu.inc's s68000interrupt(level,vector) contract. Return
   * the vector number (2..255), or a negative value to request autovector.
   * NULL is a valid no-op (defaults to autovector, matching Musashi's
   * M68K_EMULATE_INT_ACK-off behavior). */
  int (*int_ack)(void* userdata, int level);
  void* int_ack_userdata;
} m68k_bus;

void m68k_bus_init(m68k_bus* bus);

/* Registers a flat memory region (e.g. RAM/ROM), matching
 * Starcpu.inc's SetDataRegion(...,memorycall:nil,userdata:@buf). */
void m68k_bus_add_flat_region(m68k_bus* bus, uint32_t low, uint32_t high,
                               uint8_t* flat);

/* Registers a callback-backed region (e.g. a hardware register block),
 * matching SetDataRegion(...,memorycall:@func,userdata:nil). Not used by
 * this milestone (see ay.h-style scope comment above) but built generically
 * for the MFP/DMA-sound/YM2149 follow-on work. */
void m68k_bus_add_callback_region(m68k_bus* bus, uint32_t low, uint32_t high,
                                   m68k_bus_read_cb read_cb,
                                   m68k_bus_write_cb write_cb, void* userdata);

/* Makes `bus` the active bus for Musashi's fixed-name memory hooks and
 * interrupt-ack callback. Must be called before m68k_bus_reset/exec. */
void m68k_bus_activate(m68k_bus* bus);

void m68k_bus_reset(m68k_bus* bus); /* m68k_pulse_reset(), reads SSP/PC from
                                     * the active bus's regions at 0/4 -
                                     * matches Starcpu.inc's s68000reset. */

/* Executes instructions until at least `cycles` have been used (finishing
 * the current instruction even if it overruns), mirroring Starcpu.inc's
 * s68000exec(n):longword. Returns cycles actually used. */
int m68k_bus_exec(m68k_bus* bus, int cycles);

/* M68K_REG_* constants from m68k.h (D0-D7=0-7, A0-A7=8-15, PC, SR, ...). */
uint32_t m68k_bus_get_reg(int reg);
void m68k_bus_set_reg(int reg, uint32_t value);

/* Asserts the 68000's IPL lines at `level` (0-7; 0 clears), mirroring
 * Musashi's m68k_set_irq - the level-triggered counterpart to
 * Starcpu.inc's per-call s68000interrupt(level,vector). The CPU services
 * it (and consults `int_ack` for the vector) on its own at the next
 * eligible instruction boundary during m68k_bus_exec - there is no
 * separate "try now" call the way s68000interrupt is, since Musashi
 * doesn't need one (see engine/mfp.h's file comment for the fuller
 * rationale). */
void m68k_bus_set_irq(m68k_bus* bus, int level);

#endif /* AY_ENGINE_M68K_BUS_H */
