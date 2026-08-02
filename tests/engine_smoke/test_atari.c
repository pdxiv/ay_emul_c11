/* Smoke tests for engine/src/mfp.c, dma_sound.c, and atari_emulate.c.
 * Real differential validation against the original's atari.pas lives in
 * tests/oracle_diff - see README.md. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ay_engine/atari_emulate.h"
#include "ay_engine/dma_sound.h"
#include "ay_engine/mfp.h"
#include "m68k.h"

/* atari.pas:44,1580-1586 */
#define MC68000_FREQ 8000000.0 /* ~8MHz, matches Atari ST */
#define MC_BY_MFP (13.0 / 4.0)

static void test_mfp_timer_countdown_and_fire(void) {
  mfp m;
  mfp_init(&m);

  /* Configure Timer A: data register = 10, prescaler mode 1 (divide by 4,
   * MFPKoefs[1]=4) -> period = 10 * 4 * 3.25 = 130 cycles (atari.pas:361-364). */
  int64_t od = 0;
  mfp_write_byte_at(&m, 0xFFFA1F, 10, od); /* TAD = register 15 */
  mfp_write_byte_at(&m, 0xFFFA19, 1, od);  /* TACR = register 12, mode 1 */

  assert(m.timers[0].dm == 1);
  int64_t expected_period = (int64_t)(10.0 * 4.0 * MC_BY_MFP);
  assert(m.timers[0].delay == expected_period);

  /* Not yet due. */
  int64_t next = mfp_emulate_timer(&m, 0, 1);
  assert(next == expected_period - 1);
  assert((m.reg[5] & 32) == 0); /* IPA bit for timer A not yet set */

  /* Enable the interrupt so firing has an observable IPR-bit effect. */
  mfp_write_byte_at(&m, 0xFFFA07, 32, od); /* IERA = register 3, enable bit 32 (A) */
  mfp_write_byte_at(&m, 0xFFFA13, 32, od); /* IMRA = register 9, mask bit 32 (A) */

  /* Advance exactly to expiry. */
  next = mfp_emulate_timer(&m, 0, expected_period);
  assert((m.reg[5] & 32) != 0); /* IPA bit set - timer fired */
  assert(mfp_irq_pending(&m));
  /* Always reloads (atari.pas:1423-1425): next event is one full period out. */
  assert(next == expected_period);

  int vector = mfp_ack_interrupt(&m);
  assert(vector == (0x40 | 13)); /* VCR default $40 | Timer A's V=13 */
  assert(!mfp_irq_pending(&m));  /* automatic-EOI cleared it */

  printf("test_mfp_timer_countdown_and_fire: OK (vector=%d)\n", vector);
}

static void test_mfp_live_countdown_read(void) {
  mfp m;
  mfp_init(&m);
  mfp_write_byte_at(&m, 0xFFFA1F, 100, 0); /* TAD=100 */
  mfp_write_byte_at(&m, 0xFFFA19, 1, 0);   /* TACR mode 1 */

  int64_t period = (int64_t)(100.0 * 4.0 * MC_BY_MFP);
  uint8_t cnt_start = mfp_read_byte_at(&m, 0xFFFA1F, 0);
  assert(cnt_start == 100);

  uint8_t cnt_mid = mfp_read_byte_at(&m, 0xFFFA1F, period / 2);
  assert(cnt_mid < 100 && cnt_mid > 0);

  printf("test_mfp_live_countdown_read: OK (cnt_start=%u cnt_mid=%u)\n",
         cnt_start, cnt_mid);
}

