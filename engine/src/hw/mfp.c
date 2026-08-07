/* C11 port of ay_emul/atari.pas's MFP68901 emulation - see
 * engine/include/ay_engine/mfp.h for the ported contract and scope. */
#include "ay_engine/hw/mfp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/util/trace_log.h"

/* MIG-0056 Timer A deep-dive only, temporary - AY_ENGINE_TIMERA_TRACE=<path>.
 * The C11-side sibling of ay_emul/TraceLog.pas's TraceLogTimerA - same
 * event names and field order, so both logs can be diffed directly
 * without a translation step. Off unless the env var is set. */
static FILE* g_timera_trace = NULL;
static bool g_timera_trace_checked = false;
static void ensure_timera_trace(void) {
  if (g_timera_trace_checked) return;
  g_timera_trace_checked = true;
  const char* path = getenv("AY_ENGINE_TIMERA_TRACE");
  if (path != NULL) g_timera_trace = fopen(path, "w");
}
static void trace_log_timera(int64_t cycle, const char* event, int value,
                              int dm, int64_t dr, int64_t base, int64_t delay,
                              bool ie, bool im) {
  ensure_timera_trace();
  if (g_timera_trace == NULL) return;
  fprintf(g_timera_trace,
          "cycle=%lld event=%s value=%d dm=%d dr=%lld base=%lld delay=%lld "
          "ie=%d im=%d pc=%06x a6=%08x d0=%08x\n",
          (long long)cycle, event, value, dm, (long long)dr, (long long)base,
          (long long)delay, ie ? 1 : 0, im ? 1 : 0, m68k_bus_get_reg(16),
          m68k_bus_get_reg(14), m68k_bus_get_reg(0));
  fflush(g_timera_trace);
}

/* atari.pas:93 */
static const int MFP_KOEFS[8] = {0, 4, 10, 16, 50, 64, 100, 200};
/* atari.pas:44,1580-1586: MCbyMFP = MC68000Freq/MFPFreq =
 * (ClockFreq/4)/(ClockFreq/13) = 13/4 exactly, independent of ClockFreq. */
static const double MC_BY_MFP = 13.0 / 4.0;

/* Register indices, matching MFP_Registers' field order (atari.pas:111-140). */
enum {
  REG_PDR = 0, REG_AER = 1, REG_DIR = 2,
  REG_IEA = 3, REG_IEB = 4,
  REG_IPA = 5, REG_IPB = 6,
  REG_ISA = 7, REG_ISB = 8,
  REG_IMA = 9, REG_IMB = 10,
  REG_VCR = 11,
  REG_TAC = 12, REG_TBC = 13, REG_TDC = 14, /* TDC = combined C+D control */
  REG_TAD = 15, REG_TBD = 16, REG_TCD = 17, REG_TDD = 18,
  REG_SYC = 19, REG_UCR = 20, REG_RES = 21, REG_TRS = 22, REG_UAD = 23
};

/* atari.pas:356-358 */
static int64_t expand_timer_dr(uint8_t dr) { return dr != 0 ? dr : 256; }

/* atari.pas:361-364 */
static int64_t get_mfp_delay(uint8_t dm, int64_t dr) {
  return (int64_t)((double)dr * MFP_KOEFS[dm] * MC_BY_MFP);
}

