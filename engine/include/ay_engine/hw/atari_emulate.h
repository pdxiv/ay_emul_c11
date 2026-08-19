/* C11 port of ay_emul/atari.pas's Atari_Emulate scheduling loop
 * (atari.pas:1436-1523) - ties engine/m68k_bus.h (Musashi), engine/mfp.h,
 * engine/dma_sound.h, and the already-ported engine/ay.h together the way
 * the original ties Starscream/MFP/DMA-sound/AY.pas together.
 *
 * Scope for this milestone (see migration_debt.yaml): the scheduling logic
 * itself (VBL/MFP/DMA cycle-budget clamping, interrupt injection, the
 * SynthesizerSNDH-equivalent dispatch into engine/ay.h's already-ported
 * mixer) - proven correct via synthetic 68000 programs the same bounded
 * way every core so far has been. NOT wired to a real render-loop caller
 * (MakeBufferSNDH) or SNDH file loading - that's Players.pas/Phase 2.
 *
 * Interrupt model: Starcpu.inc's s68000interrupt(level,vector) is a
 * synchronous "push this vector now, tell me if taken" call; Musashi
 * (engine/m68k_bus.h) models real level-triggered IPL lines instead. This
 * port asserts IPL4 (VBL) or IPL6 (MFP) as appropriate before each
 * m68k_bus_exec call and tracks whether the CPU actually served level 4
 * during that call via the int_ack hook, closely matching the original's
 * accept-or-retry-with-slack behavior for VBL. MFP's own IPR-bit-clearing
 * (engine/mfp.h's mfp_ack_interrupt) naturally de-asserts IPL6 once
 * serviced, matching real MFP hardware - no separate retry bookkeeping
 * needed there (see engine/mfp.h's file comment for the fuller rationale
 * on why the original's per-timer ICnt retry counter isn't replicated). */
#ifndef AY_ENGINE_ATARI_EMULATE_H
#define AY_ENGINE_ATARI_EMULATE_H

#include <stdbool.h>
#include <stdint.h>

#include "ay_engine/hw/ay.h"
#include "ay_engine/hw/dma_sound.h"
#include "ay_engine/hw/m68k_bus.h"
#include "ay_engine/hw/mfp.h"