static void test_dma_sound_mono_mix(void) {
  dma_sound d;
  dma_sound_init(&d);

  uint8_t mem[0x10000];
  memset(mem, 0, sizeof(mem));
  mem[0x1000] = (uint8_t)(int8_t)100; /* first mono sample: +100 */
  mem[0x1001] = (uint8_t)(int8_t)-50; /* second mono sample: -50 */

  /* Ctrl: play(bit0)+no loop, Mode: mono(bit7), rate 0. Start/End around
   * the sample bytes above. */
  dma_sound_write_byte_at(&d, 0xFF8903, 0x00, 0); /* Start hi */
  dma_sound_write_byte_at(&d, 0xFF8905, 0x10, 0); /* Start mid */
  dma_sound_write_byte_at(&d, 0xFF8907, 0x00, 0); /* Start lo (even) */
  dma_sound_write_byte_at(&d, 0xFF890F, 0x00, 0); /* End hi */
  dma_sound_write_byte_at(&d, 0xFF8911, 0x10, 0); /* End mid */
  dma_sound_write_byte_at(&d, 0xFF8913, 0x10, 0); /* End lo */
  dma_sound_write_byte_at(&d, 0xFF8921, 0x80, 0); /* Mode: mono, rate 0 */
  dma_sound_write_byte_at(&d, 0xFF8901, 0x01, 0); /* Ctrl: play */

  assert(d.play);
  assert(d.pstart == 0x1000 && d.pend == 0x1010);

  int lev_l = 0, lev_r = 0;
  int atari_dma_level = 128; /* unity scale for a simple assertion */
  int i;
  bool saw_nonzero = false;
  for (i = 0; i < 2000 && d.play; i++) {
    /* 2000000.0 = atari.pas's AyFreq (Atari_MainClockFreqDef/16, the
     * Atari ST's own AY/YM clock) - NOT settings.pas's AY_Freq (the ZX
     * Spectrum's 1773400), a different global despite the near-identical
     * name. See tests/oracle_diff/dump_engine_state.c's run_dma comment -
     * this exact mix-up is what the oracle comparison caught. */
    dma_sound_mix(&d, mem, sizeof(mem), atari_dma_level, MC68000_FREQ,
                  2000000.0, &lev_l, &lev_r);
    if (lev_l != 0) saw_nonzero = true;
  }
  assert(saw_nonzero);
  assert(lev_l == lev_r); /* mono: both channels get the same sample */

  printf("test_dma_sound_mono_mix: OK (lev_l=%d)\n", lev_l);
}

static void test_atari_emulate_vbl_scheduling(void) {
  static uint8_t mem[0x20000];
  memset(mem, 0, sizeof(mem));

  /* Reset vectors: SSP=$2000, PC=$400. */
  mem[0] = 0x00; mem[1] = 0x00; mem[2] = 0x20; mem[3] = 0x00;
  mem[4] = 0x00; mem[5] = 0x00; mem[6] = 0x04; mem[7] = 0x00;

  /* Autovector 4 (VBL, level 4 -> vector 24+4=28) table entry at
   * 28*4=$70, pointing at the handler below. */
  mem[0x70] = 0x00; mem[0x71] = 0x00; mem[0x72] = 0x05; mem[0x73] = 0x00;

  /* Main: MOVE.W #$2000,SR (unmask all interrupt levels) ; BRA.S * (tight
   * loop, keeps the CPU busy so VBL scheduling is exercised, not
   * instruction-count-limited). */
  mem[0x400] = 0x46; mem[0x401] = 0xFC; mem[0x402] = 0x20; mem[0x403] = 0x00;
  mem[0x404] = 0x60; mem[0x405] = 0xFE;

  /* Handler at $500: ADDQ.B #1,$3000.L ; RTE */
  mem[0x500] = 0x52; mem[0x501] = 0x39;
  mem[0x502] = 0x00; mem[0x503] = 0x00; mem[0x504] = 0x30; mem[0x505] = 0x00;
  mem[0x506] = 0x4E; mem[0x507] = 0x73;

  ay_engine ay;
  ay_engine_init(&ay);

  atari_emulate a;
  atari_emulate_init(&a, mem, sizeof(mem), &ay, MC68000_FREQ, 1000,
                      2000000.0 /* atari.pas's AyFreq, not settings.pas's AY_Freq */);
  a.tick_count_max = 1000000;
  a.do_loop = true;

  int i;
  for (i = 0; i < 200; i++) {
    atari_emulate_step(&a);
  }

  printf("test_atari_emulate_vbl_scheduling: OK (tick_count=%lld, counter=%u)\n",
         (long long)a.tick_count, mem[0x3000]);
  assert(a.tick_count > 0);
  assert(mem[0x3000] == (uint8_t)a.tick_count);
}

int main(void) {
  test_mfp_timer_countdown_and_fire();
  test_mfp_live_countdown_read();
  test_dma_sound_mono_mix();
  test_atari_emulate_vbl_scheduling();
  printf("All atari smoke tests passed.\n");
  return 0;
}
