/* C11 port of ay_emul/atari.pas's Atari_Emulate scheduling loop - see
 * engine/include/ay_engine/atari_emulate.h for the ported contract and
 * scope. */
#include "ay_engine/hw/atari_emulate.h"

#include <stdlib.h>
#include <string.h>

#include "ay_engine/util/trace_log.h"

/* MFP callback region ($FFFA00-$FFFA2F): mfp_bus_read/mfp_bus_write
 * (engine/mfp.c) are registered directly below, via a->mfp_ctx - see
 * mfp.h's mfp_bus_context comment (MIG-0054) for why the adapter (address
 * decode, live-countdown/timer-mode `od` threading, and the
 * scheduling-write timeslice-release fix) lives in mfp.c/mfp.h now
 * instead of as wrapper functions here. */

/* ---- DMA sound callback region ($FF8900-$FF89FF) ---- */

static uint8_t dma_region_read(void* userdata, uint32_t address) {
  atari_emulate* a = (atari_emulate*)userdata;
  return dma_sound_read_counter_byte_at(&a->dma, address, a->mc68000_freq,
                                        a->cycle_count);
}
static void dma_region_write(void* userdata, uint32_t address,
                              uint8_t value) {
  atari_emulate* a = (atari_emulate*)userdata;
  dma_sound_write_byte_at(&a->dma, address, value, a->cycle_count);
}

/* ---- YM2149 callback region ($FF8800-$FF88FF), atari.pas:520-558 ---- */

static uint8_t ym_region_read(void* userdata, uint32_t address) {
  atari_emulate* a = (atari_emulate*)userdata;
  if ((address & 3) == 0 && a->ym_cur_reg < 16) {
    return a->ay->chip.reg[a->ym_cur_reg];
  }
  return 0;
}
static void queue_write(atari_emulate* a, uint8_t reg, uint8_t data) {
  int n = (int)(sizeof(a->pending_writes) / sizeof(a->pending_writes[0]));
  if (a->pending_write_count < n) {
    a->pending_writes[a->pending_write_count].reg = reg;
    a->pending_writes[a->pending_write_count].data = data;
    a->pending_write_count++;
  }
}

static void ym_region_write(void* userdata, uint32_t address,
                             uint8_t value) {
  atari_emulate* a = (atari_emulate*)userdata;
  switch (address & 3) {
    case 0:
      a->ym_cur_reg = value;
      break;
    case 2:
      /* atari.pas:538-556 (soundchip_writebyte's data-write branch) - a
       * reentrant, mid-instruction-stream SynthesizerSNDH flush plus a
       * BuffLen/IntFlag-driven deferred-write queue - MIG-0050. Previously
       * "out of scope" (apply the write immediately, let a future render-
       * loop caller own flush timing), which turned out to be the actual
       * cause of a stubborn WAV-comparison divergence that persisted even
       * after MIG-0046 through MIG-0049 closed the underlying VBL/cycle-
       * accounting gap almost exactly: both implementations were silent
       * well past the divergence point in every trace taken, meaning the
       * difference could only be in exactly how many samples get
       * generated per unit of elapsed cycle time - which is controlled by
       * WHEN SynthesizerSNDH gets called relative to CPU execution, not by
       * VBL scheduling. Real 68000 programs can and do write several AY
       * registers in a tight burst right at a render-buffer boundary, so
       * this reentrant flush (and the deferred queue for writes that land
       * after the buffer's already full) matters for byte-exact output,
       * not just for "is audio present at all". */
      {
        int64_t live_cycle_count =
            a->cycle_count + (int64_t)m68k_bus_cycles_run();
        trace_log_ay(live_cycle_count, "write_req", a->ym_cur_reg, value);
        if (a->ym_cur_reg < 14) {
          if (a->ay->buf_len >= a->ay->buffer_length) {
            queue_write(a, a->ym_cur_reg, value);
          } else {
            ay_synthesizer_ay(a->ay, live_cycle_count);
            if (a->ay->buf_len >= a->ay->buffer_length) {
              m68k_bus_end_timeslice();
            }
            if (!a->ay->int_flag) {
              ay_chip_set_ay_register(&a->ay->chip, a->ym_cur_reg, value);
              trace_log_ay(live_cycle_count, "write_apply", a->ym_cur_reg,
                            value);
            } else {
              queue_write(a, a->ym_cur_reg, value);
            }
          }
        } else {
          ay_chip_set_ay_register(&a->ay->chip, a->ym_cur_reg, value);
          trace_log_ay(live_cycle_count, "write_apply", a->ym_cur_reg, value);
        }
      }
      break;
    default:
      break;
  }
}