typedef struct atari_emulate {
  m68k_bus bus;
  mfp mfp;
  mfp_bus_context mfp_ctx; /* wires mfp_bus_read/mfp_bus_write directly
                            * into the $FFFA00-$FFFA2F callback region -
                            * set up once in atari_emulate_init, see
                            * mfp.h's mfp_bus_context comment (MIG-0054,
                            * moving the MFP<->bus adapter out of this
                            * file and into mfp.c/mfp.h for modularity). */
  dma_sound dma;
  ay_engine* ay; /* not owned; caller must ay_engine_init it first */

  uint8_t* mem; /* flat 68000 RAM, not owned; plain big-endian bytes */
  uint32_t mem_size;

  uint8_t ym_cur_reg; /* atari.pas: SoundChip[0].Current_RegisterAY, the
                       * $FF8800-range register-select latch */

  int64_t cycle_count; /* matches s68000readOdometer's role; a plain 64-bit
                        * accumulator, no renormalization needed - see
                        * migration_debt.yaml for why that's a documented,
                        * intentional simplification vs the original's
                        * 32-bit-Starscream-only tripOdometer mechanism. */
  int64_t base_vbl;    /* atari.pas: BaseVBL */
  int64_t vbl_period;  /* atari.pas: VBLPeriod = round(MC68000Freq/VBLFreq) */
  int64_t tick_count, tick_count_max; /* atari.pas: TickCount/TickCountMax */
  bool do_loop;      /* settings.pas: Do_Loop */
  bool real_end_all; /* Players.pas/AY.pas: Real_End_All */

  double mc68000_freq; /* atari.pas: MC68000Freq */
  double ay_freq;      /* atari.pas's OWN AyFreq global (Atari_SetDefault:
                        * Atari_MainClockFreqDef/16 = 2000000.0, the Atari
                        * ST's AY/YM clock) - NOT settings.pas's AY_Freq
                        * (the ZX Spectrum's 1773400), a different global
                        * despite the near-identical name; confirmed the
                        * hard way via an oracle-diff mismatch while
                        * building this milestone's "dma" scenario (see
                        * migration_debt.yaml). DMA sound's mixer-step
                        * divisor, see dma_sound_mix. */

  /* atari.pas: VBLFreq - PER-FILE (SNDH's own PlayFreq tag,
   * sndh_file.c's `vbl_freq = tags.play_freq`), previously only a local
   * variable inside sndh_file_load and never persisted here. Stored so
   * atari_emulate_set_mc68000_freq (MIG-0120, Mixer.pas's GBMCFrq
   * override) can recompute vbl_period (atari.pas:1176's `round(
   * MC68000Freq/VBLFreq)`) later without needing the file's tags
   * re-derived from scratch. */
  double vbl_freq;

  /* Mixer.pas: GBMFPFrq's own MFPTimerMode/MFPTimerFrq (MIG-0120) -
   * mode 0 = Auto (MFPTimerFrq recomputed from the loaded file's AY
   * clock, MainWin.pas:1570's `Trunc(AY_Freq*16/13+0.5)`), mode 1 =
   * manual override (MainWin.pas:1573-1577). Purely informational/
   * display bookkeeping here - the actual effect on playback is entirely
   * via mfp.mc_by_mfp, recomputed by atari_emulate_set_mfp_freq/
   * atari_emulate_set_mc68000_freq whenever either the MFP frequency or
   * the 68000 clock changes (MainWin.pas:1581,1645's `MCbyMFP :=
   * MC68000Freq/MFPFreq`). */
  int mfp_timer_mode;
  double mfp_timer_freq; /* atari.pas: MFPFreq (Hz) */

  /* Mixer.pas: GBSNDH's STRB/STeRB radio pair (MIG-0121) - true for an
   * Atari STe (default, preserving this port's pre-existing behavior
   * exactly - see atari_emulate_init's own comment), false for a plain
   * Atari ST, which has NO DMA-sound hardware at all (atari.pas:1078-
   * 1110's repeated `if STe then SetDataRegion(...)` for the $FF8900-
   * $FF89FF region - a plain ST never maps it). Gates the DMA-sound
   * callback-region registration in atari_emulate_init; read nowhere
   * else (dma_sound.c's own mixing code is unconditional once the
   * region exists to drive it, matching the original). */
  bool is_ste;

  bool vbl_acked_this_exec; /* internal, set by the int_ack trampoline */

  /* atari.pas: bit 4 of Starscream's ctx(interrupts) pending-request
   * bitfield (see s68000interrupt's real assembly semantics, Starcpu32.asm)
   * - MIG-0046 discovered this the hard way: s68000interrupt(level,vector)
   * does NOT check the CPU's interrupt mask at all (that decision is made
   * later, inside exec()'s own dispatch) - it just sets a "this level is
   * now pending" bit and returns 0, UNLESS that bit was ALREADY set from
   * an earlier, not-yet-serviced request (returns 1/failure in that case).
   * BaseVBL/TickCount advance whenever a *new* pending request is
   * successfully raised (0 returned) OR the 10%-slack fallback trips -
   * NOT based on whether the CPU's mask would currently accept it. This
   * flag mirrors that pending bit: set here when a fresh VBL request is
   * raised, cleared by the int_ack trampoline once Musashi actually
   * services it (matching Starscream's own bit-clear-on-dispatch). */
  bool vbl_irq_pending;

  /* MIG-0053: true if int_ack fired (either level) during the PREVIOUS
   * step's exec() call - used to decide whether THIS step needs the min=1
   * dispatch-guarantee cap (see atari_emulate_step's file comment on why
   * Musashi's STOP handling needs it) or can run at full speed. Forcing
   * min=1 on every step while anything is pending (the original MIG-0053
   * fix) over-corrected: it re-evaluates all four MFP timers' icnt-retry
   * logic once per single INSTRUCTION for a handler's entire body, not
   * just once to guarantee its OWN dispatch, giving backlogged retries far
   * more chances per unit of real CPU work to grab the shared level-6 slot
   * than Starscream's natural once-per-outer-call cadence allows - audible
   * as extra, premature register writes ("jitter"). Capping min=1 only for
   * the step that still needs to dispatch something (nothing was just
   * acked) while letting the step right after an ack run at full budget
   * keeps prompt dispatch without that extra granularity. */
  bool irq_acked_prev_exec;

  /* atari.pas's AYOuts linked list (AddOut/ClearOuts/Atari_CheckOuts,
   * atari.pas:517-543,1574-1596) - MIG-0050: soundchip_writebyte calls
   * SynthesizerSNDH REENTRANTLY, mid-instruction-stream, on every AY
   * data-register write, flushing pending audio samples up to that exact
   * cycle. If the caller's output buffer is already full at that point (or
   * becomes full as a result of that flush), the write itself is deferred
   * onto this queue instead of applied immediately, and replayed at the
   * top of the NEXT render call (Atari_CheckOuts) before more CPU
   * execution resumes - real 68000 programs can and do write several AY
   * registers in a tight burst right at a buffer boundary. Fixed-size
   * (unlike Pascal's unbounded linked list): 14 registers, and a real
   * SNDH replayer writes each register at most a handful of times per
   * VBL, so this comfortably covers any realistic burst; writes beyond
   * the limit are dropped (silently, matching "ran out of AYOuts nodes"
   * never being a real original-code failure path either). */
  struct {
    uint8_t reg;
    uint8_t data;
  } pending_writes[32];
  int pending_write_count;
} atari_emulate;