void mfp_init(mfp* m) {
  memset(m, 0, sizeof(*m));

  m->reg[REG_VCR] = 0x40; /* atari.pas:1344, Atari_InitEmu's reset default */

  m->timers[0].v = 13; m->timers[0].ipx_index = REG_IPA;
  m->timers[0].isx_index = REG_ISA; m->timers[0].txd_index = REG_TAD;
  m->timers[0].ipsb = 32;

  m->timers[1].v = 8; m->timers[1].ipx_index = REG_IPA;
  m->timers[1].isx_index = REG_ISA; m->timers[1].txd_index = REG_TBD;
  m->timers[1].ipsb = 1;

  m->timers[2].v = 5; m->timers[2].ipx_index = REG_IPB;
  m->timers[2].isx_index = REG_ISB; m->timers[2].txd_index = REG_TCD;
  m->timers[2].ipsb = 32;

  m->timers[3].v = 4; m->timers[3].ipx_index = REG_IPB;
  m->timers[3].isx_index = REG_ISB; m->timers[3].txd_index = REG_TDD;
  m->timers[3].ipsb = 16;

  /* atari.pas:1346-1354 - MIG-0054 (docs/mfp_reference.md cross-check):
   * Atari_InitEmu explicitly sets DR/Cnt to 256 for all four timers after
   * zeroing everything else, NOT left at 0 - a raw data-register byte of
   * 0 always expands to 256 (see expand_timer_dr), so this seeds every
   * timer as if its data register already held that expanded value, the
   * same way real hardware's "00 reloads as 256" rule applies from power-
   * on. Previously left at 0 here (from the memset above), meaning any
   * timer started before its data register was ever explicitly written
   * would compute a delay of exactly 0 (get_mfp_delay(dm, 0) = 0) instead
   * of the same nonzero delay atari.pas would compute from dr=256. */
  int i;
  for (i = 0; i < 4; i++) {
    m->timers[i].dr = 256;
    m->timers[i].cnt = 256;
  }
}

/* atari.pas:366-372. `od` is the current cycle count (caller-supplied,
 * matching s68000readOdometer's role). */
static int64_t calc_timer_cnt(mfp_timer* t, int64_t od) {
  if (t->dm != 0) {
    double period = MFP_KOEFS[t->dm] * MC_BY_MFP;
    int64_t elapsed_periods = (int64_t)((double)(od - t->base) / period);
    t->cnt = t->dr - (elapsed_periods % t->dr);
  }
  return t->cnt;
}

/* atari.pas:374-382 */
static void set_timer_data_register(mfp* m, mfp_timer* t, uint8_t value) {
  m->reg[t->txd_index] = value;
  if (t->dm == 0) {
    t->dr = expand_timer_dr(value);
    t->cnt = t->dr;
  }
}

/* atari.pas:384-404. `od` is the current cycle count. */
static void set_timer_delay_mode(mfp_timer* t, uint8_t new_dm, int64_t od) {
  if (new_dm == 0) {
    calc_timer_cnt(t, od);
    t->delay = 0;
    t->dm = 0;
  } else if (new_dm != t->dm) {
    t->delay = get_mfp_delay(new_dm, t->dr);
    if (t->dm != 0) {
      t->base = od - (int64_t)((double)(od - t->base) *
                                (MFP_KOEFS[new_dm] / (double)MFP_KOEFS[t->dm]));
    } else {
      t->base = od - get_mfp_delay(new_dm, t->dr - t->cnt);
    }
    t->dm = new_dm;
  }
}

/* atari.pas:406-490. `od` is the current cycle count, needed by the
 * SetTimerDelayMode calls this dispatches to. */