/* ---- DMA-sound mixer hook, wired into ay_engine.on_mix_dma ---- */

static void on_mix_dma(void* userdata, int* lev_l, int* lev_r) {
  atari_emulate* a = (atari_emulate*)userdata;
  dma_sound_mix(&a->dma, a->mem, a->mem_size, a->ay->atari_dma_level,
                a->mc68000_freq, a->ay_freq, lev_l, lev_r);
}

/* ---- Interrupt acknowledge ---- */

/* MIG-0052: real 68000 IPL priority is BY LEVEL NUMBER (7=highest/NMI,
 * 1=lowest) - MFP's level 6 outranks VBL's level 4 - shared by
 * atari_emulate_step (picking what to assert BEFORE the next exec burst)
 * and int_ack (picking what to leave asserted AFTER acknowledging one -
 * see int_ack's comment for why this second call site is required). */
static int compute_irq_level(atari_emulate* a) {
  if (mfp_irq_pending(&a->mfp)) return 6;
  if (a->vbl_irq_pending) return 4;
  return 0;
}

static int int_ack(void* userdata, int level) {
  atari_emulate* a = (atari_emulate*)userdata;
  int64_t live_cycle_count = a->cycle_count + (int64_t)m68k_bus_cycles_run();
  int vector = -1;
  a->irq_acked_prev_exec = true; /* MIG-0053 - see atari_emulate.h */
  if (level == 4) {
    a->vbl_acked_this_exec = true;
    a->vbl_irq_pending = false; /* Starscream clears ctx(interrupts)'s bit
                                 * 4 on dispatch (flush_interrupts) - see
                                 * atari_emulate.h's vbl_irq_pending
                                 * comment, MIG-0046. */
    vector = -1; /* autovector, matches s68000interrupt(4,-1) */
    trace_log_irq(live_cycle_count, "service", 4, -1);
  } else if (level == 6) {
    vector = mfp_ack_interrupt(&a->mfp);
    trace_log_irq(live_cycle_count, "service", 6, vector);
  }
  /* MIG-0052: engine/third_party/musashi/m68kcpu.h's m68ki_exception_
   * interrupt does NOT auto-clear CPU_INT_LEVEL once a custom int_ack
   * callback is registered ("Automatically clear IRQ if we are not using
   * an acknowledge scheme" - the #if guarding that clear is #if
   * !M68K_EMULATE_INT_ACK) - lowering the IRQ line after acknowledging is
   * explicitly the CALLER's job, mirroring real hardware where the
   * interrupting device itself deasserts its line. atari_emulate_step
   * only calls m68k_bus_set_irq once, at the TOP of each step, from
   * state captured BEFORE that step's exec() runs - so previously,
   * once the in-flight exec() call's instruction stream executed RTE
   * and the CPU's own interrupt mask dropped back below 6, Musashi's
   * m68ki_check_interrupts saw CPU_INT_LEVEL still latched at 6 (nothing
   * had told it otherwise) and re-fired the SAME interrupt immediately,
   * over and over, until the NEXT atari_emulate_step call happened to
   * reassess and lower it - confirmed via the new engine/trace_log.c
   * tooling (MIG-0051/MIG-0052 investigation): a 30-second render of
   * Temple_of_Asherah.sndh showed 1,886,081 level-6 "service" events for
   * only 133,420 real "assert" events, and MFP's own request/coalesce
   * bookkeeping saw 258,198 coalesced requests against the oracle's 321,
   * because level6_pending was being asserted-then-immediately-spurious-
   * ly-re-serviced far more often than the underlying MFP condition ever
   * actually recurred. Fixed by recomputing and re-asserting the
   * genuinely-still-pending level immediately after every acknowledge,
   * not just once per outer step. */
  m68k_bus_set_irq(&a->bus, compute_irq_level(a));
  return vector;
}

