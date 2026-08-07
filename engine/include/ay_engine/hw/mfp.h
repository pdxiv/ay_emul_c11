/* C11 port of ay_emul/atari.pas's MFP68901 timer emulation
 * (MFP_Registers/TMFP_DelayTimer/EmulateTimer/SetMFPRegister/mfp_readbyte/
 * mfp_writebyte, atari.pas:111-160,356-489,892-922,1383-1431). Exposed as
 * an engine/m68k_bus.h callback region at $FFFA00-$FFFA2F.
 *
 * Scope for this milestone (see migration_debt.yaml): the 4 delay-mode
 * timers (A-D) that drive SNDH playback timing, and the register
 * read/write semantics real SNDH tunes exercise. Event-count/pulse mode
 * (AER-driven) is not ported - the original itself only implements delay
 * mode (see mfp.c's header comment for the source confirmation).
 *
 * MIG-0051: the ICnt retry mechanism WAS deliberately dropped here as
 * "unnecessary under Musashi's level-triggered model" - that reasoning was
 * wrong, found the hard way once a real SNDH tune (Temple_of_Asherah.sndh)
 * turned out to drive timers A/B/D almost continuously for effects, and
 * played back correctly in some stretches but audibly too fast in others.
 * atari.pas:1466's s68000interrupt(level,vector) tracks a SINGLE, SHARED
 * pending slot PER CPU INTERRUPT LEVEL (confirmed from Starscream's own
 * assembly, Starcpu32.asm) - level 6 is shared by ALL FOUR timers, not
 * one slot per timer. When a timer's request finds that shared slot
 * already occupied by an earlier, not-yet-serviced request (from ANY of
 * the 4 timers), atari.pas:1383-1431's EmulateTimer increments THAT
 * TIMER's own ICnt and retries again on a later call, once per call,
 * until it succeeds - guaranteeing every single timer expiry eventually
 * gets its own separate service. This port's IPR-bit approach was
 * missing the shared-slot concept entirely: OR-ing a timer's own IPR bit
 * is idempotent, so back-to-back expiries arriving faster than the CPU's
 * mask allows them to be serviced were silently coalesced into a single
 * eventual service instead of each retrying until served - undercounting
 * how many times a timer-driven effect (e.g. a digidrum sample step)
 * should have advanced whenever the CPU is masked/busy for a stretch
 * longer than one timer period, which is exactly the situation MIG-0050's
 * reentrant-flush-timing bursts and ordinary VBL-handler-masked sections
 * create. Fixed: mfp.level6_pending now models that single shared slot
 * directly, and mfp_timer.icnt implements the same per-timer retry
 * counter Pascal has. */
#ifndef AY_ENGINE_MFP_H
#define AY_ENGINE_MFP_H

#include <stdbool.h>
#include <stdint.h>

#include "ay_engine/hw/m68k_bus.h"

typedef struct {
  bool ie, im;   /* interrupt enable / mask, cached from IERx/IMRx */
  uint8_t v;     /* vector-offset field: A=13,B=8,C=5,D=4 */
  uint8_t dm;    /* prescaler mode, 0 (stopped) .. 7 */
  int64_t dr;    /* expanded data register value, 1..256 */
  int64_t cnt;   /* live countdown, recomputed on read - see mfp_read_byte */
  int64_t base;  /* cycle-domain origin of the current countdown */
  int64_t delay; /* cycle-domain length of the current countdown */
  int ipx_index; /* register index of this timer's IPR byte (IPA=5/IPB=6) */
  int isx_index; /* register index of this timer's ISR byte (ISA=7/ISB=8) */
  int txd_index; /* register index of this timer's data reg (TAD..TDD, 15-18) */
  uint8_t ipsb;  /* this timer's pending/in-service bit within IPx/ISx */
  int icnt;      /* atari.pas: MFP_DT.ICnt - MIG-0051, see mfp's file
                  * comment: this timer's own backlog of level-6 requests
                  * that found the SHARED CPU-level slot already occupied,
                  * retried one at a time as that slot frees up. */
} mfp_timer;

typedef struct mfp {
  uint8_t reg[24]; /* MFP_Registers.Index, $FFFA00-based, one byte/register */
  mfp_timer timers[4]; /* 0=A,1=B,2=C,3=D */

  /* atari.pas: the single bit of Starscream's shared ctx(interrupts)
   * bitfield covering CPU interrupt level 6 - MIG-0051. Set when any
   * timer's request succeeds (mirrors s68000interrupt(6,...)=0), cleared
   * by mfp_ack_interrupt once Musashi actually dispatches level 6
   * (mirrors Starscream's flush_interrupts clearing the same bit on
   * dispatch). While set, a NEW timer expiry's own request fails
   * (mirrors s68000interrupt(6,...)<>0) and increments that timer's icnt
   * instead, retried on a later call - see mfp_timer.icnt. */
  bool level6_pending;
  int pending_vector; /* atari.pas: Starscream's __interrupts[6] vector
                       * storage (Starcpu32.asm's s68000interrupt: `mov
                       * [__interrupts+xAX],dl` at request time, `mov
                       * dl,[__interrupts+xDX]` at dispatch time) - the
                       * vector is latched HERE, at request time, not
                       * re-derived later from IPR bits (which
                       * mfp_emulate_timer's request-success path may
                       * already have cleared by the time
                       * mfp_ack_interrupt runs). */
} mfp;

