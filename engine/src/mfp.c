/* C11 port of ay_emul/atari.pas's MFP68901 emulation - see
 * engine/include/ay_engine/mfp.h for the ported contract and scope. */
#include "ay_engine/mfp.h"

#include <string.h>

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
    case REG_TAC:
      set_timer_delay_mode(&m->timers[0], value & 7, od);
      m->reg[REG_TAC] = value;
      break;
    case REG_TBC:
      set_timer_delay_mode(&m->timers[1], value & 7, od);
      m->reg[REG_TBC] = value;
      break;
    case REG_TDC:
      set_timer_delay_mode(&m->timers[2], (value >> 4) & 7, od);
      set_timer_delay_mode(&m->timers[3], value & 7, od);
      m->reg[REG_TDC] = value;
      break;
    case REG_TAD: set_timer_data_register(m, &m->timers[0], value); break;
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

void mfp_write_byte_at(mfp* m, uint32_t address, uint8_t value, int64_t od) {
  if ((address & 1) == 0) return;
  int i = (int)((address - 0xFFFA01) / 2);
  if (i >= 0 && i < 24) set_mfp_register(m, i, value, od);
}

/* atari.pas:1383-1431, minus the ICnt retry mechanism - see mfp.h. */
int64_t mfp_emulate_timer(mfp* m, int idx, int64_t od) {
  mfp_timer* t = &m->timers[idx];
  if (t->delay <= 0) return -1;

  if (od - t->base >= t->delay) {
    if (t->ie) {
      m->reg[t->ipx_index] |= t->ipsb;
      if (t->im && (m->reg[REG_VCR] & 8) != 0) {
        /* software end-of-interrupt mode: ISR set eagerly, cleared only by
         * an explicit register write (matches SetMFPRegister's ISA/ISB
         * case, which requires VCR bit 3 set to accept the clear). */
        m->reg[t->isx_index] |= t->ipsb;
      }
    }
    t->base += t->delay;
    t->dr = expand_timer_dr(m->reg[t->txd_index]);
    t->delay = get_mfp_delay(t->dm, t->dr);
  }
  int64_t result = t->base + t->delay - od;
  return result <= 0 ? 1 : result;
}

bool mfp_irq_pending(const mfp* m) {
  int i;
  for (i = 0; i < 4; i++) {
    const mfp_timer* t = &m->timers[i];
    if (t->ie && t->im && (m->reg[t->ipx_index] & t->ipsb) != 0) return true;
  }
  return false;
}

int mfp_ack_interrupt(mfp* m) {
  int i;
  for (i = 0; i < 4; i++) {
    mfp_timer* t = &m->timers[i];
    if (t->ie && t->im && (m->reg[t->ipx_index] & t->ipsb) != 0) {
      int vector = (m->reg[REG_VCR] & 0xF0) | t->v;
      if ((m->reg[REG_VCR] & 8) == 0) { /* automatic end-of-interrupt mode */
        m->reg[t->ipx_index] &= (uint8_t)~t->ipsb;
        m->reg[t->isx_index] &= (uint8_t)~t->ipsb;
      }
      return vector;
    }
  }
  return -1;
}
