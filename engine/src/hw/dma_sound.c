/* C11 port of ay_emul/atari.pas's DMA sound emulation - see
 * engine/include/ay_engine/dma_sound.h for the ported contract and scope. */
#include "ay_engine/hw/dma_sound.h"

#include <string.h>

void dma_sound_init(dma_sound* d) { memset(d, 0, sizeof(*d)); }

/* atari.pas:693-701 */
static void microwire_dummy_shift(dma_sound* d) {
  if (d->microwire_shift > 0) {
    d->microwire_shift--;
    d->microwire_data = (uint16_t)((d->microwire_data << 1) |
                                    (d->microwire_data >> 15));
    d->microwire_mask = (uint16_t)((d->microwire_mask << 1) |
                                    (d->microwire_mask >> 15));
  }
}

/* atari.pas:286-314. `od` is the current cycle count. */
static void ctrl_dma_snd(dma_sound* d, int64_t od) {
  d->ploop = (d->ctrl & 2) != 0;
  if ((d->ctrl & 1) != 0 && d->play) return;
  d->play = (d->ctrl & 1) != 0;
  if (d->play) {
    d->prate = (uint8_t)(d->mode & 3);
    d->pmono = (d->mode & 128) != 0;
    d->pstart = d->start;
    d->ppos = -1;
    d->pend = d->end;
    (void)od;
  }
}

/* atari.pas:703-736. `od` is the current cycle count. */
static uint32_t calc_dma_snd_counter(const dma_sound* d, double mc68000_freq,
                                      int64_t od) {
  if (!d->play) return d->pstart;

  uint32_t k;
  if (d->pmono) {
    k = d->pend;
  } else {
    k = d->pstart + (d->pend - d->pstart) / 2;
  }
  int64_t result = (int64_t)((50066.0 / 8.0) * (1 << d->prate) *
                              ((double)od - d->pbase) / mc68000_freq);
  if ((int64_t)d->pstart + result < (int64_t)k) {
    if (d->pmono) {
      result = (result & ~(int64_t)1) + d->pstart;
    } else {
      result = result * 2 + d->pstart;
    }
  } else if (!d->ploop) {
    result = d->pend;
  } else if (k > d->pstart) {
    result = result % (int64_t)(k - d->pstart);
    if (d->pmono) {
      result = (result & ~(int64_t)1) + d->pstart;
    } else {
      result = result * 2 + d->pstart;
    }
  } else {
    result = d->pstart;
  }
  return (uint32_t)result;
}

/* atari.pas:738-799 */
uint8_t dma_sound_read_byte_at(const dma_sound* d, uint32_t address,
                                int64_t od) {
  /* mc68000_freq isn't available here (matches the original: reads of the
   * live counter go through CalcDmaSndCounter, which needs it) - callers
   * needing $FF8909/0B/0D should use dma_sound_read_counter_byte_at
   * instead. Every other register is a plain field read. */
  (void)od;
  switch (address) {
    case 0xFF8901: return d->ctrl;
    case 0xFF8921: return d->mode;
    case 0xFF8903: return (uint8_t)(d->start >> 16);
    case 0xFF8905: return (uint8_t)((d->start >> 8) & 0xFF);
    case 0xFF8907: return (uint8_t)(d->start & 0xFF);
    case 0xFF890F: return (uint8_t)(d->end >> 16);
    case 0xFF8911: return (uint8_t)((d->end >> 8) & 0xFF);
    case 0xFF8913: return (uint8_t)(d->end & 0xFF);
    case 0xFF8923: return (uint8_t)(d->microwire_data & 0xFF);
    case 0xFF8925: return (uint8_t)(d->microwire_mask & 0xFF);
    default: return 0;
  }
}

uint8_t dma_sound_read_counter_byte_at(dma_sound* d, uint32_t address,
                                        double mc68000_freq, int64_t od) {
  uint32_t counter = calc_dma_snd_counter(d, mc68000_freq, od);
  switch (address) {
    case 0xFF8909: return (uint8_t)(counter >> 16);
    case 0xFF890B: return (uint8_t)((counter >> 8) & 0xFF);
    case 0xFF890D: return (uint8_t)(counter & 0xFF);
    case 0xFF8922:
      microwire_dummy_shift(d);
      return (uint8_t)(d->microwire_data >> 8);
    case 0xFF8924:
      microwire_dummy_shift(d);
      return (uint8_t)(d->microwire_mask >> 8);
    default: return dma_sound_read_byte_at(d, address, od);
  }
}