void mfp_init(mfp* m);

/* Callback-region handlers matching mfp_readbyte/mfp_writebyte's address
 * decode ((address-$FFFA01) div 2, i.e. only odd byte lanes respond). `od`
 * is the current cycle count (matching s68000readOdometer's role) - needed
 * for live-countdown reads and timer-mode/data-register writes, passed in
 * explicitly rather than read from a global, matching engine/ay.c's
 * ay_synthesizer_ay(engine, current_tact) convention. */
uint8_t mfp_read_byte_at(mfp* m, uint32_t address, int64_t od);
/* Returns true if atari.pas's SetMFPRegister would have called
 * s68000releaseTimeslice for this register (docs/mfp_reference.md
 * cross-check, MIG-0054): IEA/IEB/IMA/IMB/TAC/TBC/TDC/TAD/TBD/TCD/TDD -
 * every register that can change timer/interrupt scheduling state, NOT
 * PDR/AER/DIR/IPA/IPB/ISA/ISB/VCR/USART. The caller (atari_emulate.c)
 * must call m68k_bus_end_timeslice() when this returns true - matching
 * atari.pas ending the current s68000exec burst immediately so
 * Atari_Emulate re-evaluates all four timers' scheduling on FRESH state
 * right away, instead of continuing to run on a stale `min` budget
 * computed before this write reconfigured a timer. Without this, a
 * timer's own base/delay bookkeeping can fall far behind the CPU's
 * odometer before the scheduler ever notices the reconfiguration -
 * exactly the large od-vs-base gap that seeds an MFP ICnt backlog. */
bool mfp_write_byte_at(mfp* m, uint32_t address, uint8_t value, int64_t od);

/* Bundles what mfp_bus_read/mfp_bus_write need beyond the mfp struct
 * itself, so this module's m68k_bus_add_callback_region adapter can live
 * here instead of in atari_emulate.c, without mfp.h/mfp.c depending on
 * atari_emulate.h (which itself depends on mfp.h - m68k_bus.h is the
 * lower-level, sibling module both build on). `od` is a pointer to the
 * CALLER's own current-cycle-count field (e.g. atari_emulate's
 * cycle_count) - read fresh on every call, not copied in, since it
 * changes between calls. `bus` is used only by mfp_bus_write, to call
 * m68k_bus_end_timeslice() on scheduling-relevant register writes (see
 * mfp_write_byte_at's doc comment, MIG-0054). */
typedef struct {
  mfp* m;
  const int64_t* od;
  m68k_bus* bus;
} mfp_bus_context;

/* Directly registrable via m68k_bus_add_callback_region - userdata must
 * point to a mfp_bus_context. */
uint8_t mfp_bus_read(void* userdata, uint32_t address);
void mfp_bus_write(void* userdata, uint32_t address, uint8_t value);

/* Advances timer `idx` (0=A..3=D) to cycle count `od` (matching
 * Starcpu.inc's s68000readOdometer / m68k_bus's accumulated cycle count),
 * mirroring EmulateTimer's countdown-expiry-and-reload logic, the shared-
 * level-6-slot request/retry (mfp.level6_pending/mfp_timer.icnt - MIG-0051)
 * and IPR-bit side effects. Returns cycles until that timer's next event,
 * or -1 if the timer is stopped (DM=0). */
int64_t mfp_emulate_timer(mfp* m, int idx, int64_t od);

/* True if the MFP's single interrupt line (fixed 68000 IPL 6) should
 * currently be asserted - MIG-0051: this is now simply level6_pending,
 * the shared CPU-level slot state, matching what actually drives interrupt
 * delivery timing (NOT a per-timer IPR-bit OR-scan, which conflated the
 * MFP chip's own per-source bookkeeping with the separate CPU-level
 * queueing state). */
bool mfp_irq_pending(const mfp* m);

/* Call from the m68k_bus int_ack callback once the CPU accepts level 6.
 * Clears level6_pending (MIG-0051 - freeing the shared slot for the next
 * request to succeed) and returns the vector (MFP_VCR high nibble |
 * timer's V field) for whichever pending source has priority - a
 * documented, simplified fixed A>B>C>D order (see migration_debt.yaml;
 * the original doesn't have this ambiguity since each timer pushes its
 * own vector directly). Does NOT clear any timer's IPR/ISR bits itself
 * any more - MIG-0051 moved that to mfp_emulate_timer's own request-
 * success path, matching atari.pas's exact timing (the original clears
 * them immediately upon a successful s68000interrupt call, not upon
 * actual CPU dispatch, which is a real, if subtle, difference for a fast
 * timer where many calls can elapse between the two). Returns -1 if
 * nothing is actually pending. */
int mfp_ack_interrupt(mfp* m);

#endif /* AY_ENGINE_MFP_H */
