/* Adapter binding superzazu/z80 (engine/third_party/z80) to the memory/port
 * model ay_emul/Z80.pas's live (actually-compiled) code exercises, per the
 * approved plan and tests/zexall/FIDELITY_GATE.md.
 *
 * Faithfully reproduces, from Z80.pas's live {$else Z80Emu_noASM} branch
 * (Z80.pas:10854-22052 - the {$ifdef Z80Emu_ASM} branch above it is dead
 * code, see FIDELITY_GATE.md):
 *  - Flat 64K memory (Z80.pas: ZRAM.Index), no banking.
 *  - The ZX-Spectrum-style and Amstrad-CPC-style AY port-decode protocols,
 *    including the mid-stream auto-detect-and-switch (Z80.pas:10986-11289:
 *    CPCInProc/ZXInProc/CPCOutProc/ZXOutProc/InitialOutProc/InitialInProc).
 *  - The once-per-frame maskable-interrupt acceptance window (Z80.pas:
 *    IntLength=32 T-states, Z80.pas:21027-21045).
 *
 * Deliberately not ported here:
 *  - OutZXConverter/OutCPCConverter (Z80.pas:11141-11183) - used only for
 *    Convs.pas's register-write-recording/export mode, not core playback;
 *    belongs to a later milestone (MIG-0010).
 *  - IM0 (Z80.pas's live code never distinguishes it from IM1 - see
 *    MIG-0001); this adapter always requests an IM1/IM2-shaped interrupt.
 */
#ifndef AY_ENGINE_Z80_BUS_H
#define AY_ENGINE_Z80_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "z80.h"

typedef enum {
  Z80_BUS_MACHINE_INITIAL = 0, /* Z80.pas: InitialOutProc/InitialInProc */
  Z80_BUS_MACHINE_ZX = 1,      /* Z80.pas: ZXOutProc/ZXInProc */
  Z80_BUS_MACHINE_CPC = 2      /* Z80.pas: CPCOutProc/CPCInProc */
} z80_bus_machine;

/* Invoked when a CPC-style AY port write is auto-detected mid-stream,
 * replacing Z80.pas:11110-11112's `if AYFileEnableAutoSwitch then
 * FrmMain.Set_Chip_Frq(1000000)` GUI call with a plain callback. NULL is a
 * valid no-op (there is no GUI in this milestone). */
typedef void (*z80_bus_chip_freq_cb)(void* userdata, unsigned long hz);

/* Invoked whenever the bus decides an AY register write should take effect
 * now - mirrors the SoundChip[0].SetAYRegister(reg, data) call sites inside
 * Z80.pas's ZXOutProc/CPCOutProc/InitialOutProc. The caller is expected to
 * flush synthesis up to the current tact (AY.pas: SynthesizerAY) and then
 * apply the write, exactly as the original does inline - this callback
 * fires at the same point in the control flow the original's direct call
 * did, just externalized instead of reaching into a global SoundChip[0]. */
typedef void (*z80_bus_ay_write_cb)(void* userdata, uint8_t reg, uint8_t data);

/* Invoked to read back an AY register for IN, e.g. `IN A,(0xFFFD)` reading
 * the currently-selected register - mirrors ZXInProc/CPCInProc/
 * InitialInProc's `Dat := SoundChip[0].RegisterAY.Index[reg]`. */
typedef uint8_t (*z80_bus_ay_read_cb)(void* userdata, uint8_t reg);

/* Invoked on every port write whose low bit is 0 (the ULA border/beeper/tape
 * port on real Spectrum hardware) - mirrors Z80.pas's inline beeper-edge
 * handling in ZXOutProc/InitialOutProc (`if Prt and 1 = 0 then ... if
 * BeeperNext <> Beeper then ...`). Passed the new beeper level (already
 * resolved to 0 or the caller's beeper-on level) only when it changes,
 * matching the original's edge-triggered SynthesizerAY flush. */
typedef void (*z80_bus_beeper_cb)(void* userdata, int level);

typedef struct z80_bus {
  z80 cpu;
  uint8_t ram[0x10000];

  z80_bus_machine machine;
  bool ay_file_enable_auto_switch; /* Z80.pas: AYFileEnableAutoSwitch */

  /* AY register-select latch, mirrors Z80.pas's
   * SoundChip[0].Current_RegisterAY (formerly the AY_CurReg global). */
  uint8_t ay_cur_reg;

  /* CPC PPI/PSG-select state, mirrors Z80.pas's CPCData/CPCSwitch. */
  uint8_t cpc_data;
  uint8_t cpc_switch;

  /* Beeper edge state, mirrors Z80.pas's Beeper/BeeperNext and the local
   * `Flg` used to avoid a double SynthesizerAY flush within one OUT. */
  int beeper;
  int beeper_on_level; /* caller-supplied "BeeperLevel", AY.pas:1006 */

  /* Frame timing, mirrors Z80.pas's MaxTStates/CurrentTact/IntLength. */
  int64_t current_tact;
  int64_t max_tstates;
  int64_t int_length; /* fixed at 32, Z80.pas:28 */
  bool int_pending_this_frame; /* tracks whether z80_gen_int is still
                                 * "live" within this frame's acceptance
                                 * window - see z80_bus.c for why this is
                                 * needed on top of superzazu/z80's own
                                 * int_pending flag. */

  /* Set by z80_bus_step() before calling z80_step(), consumed by the
   * port_in/port_out trampolines: superzazu/z80's port_in/port_out only
   * pass the low 8 bits of the port (see z80_bus.c's reconstruct_port() for
   * why the full 16-bit port must be reconstructed from z->b or z->a
   * depending on which opcode form triggered the call). */
  bool next_port_op_is_bc_form;

  z80_bus_chip_freq_cb on_chip_freq_change;
  void* chip_freq_userdata;
  z80_bus_ay_write_cb on_ay_write;
  void* ay_write_userdata;
  z80_bus_ay_read_cb on_ay_read;
  void* ay_read_userdata;
  z80_bus_beeper_cb on_beeper_change;
  void* beeper_userdata;
} z80_bus;

void z80_bus_init(z80_bus* bus, int64_t max_tstates);

/* Executes exactly one Z80 instruction, including the once-per-frame
 * interrupt-acceptance check, mirroring Z80.pas's live Z80_Step
 * (Z80.pas:21017-21051). */
void z80_bus_step(z80_bus* bus);

#endif /* AY_ENGINE_Z80_BUS_H */