/* mem/mem_size/ay are borrowed references, not copied - caller owns their
 * lifetime and must keep them valid for as long as `a` is used. `ay` must
 * already be ay_engine_init'd; this function wires its on_mix_dma hook to
 * `a`'s dma_sound. mc68000_freq/vbl_period/ay_freq are computed by the
 * caller from settings.pas-equivalent constants (see MainWin.pas:1580-1586
 * for the original's derivation: MC68000Freq = MainClockFreq/4, VBLPeriod
 * = round(MC68000Freq/VBLFreq)). vbl_freq is the same VBLFreq used to
 * derive vbl_period, stored verbatim for a later atari_emulate_set_
 * mc68000_freq call to reuse (MIG-0120, see vbl_freq's own struct
 * comment). is_ste selects whether the DMA-sound ($FF8900-$FF89FF)
 * region is mapped at all (MIG-0121, see is_ste's own struct comment) -
 * every existing caller before this parameter existed behaved as if
 * is_ste were true, so pass true to preserve exact prior behavior. mfp_
 * timer_mode/mfp_timer_freq are seeded to Mixer.pas's own MFPTimerModeDef/
 * an MFPFreq consistent with mfp_init's own MC_BY_MFP_DEFAULT seed
 * (mc68000_freq * 4.0/13.0) - i.e. "Auto" in name only until a caller
 * explicitly invokes atari_emulate_set_mfp_freq; mfp.mc_by_mfp itself is
 * untouched by this function (mfp_init already seeded it), so default
 * playback timing is bit-for-bit unchanged from before mc_by_mfp existed
 * as a runtime field. */
void atari_emulate_init(atari_emulate* a, uint8_t* mem, uint32_t mem_size,
                        ay_engine* ay, double mc68000_freq,
                        int64_t vbl_period, double ay_freq, double vbl_freq,
                        bool is_ste);

