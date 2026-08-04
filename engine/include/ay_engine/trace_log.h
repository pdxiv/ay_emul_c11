/* Optional, opt-in diagnostic tracing for cross-checking this port against
 * the Pascal oracle (ay_emul/TraceLog.pas is the Pascal-side sibling of
 * this module - keep the two log line formats in sync so a diff tool can
 * compare them directly). Disabled (zero-cost beyond one getenv() call and
 * a cached boolean check) unless the relevant environment variable is set:
 *
 *   AY_ENGINE_AY_TRACE=<path>  - logs every AY register write the emulated
 *                                68000 program issues via the $FF8800
 *                                region (engine/src/atari_emulate.c's
 *                                ym_region_write), with the CPU cycle count
 *                                ("od", matching s68000readOdometer's role)
 *                                as the timestamp - not wall-clock time,
 *                                since that's the time domain this whole
 *                                port's oracle-diff methodology already
 *                                reasons in.
 *   AY_ENGINE_IRQ_TRACE=<path> - logs VBL (level 4) and MFP (level 6)
 *                                interrupt request/coalesce/service events,
 *                                also cycle-count-timestamped.
 *   AY_ENGINE_STEP_TRACE=<path> - logs atari_emulate_step's own exec-budget
 *                                accounting: the requested cycle budget
 *                                ("min", the same value passed to
 *                                m68k_bus_exec) versus what Musashi actually
 *                                reported consuming ("used") - MIG-0052/
 *                                MIG-0053 investigation into whether
 *                                Musashi's STOP handling pads `used` up to
 *                                the full requested budget even when real
 *                                CPU work before hitting STOP was much
 *                                smaller (m68k_in.c's stop opcode handler
 *                                forces m68ki_remaining_cycles down to just
 *                                the STOP instruction's own cost, discarding
 *                                whatever budget was left, whenever STOP
 *                                executes - `#if !M68K_EMULATE_INT_ACK`
 *                                guards an *different*, unrelated auto-clear
 *                                elsewhere; this STOP-specific behavior is
 *                                unconditional).
 *   AY_ENGINE_MFP_TRACE=<path> - logs every write to an MFP register
 *                                (engine/mfp.c's mfp_bus_write), the
 *                                register's mfp.c enum index, the byte
 *                                written, and whether it triggered
 *                                m68k_bus_end_timeslice() (MIG-0054) -
 *                                added to correlate Timer A's residual
 *                                coalesce excess against nearby register
 *                                writes once the bulk of it (the missing
 *                                timeslice-release bug) was fixed.
 *
 * Both are line-buffered text files, one event per line, opened (truncated)
 * on first use and left open for the process lifetime.
 *
 * Asymmetry warning: the C11 side can log a "service" event (int_ack
 * actually firing, meaning Musashi's dispatch loop took the interrupt) via
 * m68k_bus's int_ack callback - Starscream (the Pascal side's 68000 core)
 * exposes no equivalent callback (confirmed from star027b/Starcpu32.asm:
 * s68000interrupt's vector is supplied at REQUEST time and dispatch happens
 * entirely inside the assembly's flush_interrupts with no Pascal-visible
 * hook). So TraceLog.pas can only ever log "assert"/"coalesce" (request-
 * time) events, never "service" - this is a real, unavoidable difference
 * between what each side can observe, not a bug in either log. */
#ifndef AY_ENGINE_TRACE_LOG_H
#define AY_ENGINE_TRACE_LOG_H

#include <stdbool.h>
#include <stdint.h>

/* event: "write_req" (the 68000 program issued the write) or
 * "write_apply" (the write actually took effect on the AY chip state -
 * may be later than write_req if MIG-0050's reentrant-flush/deferred-queue
 * mechanism delayed it). reg/value: the AY register index and byte value. */
void trace_log_ay(int64_t cycle, const char* event, int reg, int value);

/* event: "assert" (a fresh interrupt request, previously not pending),
 * "coalesce" (a request found the shared per-level slot already occupied -
 * MFP only, see mfp.h's MIG-0051 comment; logged instead of silently
 * dropped) or "service" (int_ack fired - Musashi actually dispatched this
 * level - C11-side only, see this header's asymmetry note above).
 * level: 4 (VBL) or 6 (MFP). vector: the autovector/vector value, or -1 if
 * not applicable (e.g. VBL's autovector request/coalesce events). */
void trace_log_irq(int64_t cycle, const char* event, int level, int vector);

/* cycle: a->cycle_count BEFORE this step's exec() call (matching od's role
 * elsewhere). min/used: the requested budget and Musashi's reported
 * consumption for this call - see AY_ENGINE_STEP_TRACE above. */
void trace_log_step(int64_t cycle, int64_t min, int used);

/* reg: mfp.c's REG_* enum index (0-23). released: whether this write
 * triggered m68k_bus_end_timeslice() - see AY_ENGINE_MFP_TRACE above. */
void trace_log_mfp(int64_t cycle, int reg, int value, bool released);

#endif /* AY_ENGINE_TRACE_LOG_H */
