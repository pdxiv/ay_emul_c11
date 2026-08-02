/* C11 port of ay_emul/atari.pas's Atari_Emulate scheduling loop - see
 * engine/include/ay_engine/atari_emulate.h for the ported contract and
 * scope. */
#include "ay_engine/atari_emulate.h"

#include <string.h>

/* ---- MFP callback region ($FFFA00-$FFFA2F) ---- */

static uint8_t mfp_region_read(void* userdata, uint32_t address) {
  atari_emulate* a = (atari_emulate*)userdata;
  return mfp_read_byte_at(&a->mfp, address, a->cycle_count);
}
static void mfp_region_write(void* userdata, uint32_t address,
                              uint8_t value) {
  atari_emulate* a = (atari_emulate*)userdata;
  mfp_write_byte_at(&a->mfp, address, value, a->cycle_count);
}

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
static void ym_region_write(void* userdata, uint32_t address,
                             uint8_t value) {
  atari_emulate* a = (atari_emulate*)userdata;
  switch (address & 3) {
    case 0:
      a->ym_cur_reg = value;
      break;
    case 2:
      /* Original's data-write branch also does a reentrant SynthesizerSNDH
       * flush + BuffLen/IntFlag-driven deferred-write bookkeeping
       * (atari.pas:538-556) - render-loop orchestration out of scope this
       * milestone, matching the Z80/AY milestone's on_ay_write contract
       * (engine/z80_bus.h): apply the write immediately, let a future
       * render-loop caller own flush timing. */
      if (a->ym_cur_reg < 14) {
        ay_chip_set_ay_register(&a->ay->chip, a->ym_cur_reg, value);
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

static int int_ack(void* userdata, int level) {
  atari_emulate* a = (atari_emulate*)userdata;
  if (level == 4) {
    a->vbl_acked_this_exec = true;
    return -1; /* autovector, matches s68000interrupt(4,-1) */
  }
  if (level == 6) {
    return mfp_ack_interrupt(&a->mfp);
  }
  return -1;
}

void atari_emulate_init(atari_emulate* a, uint8_t* mem, uint32_t mem_size,
                        ay_engine* ay, double mc68000_freq,
                        int64_t vbl_period, double ay_freq) {
  memset(a, 0, sizeof(*a));
  a->mem = mem;
  a->mem_size = mem_size;
  a->ay = ay;
  a->mc68000_freq = mc68000_freq;
  a->vbl_period = vbl_period;
  a->ay_freq = ay_freq;
  a->tick_count_max = 1; /* caller overrides for real playback; a nonzero
                          * default avoids an immediate real_end_all on the
                          * very first VBL for standalone/test use. */

  mfp_init(&a->mfp);
  dma_sound_init(&a->dma);

  m68k_bus_init(&a->bus);
  m68k_bus_add_flat_region(&a->bus, 0, mem_size - 1, mem);
  m68k_bus_add_callback_region(&a->bus, 0xFFFA00, 0xFFFA2F, mfp_region_read,
                                mfp_region_write, a);
  m68k_bus_add_callback_region(&a->bus, 0xFF8900, 0xFF89FF, dma_region_read,
                                dma_region_write, a);
  m68k_bus_add_callback_region(&a->bus, 0xFF8800, 0xFF88FF, ym_region_read,
                                ym_region_write, a);
  a->bus.int_ack = int_ack;
  a->bus.int_ack_userdata = a;

  ay->on_mix_dma = on_mix_dma;
  ay->on_mix_dma_userdata = a;

  m68k_bus_activate(&a->bus);
  m68k_bus_reset(&a->bus);
}

/* atari.pas:1436-1523 */
void atari_emulate_step(atari_emulate* a) {
  int64_t od = a->cycle_count;
  int i;

  bool vbl_due = (od - a->base_vbl >= a->vbl_period);
  a->vbl_acked_this_exec = false;

  int irq_level = 0;
  if (vbl_due) irq_level = 4;
  else if (mfp_irq_pending(&a->mfp)) irq_level = 6;
  m68k_bus_set_irq(&a->bus, irq_level);

  int64_t min = a->base_vbl + a->vbl_period - od;
  if (min <= 0) min = 1;

  for (i = 0; i < 4; i++) {
    int64_t t = mfp_emulate_timer(&a->mfp, i, od);
    if (t > 0 && min > t) min = t;
  }

  bool dma_active = false;
  int64_t dma_boundary = dma_sound_next_boundary_cycles(&a->dma, a->mc68000_freq);
  if (dma_boundary >= 0) {
    if (min > dma_boundary) min = dma_boundary;
    dma_active = true;
  }

  int used = m68k_bus_exec(&a->bus, (int)min);
  a->cycle_count += used;
  od = a->cycle_count;

  if (vbl_due) {
    /* atari.pas:1444-1445: accept if acked, or force-advance past a
     * missed/masked interrupt once slack (10%) is exceeded. */
    if (a->vbl_acked_this_exec || (od - a->base_vbl >= a->vbl_period + a->vbl_period / 10)) {
      a->base_vbl += a->vbl_period;
      a->tick_count++;
      if (!a->do_loop && a->tick_count >= a->tick_count_max) {
        a->real_end_all = true;
      }
    }
  }

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