void atari_emulate_init(atari_emulate* a, uint8_t* mem, uint32_t mem_size,
                        ay_engine* ay, double mc68000_freq,
                        int64_t vbl_period, double ay_freq, double vbl_freq,
                        bool is_ste) {
  memset(a, 0, sizeof(*a));
  a->mem = mem;
  a->mem_size = mem_size;
  a->ay = ay;
  a->mc68000_freq = mc68000_freq;
  a->vbl_period = vbl_period;
  a->ay_freq = ay_freq;
  a->vbl_freq = vbl_freq;
  a->is_ste = is_ste;
  /* MIG-0120: seeds mfp_timer_mode/mfp_timer_freq consistently with
   * mfp_init's own MC_BY_MFP_DEFAULT (13.0/4.0) seed for mfp.mc_by_mfp -
   * see atari_emulate_init's own header comment. */
  a->mfp_timer_mode = 0;
  a->mfp_timer_freq = mc68000_freq * 4.0 / 13.0;
  a->tick_count_max = 1; /* caller overrides for real playback; a nonzero
                          * default avoids an immediate real_end_all on the
                          * very first VBL for standalone/test use. */

  mfp_init(&a->mfp);
  a->mfp_ctx.m = &a->mfp;
  a->mfp_ctx.od = &a->cycle_count;
  a->mfp_ctx.bus = &a->bus;
  dma_sound_init(&a->dma);

  m68k_bus_init(&a->bus);
  m68k_bus_add_flat_region(&a->bus, 0, mem_size - 1, mem);
  m68k_bus_add_callback_region(&a->bus, 0xFFFA00, 0xFFFA2F, mfp_bus_read,
                                mfp_bus_write, &a->mfp_ctx);
  /* atari.pas:1078-1110 - a plain Atari ST has no DMA-sound hardware at
   * all; the $FF8900-$FF89FF region is only mapped for an STe (MIG-0121,
   * see is_ste's own struct comment). */
  if (a->is_ste) {
    m68k_bus_add_callback_region(&a->bus, 0xFF8900, 0xFF89FF, dma_region_read,
                                  dma_region_write, a);
  }
  m68k_bus_add_callback_region(&a->bus, 0xFF8800, 0xFF88FF, ym_region_read,
                                ym_region_write, a);
  a->bus.int_ack = int_ack;
  a->bus.int_ack_userdata = a;

  ay->on_mix_dma = on_mix_dma;
  ay->on_mix_dma_userdata = a;

  m68k_bus_activate(&a->bus);
  m68k_bus_reset(&a->bus);

  /* MIG-0056: opt-in, gated the same way as engine/trace_log.c's env vars -
   * off unless explicitly requested. */
  if (getenv("AY_ENGINE_STARSCREAM_TIMING") != NULL) {
    atari_emulate_enable_starscream_timing(a, true);
  }
}

void atari_emulate_enable_starscream_timing(atari_emulate* a, bool enable) {
  (void)a; /* singleton Musashi core - see m68k_bus.h's file comment */
  m68k_bus_enable_starscream_timing_override(enable);
}

/* MainWin.pas:1636-1650 - see atari_emulate.h's own comment. */
void atari_emulate_set_mc68000_freq(atari_emulate* a, double freq) {
  if (freq < 2000000.0 || freq > 16000000.0) return; /* MainWin.pas:1638 */
  a->mc68000_freq = freq;
  a->vbl_period = (int64_t)(a->mc68000_freq / a->vbl_freq + 0.5);
  a->ay->frq_ay_by_frq_z80 =
      (int64_t)(a->ay_freq / a->mc68000_freq / 8.0 * 4294967296.0 + 0.5);
  if (a->mfp_timer_freq > 0.0) {
    a->mfp.mc_by_mfp = a->mc68000_freq / a->mfp_timer_freq;
  }
}