static void set_mfp_register(mfp* m, int num, uint8_t value, int64_t od) {
  switch (num) {
    case REG_PDR: case REG_AER: case REG_DIR:
    case REG_SYC: case REG_UCR: case REG_RES: case REG_TRS: case REG_UAD:
      m->reg[num] = value;
      break;
    case REG_IEA:
      m->timers[0].ie = (value & 32) != 0;
      m->timers[1].ie = (value & 1) != 0;
      m->reg[REG_IEA] = value;
      break;
    case REG_IEB:
      m->timers[2].ie = (value & 32) != 0;
      m->timers[3].ie = (value & 16) != 0;
      m->reg[REG_IEB] = value;
      break;
    case REG_IPA: case REG_IPB:
      m->reg[num] = m->reg[num] & value;
      break;
    case REG_ISA: case REG_ISB:
      if (m->reg[REG_VCR] & 8) m->reg[num] = m->reg[num] & value;
      break;
    case REG_IMA:
      m->timers[0].im = (value & 32) != 0;
      m->timers[1].im = (value & 1) != 0;
      m->reg[REG_IMA] = value;
      break;
    case REG_IMB:
      m->timers[2].im = (value & 32) != 0;
      m->timers[3].im = (value & 16) != 0;
      m->reg[REG_IMB] = value;
      break;
    case REG_VCR:
      m->reg[REG_VCR] = value & 0xF8;
      if ((value & 8) == 0) {
        m->reg[REG_ISA] = 0;
        m->reg[REG_ISB] = 0;
      }
      break;
    case REG_TAC: {
      mfp_timer* t0 = &m->timers[0];
      trace_log_timera(od, "write_tac_before", value, t0->dm, t0->dr,
                        t0->base, t0->delay, t0->ie, t0->im);
      set_timer_delay_mode(t0, value & 7, od);
      m->reg[REG_TAC] = value;
      trace_log_timera(od, "write_tac_after", value, t0->dm, t0->dr,
                        t0->base, t0->delay, t0->ie, t0->im);
      break;
    }
    case REG_TBC:
      set_timer_delay_mode(&m->timers[1], value & 7, od);
      m->reg[REG_TBC] = value;
      break;
    case REG_TDC:
      set_timer_delay_mode(&m->timers[2], (value >> 4) & 7, od);
      set_timer_delay_mode(&m->timers[3], value & 7, od);
      m->reg[REG_TDC] = value;
      break;
    case REG_TAD: {
      mfp_timer* t0 = &m->timers[0];
      trace_log_timera(od, "write_tad_before", value, t0->dm, t0->dr,
                        t0->base, t0->delay, t0->ie, t0->im);
      set_timer_data_register(m, t0, value);
      trace_log_timera(od, "write_tad_after", value, t0->dm, t0->dr,
                        t0->base, t0->delay, t0->ie, t0->im);
      break;
    }
    case REG_TBD: set_timer_data_register(m, &m->timers[1], value); break;
    case REG_TCD: set_timer_data_register(m, &m->timers[2], value); break;
    case REG_TDD: set_timer_data_register(m, &m->timers[3], value); break;
    default: break;
  }
}

/* atari.pas:892-911. Note: mfp_write_byte/read_byte need the current cycle
 * count for timer-mode/data-register writes and live-countdown reads;
 * callers (engine/atari_emulate.c) pass it through explicitly rather than
 * this module reaching into a global, matching engine/ay.c's
 * ay_synthesizer_ay(engine, current_tact) convention. */
uint8_t mfp_read_byte_at(mfp* m, uint32_t address, int64_t od) {
  if ((address & 1) == 0) return 0;
  int i = (int)((address - 0xFFFA01) / 2);
  switch (i) {
    case 15: return (uint8_t)calc_timer_cnt(&m->timers[0], od);
    case 16: return (uint8_t)calc_timer_cnt(&m->timers[1], od);
    case 17: return (uint8_t)calc_timer_cnt(&m->timers[2], od);
    case 18: return (uint8_t)calc_timer_cnt(&m->timers[3], od);
    default:
      return (i >= 0 && i < 24) ? m->reg[i] : 0;
  }
}

bool mfp_write_byte_at(mfp* m, uint32_t address, uint8_t value, int64_t od) {
  if ((address & 1) == 0) return false;
  int i = (int)((address - 0xFFFA01) / 2);
  if (i < 0 || i >= 24) return false;
  set_mfp_register(m, i, value, od);
  /* atari.pas:434-516's SetMFPRegister - s68000releaseTimeslice is called
   * for exactly these cases, see mfp.h's doc comment (MIG-0054). */
  switch (i) {
    case REG_IEA: case REG_IEB: case REG_IMA: case REG_IMB:
    case REG_TAC: case REG_TBC: case REG_TDC:
    case REG_TAD: case REG_TBD: case REG_TCD: case REG_TDD:
      return true;
    default:
      return false;
  }
}

uint8_t mfp_bus_read(void* userdata, uint32_t address) {
  const mfp_bus_context* ctx = (const mfp_bus_context*)userdata;
  return mfp_read_byte_at(ctx->m, address, *ctx->od);
}