/* atari.pas:807-873. `od` is the current cycle count. */
void dma_sound_write_byte_at(dma_sound* d, uint32_t address, uint8_t value,
                              int64_t od) {
  switch (address) {
    case 0xFF8901:
      /* Original also does a reentrant SynthesizerSNDH flush + IntFlag
       * bookkeeping here (atari.pas:817-828) - render-loop orchestration
       * out of scope this milestone, see dma_sound.h's file comment. */
      d->ctrl = (uint8_t)(value & 3);
      if ((value & 1) != 0 && !d->play) {
        d->pbase = (double)od;
        d->pcurr = d->pbase;
      }
      ctrl_dma_snd(d, od);
      break;
    case 0xFF8921: d->mode = (uint8_t)(value & 0x83); break;
    case 0xFF8903:
      d->start = (d->start & 0xFFFF) | ((uint32_t)value << 16);
      break;
    case 0xFF8905:
      d->start = (d->start & 0xFF00FF) | ((uint32_t)value << 8);
      break;
    case 0xFF8907:
      d->start = (d->start & 0xFFFF00) | (value & 0xFE);
      break;
    case 0xFF890F:
      d->end = (d->end & 0xFFFF) | ((uint32_t)value << 16);
      break;
    case 0xFF8911:
      d->end = (d->end & 0xFF00FF) | ((uint32_t)value << 8);
      break;
    case 0xFF8913:
      d->end = (d->end & 0xFFFF00) | (value & 0xFE);
      break;
    case 0xFF8922:
      d->microwire_data = (uint16_t)((d->microwire_data & 0xFF) |
                                      ((uint16_t)value << 8));
      break;
    case 0xFF8923:
      d->microwire_shift = 16;
      d->microwire_data =
          (uint16_t)((d->microwire_data & 0xFF00) | value);
      break;
    case 0xFF8924:
      d->microwire_mask = (uint16_t)((d->microwire_mask & 0xFF) |
                                      ((uint16_t)value << 8));
      break;
    case 0xFF8925:
      d->microwire_mask =
          (uint16_t)((d->microwire_mask & 0xFF00) | value);
      break;
    default: break;
  }
}

/* atari.pas:1468-1481 (the DMA-boundary clamp inside Atari_Emulate) */
int64_t dma_sound_next_boundary_cycles(const dma_sound* d,
                                        double mc68000_freq) {
  if (!d->play) return -1;
  return (int64_t)(mc68000_freq / ((50066.0 / 8.0) * (1 << d->prate)));
}

/* atari.pas:1630-1693. The original reads `bank0[k xor 1]` (mono) /
 * `bank0[k+1]`,`bank0[k]` (stereo L,R) because its `bank0` is word-swapped
 * for Starscream's fast direct-offset fetch path (see m68k_bus.c and
 * OracleHarness.pas's RunM68kTest for the same finding on the CPU side).
 * Undoing that swap algebraically: bank0[i] holds the real byte from
 * address (i xor 1), so bank0[k xor 1] = real byte at k, bank0[k+1] = real
 * byte at (k+1)xor1 = k (for even k), and bank0[k] = real byte at k xor 1
 * = k+1. I.e. real memory holds mono samples at k directly, and stereo
 * (left,right) samples at (k, k+1) directly - our plain, unswapped `mem`
 * needs no such compensation, just mem[k] (mono) / mem[k],mem[k+1]
 * (stereo L,R). */
void dma_sound_mix(dma_sound* d, const uint8_t* mem, uint32_t mem_size,
                    int atari_dma_level, double mc68000_freq, double ay_freq,
                    int* lev_l, int* lev_r) {
  if (d->play) {
    if (d->pmono) {
      int64_t k = (int64_t)d->pstart +
                  (int64_t)((50066.0 / 8.0) * (1 << d->prate) *
                            (d->pcurr - d->pbase) / mc68000_freq);
      if (k != d->ppos) {
        d->ppos = (int32_t)k;
        if ((uint32_t)k >= d->pend) {
          d->play = false;
          if (d->ploop) {
            ctrl_dma_snd(d, (int64_t)d->pcurr);
            d->pbase = d->pcurr;
          }
        }
        if (d->play && (uint32_t)k < mem_size) {
          d->prev_l = (int8_t)mem[k] * atari_dma_level / 128;
        }
      }
      *lev_l += d->prev_l;
      *lev_r += d->prev_l;
    } else {
      int64_t k = (int64_t)d->pstart +
                  (int64_t)((50066.0 / 8.0) * (1 << d->prate) *
                            (d->pcurr - d->pbase) / mc68000_freq) *
                      2;
      if (k != d->ppos) {
        d->ppos = (int32_t)k;
        if ((uint32_t)k >= d->pend) {
          d->play = false;
          if (d->ploop) {
            ctrl_dma_snd(d, (int64_t)d->pcurr);
            d->pbase = d->pcurr;
          }
        }
        if (d->play) {
          /* Real (left,right) sample order at (k, k+1) - see dma_sound.c's
           * derivation in the file comment above dma_sound_mix. */
          if ((uint32_t)k < mem_size)
            d->prev_l = (int8_t)mem[k] * atari_dma_level / 128;
          if ((uint32_t)(k + 1) < mem_size)
            d->prev_r = (int8_t)mem[k + 1] * atari_dma_level / 128;
        }
      }
      *lev_l += d->prev_l;
      *lev_r += d->prev_r;
    }
    if (d->play) d->pcurr += mc68000_freq * 8.0 / ay_freq;
  }
}