/* MainWin.pas:1563-1585 - see atari_emulate.h's own comment. */
void atari_emulate_set_mfp_freq(atari_emulate* a, int mode, double freq_hz) {
  if (mode == 0) {
    a->mfp_timer_mode = 0;
    a->mfp_timer_freq = freq_hz;
  } else if (mode == 1 && freq_hz >= 1000000.0 && freq_hz <= 4365292.0) {
    a->mfp_timer_mode = 1;
    a->mfp_timer_freq = freq_hz;
  } else {
    return; /* MainWin.pas:1567-1577's if/else-if: no third branch, state
             * is left completely untouched otherwise. */
  }
  a->mfp.mc_by_mfp = a->mc68000_freq / a->mfp_timer_freq;
}

/* atari.pas:1436-1523 */
void atari_emulate_step(atari_emulate* a) {
  int64_t od = a->cycle_count;
  int i;

  bool vbl_due = (od - a->base_vbl >= a->vbl_period);
  a->vbl_acked_this_exec = false;

  /* atari.pas:1466's s68000interrupt(4,-1) does NOT check the CPU's
   * interrupt mask at all (confirmed directly from Starscream's own
   * assembly source, star027b/Starcpu32.asm's s68000interrupt: it only
   * tests/sets a per-level PENDING bit in ctx(interrupts), returning 0 if
   * this is a fresh request (bit was clear) or 1 if that level was
   * ALREADY pending from an earlier, not-yet-serviced request - MIG-0046,
   * superseding an earlier, wrong "synchronous mask check" model that
   * looked plausible from the Pascal call site alone but doesn't match
   * what the call actually does). The real mask-based servicing decision
   * happens later, entirely inside exec()'s own dispatch loop - exactly
   * matching Musashi's own level-triggered IRQ model, once IRQ4 is
   * asserted and left asserted for as long as the request stays pending
   * (vbl_irq_pending, cleared by int_ack once Musashi actually services
   * it - see atari_emulate.h). So: BaseVBL/TickCount advance whenever a
   * *new* (not already-pending) request is raised, or the 10%-slack
   * fallback trips - never based on whether the interrupt could be taken
   * "right now". */
  /* atari.pas:1466-1474: BOTH the "fresh request" and "already pending but
   * slack exceeded" conditions are checked in the SAME place, using the
   * SAME `od` captured once at the top of the function, before `min` is
   * computed - so if slack is ALREADY exceeded at step entry, BaseVBL
   * advances immediately and `min` for THIS SAME step is computed from
   * the new BaseVBL (large budget). Previously the slack check here ran
   * AFTER exec() using post-execution od, one full step later than the
   * original - meaning the step where slack first became exceeded still
   * computed a degenerate min=1 budget (matching the many-prior-steps
   * pattern) instead of the large budget Pascal computes for that exact
   * step. Confirmed via direct side-by-side per-step od/BaseVBL trace
   * against the oracle: both matched exactly for ~335 consecutive steps
   * of min=1 crawling, then diverged sharply at the exact step slack
   * exceeded (oracle: od jumps 84000->120004 in ONE step once BaseVBL
   * advances to 80000 before min is computed; this port advanced BaseVBL
   * only on the NEXT step, one step of degenerate min=1 crawling later
   * than it should have) - MIG-0049. */
  bool vbl_queued_fresh = false;
  bool vbl_slack_exceeded = false;
  if (vbl_due) {
    if (!a->vbl_irq_pending) {
      a->vbl_irq_pending = true;
      vbl_queued_fresh = true;
      trace_log_irq(od, "assert", 4, -1);
    } else if (od - a->base_vbl >= a->vbl_period + a->vbl_period / 10) {
      vbl_slack_exceeded = true;
      trace_log_irq(od, "coalesce", 4, -1);
    }
  }

  if (vbl_due && (vbl_queued_fresh || vbl_slack_exceeded)) {
    a->base_vbl += a->vbl_period;
    a->tick_count++;
    if (!a->do_loop && a->tick_count >= a->tick_count_max) {
      a->real_end_all = true;
    }
  }

  int64_t min = a->base_vbl + a->vbl_period - od;
  if (min <= 0) min = 1;

  for (i = 0; i < 4; i++) {
    int64_t t = mfp_emulate_timer(&a->mfp, i, od);
    if (t > 0 && min > t) min = t;
  }

  /* MIG-0054: this MUST run AFTER the mfp_emulate_timer loop above, not
   * before it (as it did until this fix). atari.pas's EmulateTimer calls
   * s68000interrupt(6,vec) SYNCHRONOUSLY at the exact moment it detects a
   * timer expiry - Starscream's pending-interrupt state is updated before
   * min is computed and before s68000exec(min) runs, for every timer,
   * every call. mfp_emulate_timer's C11 port mirrors this faithfully for
   * its OWN bookkeeping (m->level6_pending is set synchronously inside
   * the loop above, request_level6()), but m68k_bus_set_irq - the call
   * that actually tells MUSASHI about the pending level - was being made
   * from a `compute_irq_level(a)` snapshot taken BEFORE this loop ran,
   * using stale state. Any timer that first expired partway through THIS
   * very call (the overwhelmingly common case, since a fresh expiry is
   * exactly what makes `min` small enough to end the previous call here)
   * would sit fully invisible to Musashi - its IRQ line never raised -
   * for this entire call's `min`-cycle exec() burst, however large;
   * Musashi could only notice it on the NEXT atari_emulate_step call, one
   * full step (observed up to ~18000-40000 cycles in a 30s Temple_of_
   * Asherah.sndh render) later than Starscream ever would have. Direct
   * single-step comparison confirmed the mechanism precisely: a step
   * that computed min=18096/used=18096 (a full, uninterrupted burst) with
   * a level-6 "assert" trace event logged at the very START of that same
   * burst - meaning the interrupt was known to mfp.c's own state the
   * whole time but never passed to Musashi until 18096 cycles later.
   * This dwarfs the previously-documented "SR mask=7 critical section
   * marginally exceeding Timer A's own period" explanation (which
   * remains real but is a much smaller, secondary effect) and accounts
   * for the bulk of the Timer A/B/D coalesce-rate gap against the oracle
   * (all three vectors coalescing 25-97% of their asserts here, vs
   * 0-3.6% oracle-side, pre-fix). Real 68000 IPL priority is BY LEVEL
   * NUMBER (7=highest/NMI, 1=lowest) - MFP's level 6 outranks VBL's level
   * 4, so if both happen to be outstanding at once (entirely possible
   * once digidrum/envelope timers are active alongside the VBL handler),
   * level 6 must win. Starscream's own flush_interrupts (Starcpu32.asm)
   * scans from level 7 down to 1, confirming this is the real priority
   * order, not an arbitrary choice - this was previously checked
   * backwards (VBL first, MFP as the fallback). Shared with int_ack's own
   * re-assertion after acknowledging - see compute_irq_level, MIG-0052. */
  int irq_level = compute_irq_level(a);
  m68k_bus_set_irq(&a->bus, irq_level);

  bool dma_active = false;
  int64_t dma_boundary = dma_sound_next_boundary_cycles(&a->dma, a->mc68000_freq);
  if (dma_boundary >= 0) {
    if (min > dma_boundary) min = dma_boundary;
    dma_active = true;
  }

  /* MIG-0053: engine/third_party/musashi/m68k_in.c's stop opcode handler
   * unconditionally forces m68ki_remaining_cycles down to STOP's own tiny
   * cost whenever STOP executes, discarding whatever budget was left -
   * m68k_execute's return value ("used") then reports the FULL requested
   * `min` as consumed, even when an interrupt was serviced then the CPU
   * returned to STOP after only a few dozen real cycles of work. Found via
   * engine/trace_log.c's new AY_ENGINE_STEP_TRACE cross-referenced against
   * AY_ENGINE_IRQ_TRACE's "service" events: 4223 occurrences in a single
   * ~45s render, averaging ~11700 phantom cycles each - 13.7% of the
   * WHOLE render's total cycle count credited to a->cycle_count with no
   * corresponding real 68000 work. Because min is computed ONCE per call
   * (the time until the FURTHEST-out relevant deadline), padding to that
   * full min silently fast-forwards cycle_count past points where OTHER,
   * sooner-expiring MFP timers should have been individually rechecked -
   * exactly the missed-service/coalesce backlog MIG-0052 measured (more
   * request attempts, far fewer successful services, and correspondingly
   * fewer AY register writes than the oracle). Capping min whenever an
   * interrupt is ALREADY pending forces atari_emulate_step to be called
   * again immediately after handling it (recomputing a fresh, currently-
   * accurate min across all four timers), matching Starscream's own
   * apparent behavior of reporting honest short counts on early STOP -
   * this doesn't change WHAT gets serviced, only how promptly the
   * scheduler notices new work once something is already pending.
   *
   * Refined (still MIG-0053): capping min=1 unconditionally whenever
   * ANYTHING is pending over-corrected - it re-runs all four timers'
   * icnt-retry logic once per single INSTRUCTION for a handler's entire
   * body, not just once to guarantee ITS OWN dispatch, giving backlogged
   * retries far more chances per unit of real CPU work to grab the shared
   * level-6 slot than Starscream's natural once-per-outer-call cadence -
   * audible as extra, premature register writes. Only cap when nothing
   * was acked last step (a fresh/still-undispatched request still needs
   * the guarantee); once something IS acked, the very next step runs at
   * full budget, letting a handler's own tail code (and Musashi's own
   * padding-on-STOP, now harmless since nothing else is freshly pending)
   * proceed the way it would if genuinely running at full speed - see
   * atari_emulate.h's irq_acked_prev_exec comment. */
  bool need_dispatch_guarantee = (irq_level != 0) && !a->irq_acked_prev_exec;
  if (need_dispatch_guarantee && min > 1) min = 1;
  a->irq_acked_prev_exec = false; /* int_ack sets this again if it fires
                                   * during THIS step's exec() call. */

  int used = m68k_bus_exec(&a->bus, (int)min);
  trace_log_step(od, min, used);
  /* MIG-0046 originally padded `used` up to `min` here whenever Musashi's
   * STOP handler (m68k_op_stop) returned early, reasoning that Starscream
   * doesn't distinguish "halted, idling out the remainder" from real
   * execution - true for the very first VBL wait (od=39962 vs Pascal's
   * exact 40000, a 38-cycle gap) but MIG-0049 found this does NOT hold in
   * general: direct per-step od/BaseVBL comparison against the oracle at
   * a later, larger STOP-shortfall (step 1673: requested min=35994, real
   * work before STOP was only ~4380 cycles) showed Pascal reports the
   * HONEST short count (od advances by ~4380, not padded to the full
   * budget) - the interrupt only gets serviced value-for-value on a
   * SUBSEQUENT call once the CPU naturally re-reaches STOP again. Padding
   * unconditionally was overcrediting cycle_count on every large-shortfall
   * STOP hit, not just the negligible one at cold start. Removed - Musashi's
   * own `used` (honest, un-padded) is used directly, matching the oracle's
   * real behavior; the tiny 38-cycle first-VBL gap is left as a known,
   * negligible discrepancy (see migration_debt.yaml MIG-0049) rather than
   * "fixed" by a change that turned out to cause a much larger problem
   * elsewhere. */
  a->cycle_count += used;
  /* MIG-0056: drains any Starscream-timing-override correction accumulated
   * during this call's exec() burst (no-op, always 0, unless explicitly
   * enabled via atari_emulate_enable_starscream_timing - see m68k_bus.h). */
  a->cycle_count += m68k_bus_take_timing_correction();

  if (dma_active) {
    /* atari.pas:1695-1726, SynthesizerSNDH - the same tick-accumulation
     * formula as AY.pas's own SynthesizerAY, just driven by the 68000
     * odometer with a different ratio constant (FrqAyByFrqMC68000 there
     * vs FrqAyByFrqZ80 for the Z80 path) - ay_synthesizer_ay is generic
     * over "current CPU cycle count" despite the field name, so it's
     * reused as-is here; the caller must set a->ay->frq_ay_by_frq_z80 to
     * the Atari-appropriate ratio before driving atari_emulate_step. */
    ay_synthesizer_ay(a->ay, a->cycle_count);
  }
}

void atari_emulate_flush_pending_writes(atari_emulate* a) {
  int i;
  for (i = 0; i < a->pending_write_count; i++) {
    ay_chip_set_ay_register(&a->ay->chip, a->pending_writes[i].reg,
                             a->pending_writes[i].data);
    trace_log_ay(a->cycle_count, "write_apply", a->pending_writes[i].reg,
                 a->pending_writes[i].data);
  }
  a->pending_write_count = 0;
}
