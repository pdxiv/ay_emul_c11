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

#include "ay_engine/ay.h"
#include "ay_engine/dma_sound.h"
#include "ay_engine/m68k_bus.h"
#include "ay_engine/mfp.h"

typedef struct atari_emulate {
  m68k_bus bus;
  mfp mfp;
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

  bool vbl_acked_this_exec; /* internal, set by the int_ack trampoline */
} atari_emulate;

/* mem/mem_size/ay are borrowed references, not copied - caller owns their
 * lifetime and must keep them valid for as long as `a` is used. `ay` must
 * already be ay_engine_init'd; this function wires its on_mix_dma hook to
 * `a`'s dma_sound. mc68000_freq/vbl_period/ay_freq are computed by the
 * caller from settings.pas-equivalent constants (see MainWin.pas:1580-1586
 * for the original's derivation: MC68000Freq = MainClockFreq/4, VBLPeriod
 * = round(MC68000Freq/VBLFreq)). */
void atari_emulate_init(atari_emulate* a, uint8_t* mem, uint32_t mem_size,
                        ay_engine* ay, double mc68000_freq,
                        int64_t vbl_period, double ay_freq);

/* Mirrors Atari_Emulate (atari.pas:1436-1523): computes the bounded cycle
 * budget from the nearest upcoming VBL/MFP-timer/DMA-sample-boundary
 * event, executes exactly that many 68000 cycles, injects the VBL
 * interrupt on schedule, and flushes the AY mixer (via
 * ay_synthesizer_stereo16/mono, engine/ay.h - the SynthesizerSNDH
 * equivalent) if DMA sound is active. Call in a loop, checking
 * a->real_end_all, the same shape MakeBufferSNDH will use once it exists
 * (Players.pas/Phase 2). */
void atari_emulate_step(atari_emulate* a);

#endif /* AY_ENGINE_ATARI_EMULATE_H */