/* MainWin.pas: Set_MC68K_Frq (1636-1650) - a LOAD-TIME-ONLY 68000 clock
 * override (MIG-0120; matches this port's own established convention for
 * player_set_chip_freq/player_set_player_freq/player_set_number_of_
 * channels of applying overrides once, right after a fresh load, not
 * live mid-playback - see player_set_number_of_channels's own doc
 * comment in player.h for the fuller rationale). Silently ignored if
 * `freq` is outside MainWin.pas:1638's own [2000000, 16000000] guard
 * range. Recomputes vbl_period (atari.pas:1176, using the vbl_freq
 * stored at atari_emulate_init time), ay->frq_ay_by_frq_z80 (atari.pas's
 * own FrqAyByFrqMC68000, `round(AyFreq/MC68000Freq/8*4294967296)`), and
 * mfp.mc_by_mfp (`MC68000Freq/MFPFreq`, MainWin.pas:1645) from the
 * CURRENTLY effective mfp_timer_freq - mirrors Set_MC68K_Frq's own three
 * assignments exactly, in the same order. */
void atari_emulate_set_mc68000_freq(atari_emulate* a, double freq);

/* MainWin.pas: Set_MFP_Frq (1563-1585) - a LOAD-TIME-ONLY MFP timer
 * frequency override (MIG-0120, same load-time-only rationale as
 * atari_emulate_set_mc68000_freq above). `mode` 0 = Auto: `freq_hz` is
 * ignored by this function (the caller - player_set_mfp_freq - computes
 * the Auto value itself, `Trunc(AY_Freq*16/13+0.5)` via player_get_
 * ay_freq, and passes the already-computed result here either way, so
 * this function's own job is identical for both modes: just apply
 * whatever frequency the caller decided on). `mode` 1 = manual: `freq_hz`
 * must already be in MainWin.pas:1573's own [1000000, 4365292] range -
 * out-of-range values, or any `mode` other than 0/1, leave mfp_timer_
 * mode/mfp_timer_freq/mfp.mc_by_mfp completely untouched (matching
 * Set_MFP_Frq's own if/else-if chain, which likewise does nothing at all
 * outside those two cases). Recomputes mfp.mc_by_mfp = mc68000_freq/
 * freq_hz (MainWin.pas:1581's `MCbyMFP := MC68000Freq/MFPFreq`). */
void atari_emulate_set_mfp_freq(atari_emulate* a, int mode, double freq_hz);

/* Mirrors Atari_Emulate (atari.pas:1436-1523): computes the bounded cycle
 * budget from the nearest upcoming VBL/MFP-timer/DMA-sample-boundary
 * event, executes exactly that many 68000 cycles, injects the VBL
 * interrupt on schedule, and flushes the AY mixer (via
 * ay_synthesizer_stereo16/mono, engine/ay.h - the SynthesizerSNDH
 * equivalent) if DMA sound is active. Call in a loop, checking
 * a->real_end_all, the same shape MakeBufferSNDH will use once it exists
 * (Players.pas/Phase 2). */
void atari_emulate_step(atari_emulate* a);

/* MIG-0056, EXPERIMENTAL / OPT-IN, explicit user directive: thin wrapper
 * around m68k_bus_enable_starscream_timing_override - see m68k_bus.h for
 * the full mechanism/scope/policy-history comment. Off by default; call
 * once after atari_emulate_init to enable. */
void atari_emulate_enable_starscream_timing(atari_emulate* a, bool enable);

/* atari.pas: Atari_CheckOuts (1574-1596), the AY-register half only (the
 * DMA-sound IntDMASnd branch has no equivalent here yet - no in-scope test
 * file uses DMA sound, see migration_debt.yaml). Applies any writes
 * deferred by a reentrant mid-exec SynthesizerSNDH-equivalent flush (see
 * atari_emulate.h's pending_writes comment) directly to the AY chip, then
 * clears the queue. Callers (e.g. sndh_file_make_buffer) must call this
 * once at the top of each render call, before resuming CPU execution,
 * mirroring MakeBufferSNDH's own call ordering exactly (after the top-of-
 * function IntFlag check, before the main Atari_Emulate loop) - MIG-0050. */
void atari_emulate_flush_pending_writes(atari_emulate* a);

#endif /* AY_ENGINE_ATARI_EMULATE_H */
