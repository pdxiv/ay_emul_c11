/* Register/mixer regression tests for engine/src/ay.c. See README.md. */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ay_engine/ay.h"

static void test_register_masking(void) {
  ay_chip chip;
  memset(&chip, 0, sizeof(chip));

  /* AY.pas:429: SetAmplA(Value and 31) - value masked to 5 bits, and bit 4
   * (0x10) toggles Envelope_EnA. */
  ay_chip_set_ay_register(&chip, 8, 0xFF);
  assert(chip.reg[8] == 0x1F); /* 0xFF & 31 */
  assert(!chip.envelope_en_a); /* bit 4 set -> envelope disabled */

  ay_chip_set_ay_register(&chip, 8, 0x0F);
  assert(chip.reg[8] == 0x0F);
  assert(chip.envelope_en_a); /* bit 4 clear -> envelope enabled */

  /* AY.pas:424-425: registers 1,3,5 (coarse tone) masked to 4 bits. */
  ay_chip_set_ay_register(&chip, 1, 0xFF);
  assert(chip.reg[1] == 0x0F);

  /* AY.pas:432-433: registers 0,2,4,11,12 unmasked. */
  ay_chip_set_ay_register(&chip, 0, 0xAB);
  assert(chip.reg[0] == 0xAB);

  printf("test_register_masking: OK\n");
}

static void test_envelope_dispatch(void) {
  ay_chip chip;
  memset(&chip, 0, sizeof(chip));

  /* AY.pas:389-398: value 8 selects Case_EnvType_8, a free-running sawtooth
   * that just decrements-and-wraps every envelope tick with no
   * first_period gating. */
  ay_chip_set_ay_register(&chip, 13, 8);
  int start = chip.ampl;
  ay_chip_synthesizer_logic_q(&chip);
  /* Envelope_Counter starts at 0, so Case_EnvType fires on the very first
   * logic tick (AY.pas:363: `if Envelope_Counter.Hi = 0 then
   * Case_EnvType`). */
  assert(chip.ampl == ((start - 1) & 31));

  printf("test_envelope_dispatch: OK\n");
}

static void test_noise_val_is_boolean_gate(void) {
  ay_chip chip;
  memset(&chip, 0, sizeof(chip));
  ay_chip_reset(&chip, true); /* Noise.Seed := $ffff */
  int i;
  for (i = 0; i < 200; i++) {
    uint32_t v = ay_noise_val(&chip);
    assert(v == 0 || v == 1);
    chip.noise_seed = (chip.noise_seed << 1 | 1) ^
                       ((chip.noise_seed >> 16) ^ ((chip.noise_seed >> 13) & 1));
    chip.noise_seed &= 0x1ffffu;
  }
  printf("test_noise_val_is_boolean_gate: OK\n");
}

static void test_end_to_end_tone_is_audible(void) {
  ay_engine e;
  int16_t buf[512 * 2];
  ay_engine_init(&e);

  /* Program: tone A on, full amplitude, a period short enough to toggle
   * within the buffer window. */
  ay_chip_set_ay_register(&e.chip, 0, 64); /* TonA low byte -> period 64 */
  ay_chip_set_ay_register(&e.chip, 1, 0);  /* TonA high byte */
  ay_chip_set_ay_register(&e.chip, 7, 0x3E); /* mixer: tone A enabled only */
  ay_chip_set_ay_register(&e.chip, 8, 15);   /* amplitude A, no envelope */

  e.chip_type = AY_CHIP_TYPE_AY;
  ay_engine_calculate_level_tables(&e);

  /* Drive Synthesizer_Stereo16 directly with a simple 1 logic-tick : 1
   * sample pacing (delay_in_tiks = 0x10000, "1.0" in the 16.16 fixed point
   * Tik/Tick_Counter use - see ay.h's tick_counter_hi comment), and 5000
   * whole ticks requested (Number_Of_Tiks.Hi, not the full fixed-point
   * value - see number_of_tiks_hi() in ay.c) - comfortably more than the
   * 512-sample buffer and more than tone A's 64-tick period, so the buffer
   * fills before the tone-A toggle stops mattering. This bypasses
   * ay_synthesizer_ay()'s real-world sample-rate/AY-clock calibration
   * (FrqAyByFrqZ80/Delay_In_Tiks, computed from settings.pas/MainWin.pas
   * constants not ported this milestone) to keep the test deterministic. */
  e.delay_in_tiks = 0x10000;
  e.tik_re = e.delay_in_tiks;
  e.number_of_tiks = ((int64_t)5000) << 32;
  e.buf = buf;
  e.buf_len = 0;
  e.buffer_length = 512;

  ay_synthesizer_stereo16(&e);

  assert(e.buf_len > 0);
  bool any_nonzero = false;
  int i;
  for (i = 0; i < e.buf_len; i++) {
    int16_t l = buf[i * 2];
    int16_t r = buf[i * 2 + 1];
    assert(!isnan((double)l) && !isnan((double)r));
    if (l != 0 || r != 0) any_nonzero = true;
  }
  assert(any_nonzero && "expected audible output from a full-amplitude tone");

  printf("test_end_to_end_tone_is_audible: OK (%d samples generated)\n",
         e.buf_len);
}

static void test_8bit_output_is_audible(void) {
  ay_engine e;
  uint8_t buf[512 * 2];
  ay_engine_init(&e);

  ay_chip_set_ay_register(&e.chip, 0, 64);
  ay_chip_set_ay_register(&e.chip, 1, 0);
  ay_chip_set_ay_register(&e.chip, 7, 0x3E);
  ay_chip_set_ay_register(&e.chip, 8, 15);

  e.chip_type = AY_CHIP_TYPE_AY;
  e.sample_bits = 8;
  ay_engine_calculate_level_tables(&e);

  e.delay_in_tiks = 0x10000;
  e.tik_re = e.delay_in_tiks;
  e.number_of_tiks = ((int64_t)5000) << 32;
  e.buf = buf;
  e.buf_len = 0;
  e.buffer_length = 512;

  ay_synthesizer_stereo8(&e);

  assert(e.buf_len > 0);
  bool any_not_128 = false; /* 128 = silence for unsigned 8-bit PCM */
  int i;
  for (i = 0; i < e.buf_len; i++) {
    if (buf[i * 2] != 128 || buf[i * 2 + 1] != 128) any_not_128 = true;
  }
  assert(any_not_128 && "expected audible output from a full-amplitude tone");

  printf("test_8bit_output_is_audible: OK (%d samples generated)\n",
         e.buf_len);
}

int main(void) {
  test_register_masking();
  test_envelope_dispatch();
  test_noise_val_is_boolean_gate();
  test_end_to_end_tone_is_audible();
  test_8bit_output_is_audible();
  printf("All ay smoke tests passed.\n");
  return 0;
}