void mfp_bus_write(void* userdata, uint32_t address, uint8_t value) {
  const mfp_bus_context* ctx = (const mfp_bus_context*)userdata;
  bool released = mfp_write_byte_at(ctx->m, address, value, *ctx->od);
  if ((address & 1) != 0) {
    int reg = (int)((address - 0xFFFA01) / 2);
    trace_log_mfp(*ctx->od, reg, value, released);
  }
  if (released) {
    m68k_bus_end_timeslice();
  }
}

/* atari.pas:1383-1431 (EmulateTimer) in full, including the ICnt retry
 * mechanism - MIG-0051 (see mfp.h's file comment for why the earlier
 * "unnecessary under the level-triggered model" reasoning was wrong). */
static void request_level6(mfp* m, mfp_timer* t, int64_t od) {
  /* atari.pas:1400-1401,1418-1419: MFP_DT.IPx^:=IPx^ or IPSb (unconditional
   * per-source pending flag) plus, if masked-and-EOI-mode, the ISR flag -
   * this part is unconditional and happens regardless of whether the
   * shared level-6 slot below is free. */
  m->reg[t->ipx_index] |= t->ipsb;
  if (t->im && (m->reg[REG_VCR] & 8) != 0) {
    m->reg[t->isx_index] |= t->ipsb;
  }
  if (!t->im) return; /* atari.pas only attempts s68000interrupt if IM set */

  int vector = (m->reg[REG_VCR] & 0xF0) | t->v;
  if (!m->level6_pending) {
    /* atari.pas: s68000interrupt(...)=0 - request succeeds, latching this
     * timer's vector into the shared slot (matching Starscream's own
     * __interrupts[6] storage at request time - see mfp.h). */
    m->level6_pending = true;
    m->pending_vector = vector;
    trace_log_irq(od, "assert", 6, vector);
    if ((m->reg[REG_VCR] & 8) == 0) { /* automatic end-of-interrupt mode */
      m->reg[t->ipx_index] &= (uint8_t)~t->ipsb;
      m->reg[t->isx_index] &= (uint8_t)~t->ipsb;
    }
  } else {
    /* atari.pas: s68000interrupt(...)<>0 - already pending, queue a retry. */
    trace_log_irq(od, "coalesce", 6, vector);
    t->icnt++;
  }
}

int64_t mfp_emulate_timer(mfp* m, int idx, int64_t od) {
  mfp_timer* t = &m->timers[idx];

  /* atari.pas:1383-1387 - retry a previously-queued request, one per
   * call, exactly as many times as it takes for the shared slot to free
   * up. */
  if (t->icnt > 0) {
    if (!t->ie || !t->im) {
      t->icnt = 0;
    } else if (!m->level6_pending) {
      m->level6_pending = true;
      m->pending_vector = (m->reg[REG_VCR] & 0xF0) | t->v;
      trace_log_irq(od, "assert", 6, m->pending_vector);
      t->icnt--;
      if (t->icnt == 0 && (m->reg[REG_VCR] & 8) == 0) {
        m->reg[t->ipx_index] &= (uint8_t)~t->ipsb;
        m->reg[t->isx_index] &= (uint8_t)~t->ipsb;
      }
    }
    /* else: still occupied - stays queued, retried again next call. */
  }

  if (t->delay <= 0) return -1;

  if (od - t->base >= t->delay) {
    if (idx == 0) {
      trace_log_timera(od, "expire_before", 0, t->dm, t->dr, t->base,
                        t->delay, t->ie, t->im);
    }
    if (t->ie) request_level6(m, t, od);
    t->base += t->delay;
    t->dr = expand_timer_dr(m->reg[t->txd_index]);
    t->delay = get_mfp_delay(t->dm, t->dr);
    if (idx == 0) {
      trace_log_timera(od, "expire_after", 0, t->dm, t->dr, t->base,
                        t->delay, t->ie, t->im);
    }
  }
  int64_t result = t->base + t->delay - od;
  return result <= 0 ? 1 : result;
}

bool mfp_irq_pending(const mfp* m) { return m->level6_pending; }

int mfp_ack_interrupt(mfp* m) {
  if (!m->level6_pending) return -1;
  m->level6_pending = false;
  return m->pending_vector;
}
