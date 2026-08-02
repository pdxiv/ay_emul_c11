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
 * Interrupt delivery model differs deliberately from the original's:
 * Starscream's s68000interrupt(level,vector) is a synchronous "push this
 * exact vector now, tell me if the CPU took it" call, retried via each
 * timer's own ICnt counter on failure. Musashi (engine/m68k_bus.h) instead
 * models real 68000 interrupt hardware: level-triggered IPL lines
 * (m68k_bus's int_ack callback), which the CPU services on its own at the
 * next eligible instruction boundary - no manual retry loop is needed or
 * possible. This port keeps the OBSERVABLE behavior (IPR/ISR bits, vector
 * value, automatic-EOI clearing) but drops the ICnt retry-counter
 * mechanism as unnecessary under the level-triggered model - see
 * migration_debt.yaml for the recorded rationale. */
#ifndef AY_ENGINE_MFP_H
#define AY_ENGINE_MFP_H

#include <stdbool.h>
#include <stdint.h>

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
} mfp_timer;

typedef struct mfp {
  uint8_t reg[24]; /* MFP_Registers.Index, $FFFA00-based, one byte/register */
  mfp_timer timers[4]; /* 0=A,1=B,2=C,3=D */
} mfp;

void mfp_init(mfp* m);

/* Callback-region handlers matching mfp_readbyte/mfp_writebyte's address
 * decode ((address-$FFFA01) div 2, i.e. only odd byte lanes respond). `od`
 * is the current cycle count (matching s68000readOdometer's role) - needed
 * for live-countdown reads and timer-mode/data-register writes, passed in
 * explicitly rather than read from a global, matching engine/ay.c's
 * ay_synthesizer_ay(engine, current_tact) convention. */
uint8_t mfp_read_byte_at(mfp* m, uint32_t address, int64_t od);
void mfp_write_byte_at(mfp* m, uint32_t address, uint8_t value, int64_t od);

/* Advances timer `idx` (0=A..3=D) to cycle count `od` (matching
 * Starcpu.inc's s68000readOdometer / m68k_bus's accumulated cycle count),
 * mirroring EmulateTimer's countdown-expiry-and-reload logic and IPR-bit
 * side effects (see mfp.h's file comment for what's intentionally
 * different about interrupt delivery). Returns cycles until that timer's
 * next event, or -1 if the timer is stopped (DM=0). */
int64_t mfp_emulate_timer(mfp* m, int idx, int64_t od);

/* True if the MFP's single interrupt line (fixed 68000 IPL 6) should
 * currently be asserted - any IE+IM-enabled timer with its IPR bit set. */
bool mfp_irq_pending(const mfp* m);

/* Call from the m68k_bus int_ack callback once the CPU accepts level 6.
 * Returns the vector (MFP_VCR high nibble | timer's V field) for whichever
 * pending source has priority - a documented, simplified fixed A>B>C>D
 * order (see migration_debt.yaml; the original doesn't have this ambiguity
 * since each timer pushes its own vector directly) - and performs the
 * automatic-EOI IPR/ISR clear if VCR bit 3 is clear (matches the
 * original's automatic end-of-interrupt mode). Returns -1 if nothing is
 * actually pending. */
int mfp_ack_interrupt(mfp* m);

#endif /* AY_ENGINE_MFP_H */
