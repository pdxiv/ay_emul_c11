/* C11 port of ay_emul/AY.pas - see engine/include/ay_engine/ay.h for scope
 * notes and what's deliberately not ported in this milestone. */
#include "ay_engine/ay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const uint16_t AY_AMPLITUDES_AY[16] = {
    0, 836, 1212, 1773, 2619, 3875, 5397, 8823,
    10392, 16706, 23339, 29292, 36969, 46421, 55195, 65535};

const uint16_t AY_AMPLITUDES_YM[32] = {
    0x0000, 0x0000, 0x00F8, 0x01C2, 0x029E, 0x033A, 0x03F2, 0x04D7,
    0x0610, 0x077F, 0x090A, 0x0A42, 0x0C3B, 0x0EC2, 0x1137, 0x13A7,
    0x1750, 0x1BF9, 0x20DF, 0x2596, 0x2C9D, 0x3579, 0x3E55, 0x4768,
    0x54FF, 0x6624, 0x773B, 0x883F, 0xA1DA, 0xC0FC, 0xE094, 0xFFFF};

/* Case_EnvType selector tags, mirroring AY.pas's SetEnvelopeRegister case
 * dispatch (AY.pas:389-398). */
enum {
  ENV_CASE_0_3_9 = 0,
  ENV_CASE_4_7_15 = 1,
  ENV_CASE_8 = 2,
  ENV_CASE_10 = 3,
  ENV_CASE_11 = 4,
  ENV_CASE_12 = 5,
  ENV_CASE_13 = 6,
  ENV_CASE_14 = 7
};

uint16_t ay_reg_tona(const ay_chip* c) {
  return (uint16_t)(c->reg[0] | (c->reg[1] << 8));
}
uint16_t ay_reg_tonb(const ay_chip* c) {
  return (uint16_t)(c->reg[2] | (c->reg[3] << 8));
}
uint16_t ay_reg_tonc(const ay_chip* c) {
  return (uint16_t)(c->reg[4] | (c->reg[5] << 8));
}
uint16_t ay_reg_envelope(const ay_chip* c) {
  return (uint16_t)(c->reg[11] | (c->reg[12] << 8));
}
uint32_t ay_noise_val(const ay_chip* c) {
  /* Noise.Val's low word mirrors Noise.Seed's high word; NoiseGenerator
   * bounds Seed to 17 bits, so this is always 0 or 1 - see ay.h. */
  return (c->noise_seed >> 16) & 0xFFFFu;
}

/* AY.pas:318-334, non-asm fallback (the only path live in any buildable
 * configuration - same situation as the Z80 core, see FIDELITY_GATE.md). */
static uint32_t ay_noise_generator(uint32_t seed) {
  /* Pascal: `and` binds tighter than `xor`, so this is
   * (Seed shr 16) xor ((Seed shr 13) and 1) - parenthesized explicitly here
   * (C's operator precedence already agrees, but make it unambiguous). */
  return (uint32_t)((((seed << 1) | 1) ^ ((seed >> 16) ^ ((seed >> 13) & 1))) &
                     0x1ffffu);
}

/* AY.pas:216-316, TSoundChip.Case_EnvType_* */
static void env_case_0_3_9(ay_chip* c) {
  if (c->first_period) {
    c->ampl--;
    if (c->ampl == 0) c->first_period = false;
  }
}
static void env_case_4_7_15(ay_chip* c) {
  if (c->first_period) {
    c->ampl++;
    if (c->ampl == 32) {
      c->first_period = false;
      c->ampl = 0;
    }
  }
}
static void env_case_8(ay_chip* c) { c->ampl = (c->ampl - 1) & 31; }
static void env_case_10(ay_chip* c) {
  if (c->first_period) {
    c->ampl--;
    if (c->ampl < 0) {
      c->first_period = false;
      c->ampl = 0;
    }
  } else {
    c->ampl++;
    if (c->ampl == 32) {
      c->first_period = true;
      c->ampl = 31;
    }
  }
}
static void env_case_11(ay_chip* c) {
  if (c->first_period) {
    c->ampl--;
    if (c->ampl < 0) {
      c->first_period = false;
      c->ampl = 31;
    }
  }
}
static void env_case_12(ay_chip* c) { c->ampl = (c->ampl + 1) & 31; }
static void env_case_13(ay_chip* c) {
  if (c->first_period) {
    c->ampl++;
    if (c->ampl == 32) {
      c->first_period = false;
      c->ampl = 31;
    }
  }
}
static void env_case_14(ay_chip* c) {
  if (!c->first_period) {
    c->ampl--;
    if (c->ampl < 0) {
      c->first_period = true;
      c->ampl = 0;
    }
  } else {
    c->ampl++;
    if (c->ampl == 32) {
      c->first_period = false;
      c->ampl = 31;
    }
  }
}

static void ay_chip_case_env_type(ay_chip* c) {
  switch (c->env_type_case) {
    case ENV_CASE_0_3_9: env_case_0_3_9(c); break;
    case ENV_CASE_4_7_15: env_case_4_7_15(c); break;
    case ENV_CASE_8: env_case_8(c); break;
    case ENV_CASE_10: env_case_10(c); break;
    case ENV_CASE_11: env_case_11(c); break;
    case ENV_CASE_12: env_case_12(c); break;
    case ENV_CASE_13: env_case_13(c); break;
    case ENV_CASE_14: env_case_14(c); break;
  }
}

/* AY.pas:380-399, TSoundChip.SetEnvelopeRegister */
static void ay_chip_set_envelope_register(ay_chip* c, uint8_t value) {
  c->envelope_counter = 0;
  c->first_period = true;
  c->ampl = ((value & 4) == 0) ? 32 : -1;
  c->reg[13] = value;
  if (value <= 3 || value == 9) {
    c->env_type_case = ENV_CASE_0_3_9;
  } else if ((value >= 4 && value <= 7) || value == 15) {
    c->env_type_case = ENV_CASE_4_7_15;
  } else if (value == 8) {
    c->env_type_case = ENV_CASE_8;
  } else if (value == 10) {
    c->env_type_case = ENV_CASE_10;
  } else if (value == 11) {
    c->env_type_case = ENV_CASE_11;
  } else if (value == 12) {
    c->env_type_case = ENV_CASE_12;
  } else if (value == 13) {
    c->env_type_case = ENV_CASE_13;
  } else if (value == 14) {
    c->env_type_case = ENV_CASE_14;
  }
}

/* AY.pas:369-378, TSoundChip.SetMixerRegister */
static void ay_chip_set_mixer_register(ay_chip* c, uint8_t value) {
  c->reg[7] = value;
  c->ton_en_a = (value & 1) == 0;
  c->noise_en_a = (value & 8) == 0;
  c->ton_en_b = (value & 2) == 0;
  c->noise_en_b = (value & 16) == 0;
  c->ton_en_c = (value & 4) == 0;
  c->noise_en_c = (value & 32) == 0;
}

/* AY.pas:401-417, TSoundChip.SetAmplA/B/C */
static void ay_chip_set_ampl_a(ay_chip* c, uint8_t value) {
  c->reg[8] = value;
  c->envelope_en_a = (value & 16) == 0;
}
static void ay_chip_set_ampl_b(ay_chip* c, uint8_t value) {
  c->reg[9] = value;
  c->envelope_en_b = (value & 16) == 0;
}
static void ay_chip_set_ampl_c(ay_chip* c, uint8_t value) {
  c->reg[10] = value;
  c->envelope_en_c = (value & 16) == 0;
}

void ay_chip_set_ay_register(ay_chip* c, int num, uint8_t value) {
  switch (num) {
    case 13: ay_chip_set_envelope_register(c, (uint8_t)(value & 15)); break;
    case 1: case 3: case 5: c->reg[num] = (uint8_t)(value & 15); break;
    case 6: c->reg[6] = (uint8_t)(value & 31); break;
    case 7: ay_chip_set_mixer_register(c, (uint8_t)(value & 63)); break;
    case 8: ay_chip_set_ampl_a(c, (uint8_t)(value & 31)); break;
    case 9: ay_chip_set_ampl_b(c, (uint8_t)(value & 31)); break;
    case 10: ay_chip_set_ampl_c(c, (uint8_t)(value & 31)); break;
    case 0: case 2: case 4: case 11: case 12: c->reg[num] = value; break;
    default: break;
  }
}

void ay_chip_set_ay_register_fast(ay_chip* c, int num, uint8_t value) {
  switch (num) {
    case 13: ay_chip_set_envelope_register(c, value); break;
    case 1: case 3: case 5: c->reg[num] = value; break;
    case 6: c->reg[6] = value; break;
    case 7: ay_chip_set_mixer_register(c, value); break;
    case 8: ay_chip_set_ampl_a(c, value); break;
    case 9: ay_chip_set_ampl_b(c, value); break;
    case 10: ay_chip_set_ampl_c(c, value); break;
    case 0: case 2: case 4: case 11: case 12: c->reg[num] = value; break;
    default: break;
  }
}

/* AY.pas:336-367, TSoundChip.Synthesizer_Logic_Q */
void ay_chip_synthesizer_logic_q(ay_chip* c) {
  c->ton_counter_a++;
  if (c->ton_counter_a >= ay_reg_tona(c)) {
    c->ton_counter_a = 0;
    c->ton_a ^= 1;
  }
  c->ton_counter_b++;
  if (c->ton_counter_b >= ay_reg_tonb(c)) {
    c->ton_counter_b = 0;
    c->ton_b ^= 1;
  }
  c->ton_counter_c++;
  if (c->ton_counter_c >= ay_reg_tonc(c)) {
    c->ton_counter_c = 0;
    c->ton_c ^= 1;
  }
  c->noise_counter++;
  if ((c->noise_counter & 1) == 0 &&
      c->noise_counter >= ((uint32_t)c->reg[6] << 1)) {
    c->noise_counter = 0;
    c->noise_seed = ay_noise_generator(c->noise_seed);
  }
  if (c->envelope_counter == 0) ay_chip_case_env_type(c);
  c->envelope_counter++;
  if (c->envelope_counter >= ay_reg_envelope(c)) c->envelope_counter = 0;
}

void ay_chip_reset(ay_chip* c, bool zeroregs) {
  if (zeroregs) {
    memset(c->reg, 0, 14);
    ay_chip_set_envelope_register(c, 0);
    ay_chip_set_mixer_register(c, 0);
    ay_chip_set_ampl_a(c, 0);
    ay_chip_set_ampl_b(c, 0);
    ay_chip_set_ampl_c(c, 0);
    c->current_register_ay = 0;
  }
  c->first_period = false;
  c->ampl = 0;
  c->envelope_counter = 0;
  c->ton_counter_a = 0;
  c->ton_counter_b = 0;
  c->ton_counter_c = 0;
  c->noise_counter = 0;
  c->ton_a = 0;
  c->ton_b = 0;
  c->ton_c = 0;
  c->noise_seed = 0xffff;
}

/* AY.pas:455-517, ApplyFilter, cpu64/non-asm fallback (the only path live in
 * any buildable configuration of the original - see FIDELITY_GATE.md for
 * the analogous finding on the Z80 core). */
int ay_apply_filter(int lev, int* filt_x, const int* filt_k, int filt_m,
                     int* filt_i) {
  int64_t res;
  int j;
  filt_x[*filt_i] = lev;
  res = (int64_t)lev * filt_k[0];
  for (j = 1; j <= filt_m; j++) {
    if (*filt_i > 0) {
      (*filt_i)--;
    } else {
      *filt_i = filt_m;
    }
    res += (int64_t)filt_x[*filt_i] * filt_k[j];
  }
  return (int)(res / 0x1000000);
}

void ay_engine_free_filter(ay_engine* e) {
  free(e->filt_k);
  free(e->filt_xl);
  free(e->filt_xr);
  e->filt_k = NULL;
  e->filt_xl = NULL;
  e->filt_xr = NULL;
  e->filt_m = 0;
  e->filt_i = 0;
}

/* MainWin.pas:5073-5168, TFrmMain.CalcFiltKoefs/SetFilter - Hamming-windowed
 * sinc lowpass FIR design. See the ay_engine_set_filter declaration in
 * ay.h for the filter_quality/is_filt semantics. */
void ay_engine_set_filter(ay_engine* e, int filter_quality, int ay_freq,
                          int sample_rate) {
  int is_filt, tap_count, i;
  double c, cutoff, filt_m2, k_sum;
  double* fkt;

  ay_engine_free_filter(e);

  if (filter_quality == 0 || sample_rate >= ay_freq / 8) {
    e->is_filt = -1;
    return;
  }

  is_filt = 0;
  c = 22050.0;
  if (sample_rate >= 44100) {
    c = sample_rate / 2.0;
    is_filt = 1;
  }

  tap_count = (int)lround(3.3 / (c - 9200.0) * (ay_freq / 8));
  if ((int64_t)ay_freq * tap_count > (int64_t)3500000 * 50) {
    /* 90% CPU-budget heuristic for a Celeron 850MHz, carried over as-is
     * (AY.pas/MainWin.pas comment: "90% of usage for my Celeron 850 MHz"). */
    tap_count = (int)lround(3500000.0 * 50.0 / ay_freq);
    is_filt = 0;
  }
  if (tap_count < 1) tap_count = 1;

  cutoff = M_PI * (9200.0 + c) / (ay_freq / 8);
  fkt = malloc(sizeof(double) * (size_t)tap_count);
  filt_m2 = (tap_count - 1) / 2.0;
  k_sum = 0.0;
  for (i = 0; i < tap_count; i++) {
    double i2 = i - filt_m2;
    double f;
    if (i2 == 0.0) {
      f = cutoff;
    } else {
      f = sin(cutoff * i2) / i2 *
          (0.54 + 0.46 * cos(2.0 * M_PI / tap_count * i2));
    }
    fkt[i] = f;
    k_sum += f;
  }

  e->filt_k = malloc(sizeof(int) * (size_t)tap_count);
  for (i = 0; i < tap_count; i++) {
    e->filt_k[i] = (int)lround(fkt[i] / k_sum * 0x1000000);
  }
  free(fkt);

  /* AY.pas: Dec(Filt_M) - Filt_M as used by ApplyFilter/the rest of the
   * engine is one less than the coefficient/tap count; filt_xl/filt_xr are
   * sized filt_m+1 to match (AY.pas: SetLength(Filt_XL, Filt_M+1) is called
   * with Filt_M already decremented). */
  e->filt_m = tap_count - 1;
  e->filt_xl = calloc((size_t)tap_count, sizeof(int));
  e->filt_xr = calloc((size_t)tap_count, sizeof(int));
  e->filt_i = 0;
  e->is_filt = is_filt;
}

/* AY.pas:519-577, TSoundChip.Synthesizer_Mixer_Q (stereo path). */
static void ay_chip_synthesizer_mixer_q(ay_chip* c, const ay_engine* e,
                                         int* level_l, int* level_r) {
  int lev_l = e->beeper;
  int lev_r = lev_l;
  int k;
  if (e->on_mix_dma != NULL) e->on_mix_dma(e->on_mix_dma_userdata, &lev_l, &lev_r);

  k = 1;
  if (c->ton_en_a) k = c->ton_a;
  if (c->noise_en_a) k &= (int)ay_noise_val(c);
  if (k != 0) {
    if (c->envelope_en_a) {
      lev_l += e->level_al[c->reg[8] * 2 + 1];
      lev_r += e->level_ar[c->reg[8] * 2 + 1];
    } else {
      lev_l += e->level_al[c->ampl];
      lev_r += e->level_ar[c->ampl];
    }
  }

  k = 1;
  if (c->ton_en_b) k = c->ton_b;
  if (c->noise_en_b) k &= (int)ay_noise_val(c);
  if (k != 0) {
    if (c->envelope_en_b) {
      lev_l += e->level_bl[c->reg[9] * 2 + 1];
      lev_r += e->level_br[c->reg[9] * 2 + 1];
    } else {
      lev_l += e->level_bl[c->ampl];
      lev_r += e->level_br[c->ampl];
    }
  }

  k = 1;
  if (c->ton_en_c) k = c->ton_c;
  if (c->noise_en_c) k &= (int)ay_noise_val(c);
  if (k != 0) {
    if (c->envelope_en_c) {
      lev_l += e->level_cl[c->reg[10] * 2 + 1];
      lev_r += e->level_cr[c->reg[10] * 2 + 1];
    } else {
      lev_l += e->level_cl[c->ampl];
      lev_r += e->level_cr[c->ampl];
    }
  }

  *level_l += lev_l;
  *level_r += lev_r;
}

/* AY.pas:772-808, TSoundChip.Synthesizer_Mixer_Q_Mono. */
static void ay_chip_synthesizer_mixer_q_mono(ay_chip* c, const ay_engine* e,
                                              int* level_l) {
  int lev = e->beeper;
  int k;
  if (e->on_mix_dma != NULL) {
    int dma_r = 0;
    e->on_mix_dma(e->on_mix_dma_userdata, &lev, &dma_r);
    lev += dma_r;
  }

  k = 1;
  if (c->ton_en_a) k = c->ton_a;
  if (c->noise_en_a) k &= (int)ay_noise_val(c);
  if (k != 0) {
    lev += c->envelope_en_a ? e->level_al[c->reg[8] * 2 + 1]
                             : e->level_al[c->ampl];
  }

  k = 1;
  if (c->ton_en_b) k = c->ton_b;
  if (c->noise_en_b) k &= (int)ay_noise_val(c);
  if (k != 0) {
    lev += c->envelope_en_b ? e->level_bl[c->reg[9] * 2 + 1]
                             : e->level_bl[c->ampl];
  }

  k = 1;
  if (c->ton_en_c) k = c->ton_c;
  if (c->noise_en_c) k &= (int)ay_noise_val(c);
  if (k != 0) {
    lev += c->envelope_en_c ? e->level_cl[c->reg[10] * 2 + 1]
                             : e->level_cl[c->ampl];
  }

  *level_l += lev;
}

void ay_engine_init(ay_engine* e) {
  memset(e, 0, sizeof(*e));
  e->chip_type = AY_CHIP_TYPE_YM; /* AY.pas: ChType default */
  e->pre_amp = 127;               /* AY.pas: PreAmpDef */
  e->pre_amp_max = 255;
  e->number_of_channels = 2;
  e->sample_bits = 16;
  e->beeper_max = 146; /* settings.pas: BeeperMaxDef */
  e->atari_dma_max = 0;
  /* settings.pas defaults for "ABC stereo" panning (MainWin.pas:923-924). */
  e->index_al = 255;
  e->index_ar = 13;
  e->index_bl = 170;
  e->index_br = 170;
  e->index_cl = 13;
  e->index_cr = 255;
  e->is_filt = -1; /* filtering disabled until the ALSA/mixer milestone wires
                     * up real Filt_K coefficients - see migration_debt.yaml
                     * and the is_filt field comment in ay.h. */
  ay_engine_reset_chip(e, true);
}

/* AY.pas:904-940, ResetAYChipEmulation (single-chip case only, see ay.h). */
void ay_engine_reset_chip(ay_engine* e, bool zeroregs) {
  ay_chip_reset(&e->chip, zeroregs);
  e->prev_left = 0;
  e->prev_right = 0;
  e->left_chan = 0;
  e->right_chan = 0;
  e->left_chan1 = 0;
  e->right_chan1 = 0;
  e->tick_counter_hi = 0;
  e->tik_re = e->delay_in_tiks;
  e->number_of_tiks = 0;
  e->current_tik = 0;
  e->int_flag = false;
}

/* AY.pas:942-1008, Calculate_Level_Tables (single-chip / no Turbosound,
 * see ay.h for scope). */
void ay_engine_calculate_level_tables(ay_engine* e) {
  int index_a, index_b, index_c;
  int l, r;
  double k;
  int i, b;

  if (e->number_of_channels == 2) {
    index_a = e->index_al;
    index_b = e->index_bl;
    index_c = e->index_cl;
    l = e->index_al + e->index_bl + e->index_cl + e->atari_dma_max;
    r = e->index_ar + e->index_br + e->index_cr + e->atari_dma_max;
    if (l < r) l = r;
    r = (e->index_al + e->index_bl + e->index_cl) * 2;
    if (l < r) l = r;
    r = (e->index_ar + e->index_br + e->index_cr) * 2;
    if (l < r) l = r;
  } else {
    index_a = e->index_al + e->index_ar;
    index_b = e->index_bl + e->index_br;
    index_c = e->index_cl + e->index_cr;
    l = index_a + index_b + index_c + e->atari_dma_max;
    r = (index_a + index_b + index_c) * 2;
    if (l < r) l = r;
  }
  if (l == 0) l++;
  r = (e->sample_bits == 8) ? 127 : 32767;
  k = (double)e->pre_amp / (double)e->pre_amp_max * 2.0;

  if (e->chip_type == AY_CHIP_TYPE_AY) {
    for (i = 0; i < 16; i++) {
      b = (int)((double)index_a / l * AY_AMPLITUDES_AY[i] / 65535.0 * r * k +
                0.5);
      e->level_al[i * 2] = b;
      e->level_al[i * 2 + 1] = b;
      b = (int)((double)e->index_ar / l * AY_AMPLITUDES_AY[i] / 65535.0 * r *
                    k +
                0.5);
      e->level_ar[i * 2] = b;
      e->level_ar[i * 2 + 1] = b;
      b = (int)((double)index_b / l * AY_AMPLITUDES_AY[i] / 65535.0 * r * k +
                0.5);
      e->level_bl[i * 2] = b;
      e->level_bl[i * 2 + 1] = b;
      b = (int)((double)e->index_br / l * AY_AMPLITUDES_AY[i] / 65535.0 * r *
                    k +
                0.5);
      e->level_br[i * 2] = b;
      e->level_br[i * 2 + 1] = b;
      b = (int)((double)index_c / l * AY_AMPLITUDES_AY[i] / 65535.0 * r * k +
                0.5);
      e->level_cl[i * 2] = b;
      e->level_cl[i * 2 + 1] = b;
      b = (int)((double)e->index_cr / l * AY_AMPLITUDES_AY[i] / 65535.0 * r *
                    k +
                0.5);
      e->level_cr[i * 2] = b;
      e->level_cr[i * 2 + 1] = b;
    }
  } else {
    for (i = 0; i < 32; i++) {
      e->level_al[i] = (int)((double)index_a / l * AY_AMPLITUDES_YM[i] /
                                  65535.0 * r * k +
                              0.5);
      e->level_ar[i] = (int)((double)e->index_ar / l * AY_AMPLITUDES_YM[i] /
                                  65535.0 * r * k +
                              0.5);
      e->level_bl[i] = (int)((double)index_b / l * AY_AMPLITUDES_YM[i] /
                                  65535.0 * r * k +
                              0.5);
      e->level_br[i] = (int)((double)e->index_br / l * AY_AMPLITUDES_YM[i] /
                                  65535.0 * r * k +
                              0.5);
      e->level_cl[i] = (int)((double)index_c / l * AY_AMPLITUDES_YM[i] /
                                  65535.0 * r * k +
                              0.5);
      e->level_cr[i] = (int)((double)e->index_cr / l * AY_AMPLITUDES_YM[i] /
                                  65535.0 * r * k +
                              0.5);
    }
  }
  e->beeper_level = -(int)((double)e->beeper_max / l * r * k + 0.5);
  /* AY.pas:1007 */
  e->atari_dma_level = (int)((double)e->atari_dma_max / l * r * k + 0.5);
}

/* AY.pas:1058-1072, SynthesizerAY, cpu64/non-asm fallback (the only path
 * live in any buildable configuration of the original). Takes the current
 * Z80 tact explicitly rather than reading a Z80-core global - see ay.h. */
void ay_synthesizer_ay(ay_engine* e, int64_t current_tact) {
  if (!e->int_flag) {
    int64_t tmp = e->number_of_tiks +
                  (current_tact - e->previous_tact) * e->frq_ay_by_frq_z80;
    if ((tmp >> 32) == 0) return; /* Pascal: if hi(tmp) = 0 then exit */
    e->number_of_tiks = tmp;
    e->previous_tact = current_tact;
  } else {
    e->int_flag = false;
  }
  /* AY.pas dispatches through a `Synthesizer` proc-pointer set by the
   * caller to one of Stereo16/Stereo8/Mono16/Mono8 depending on output
   * format; the dispatch is inlined here instead of a function-pointer
   * table. e->buf == NULL lets standalone callers (e.g. this milestone's
   * fidelity/differential harnesses) drive ay_synthesizer_ay for its tick
   * bookkeeping alone, without an output buffer wired up yet. */
  if (e->buf != NULL) {
    if (e->sample_bits == 16 && e->number_of_channels == 2) {
      ay_synthesizer_stereo16(e);
    } else if (e->sample_bits == 16) {
      ay_synthesizer_mono16(e);
    } else if (e->sample_bits == 8 && e->number_of_channels == 2) {
      ay_synthesizer_stereo8(e);
    } else if (e->sample_bits == 8) {
      ay_synthesizer_mono8(e);
    }
  }
}

static int ay_interpolator16(int l1, int l0, int64_t ofs) {
  int result = (int)(((int64_t)(l1 - l0) * ofs) / 65536 + l0);
  if (result > 32767) result = 32767;
  else if (result < -32768) result = -32768;
  return result;
}

/* AY.pas's Tick_Counter.Re always equals Tick_Counter.Hi << 16 - see ay.h. */
static int64_t tick_counter_re(const ay_engine* e) {
  return (int64_t)e->tick_counter_hi << 16;
}

/* Number_Of_Tiks is a packed variant (Lo:longword;Hi:longword) aliased as
 * Re:int64 - AY.pas:1029-1073 accumulates the full .Re value (fractional
 * fixed-point remainder in Lo, whole-tick count in Hi), but
 * Synthesizer_Stereo16/Mono16's outer loop bound and IntFlag check compare
 * against `.Hi` only (AY.pas:676,707 etc: "until Current_Tik >=
 * Number_Of_Tiks.Hi") - i.e. only the whole-tick count, not the fractional
 * remainder. */
static uint32_t number_of_tiks_hi(const ay_engine* e) {
  return (uint32_t)((uint64_t)e->number_of_tiks >> 32);
}

static int ay_averager16(int64_t l, uint32_t divisor) {
  int result = (int)(l / (int64_t)divisor);
  if (result > 32767) result = 32767;
  else if (result < -32768) result = -32768;
  return result;
}

/* AY.pas:623-630, Interpolator8/Averager8 - same shape as the 16-bit
 * versions but biased to unsigned 0..255 (AY.pas: "+ 128", clamped 0..255). */
static int ay_interpolator8(int l1, int l0, int64_t ofs) {
  int result = (int)(((int64_t)(l1 - l0) * ofs) / 65536 + l0 + 128);
  if (result > 255) result = 255;
  else if (result < 0) result = 0;
  return result;
}

static int ay_averager8(int64_t l, uint32_t divisor) {
  int result = (int)(128 + l / (int64_t)divisor);
  if (result > 255) result = 255;
  else if (result < 0) result = 0;
  return result;
}

/* AY.pas:650-709, Synthesizer_Stereo16. */
void ay_synthesizer_stereo16(ay_engine* e) {
  int16_t* out = (int16_t*)e->buf;
  do {
    int level_l = 0, level_r = 0;
    if (tick_counter_re(e) >= e->tik_re) {
      do {
        int16_t l16, r16;
        if (e->is_filt > 0) {
          int64_t tmp = e->tik_re - tick_counter_re(e) + 65536;
          l16 = (int16_t)ay_interpolator16(e->left_chan, e->prev_left, tmp);
          r16 = (int16_t)ay_interpolator16(e->right_chan, e->prev_right, tmp);
        } else {
          l16 = (int16_t)ay_averager16(e->left_chan1, e->tick_counter_hi);
          r16 = (int16_t)ay_averager16(e->right_chan1, e->tick_counter_hi);
        }
        out[e->buf_len * 2] = l16;
        out[e->buf_len * 2 + 1] = r16;
        e->tik_re += e->delay_in_tiks;
        e->buf_len++;
        if (e->buf_len == e->buffer_length) {
          if (e->current_tik < number_of_tiks_hi(e)) e->int_flag = true;
          return;
        }
      } while (!(tick_counter_re(e) < e->tik_re));
      e->tik_re -= tick_counter_re(e);
      e->left_chan1 = 0;
      e->right_chan1 = 0;
      /* AY.pas:682 `Tick_Counter.Re := Tmp(=0)` - resets the whole Re value,
       * which since only .Hi is ever nonzero (see ay.h) means resetting
       * tick_counter_hi itself, not just some separate "Re" accumulator. */
      e->tick_counter_hi = 0;
    }
    ay_chip_synthesizer_logic_q(&e->chip);
    ay_chip_synthesizer_mixer_q(&e->chip, e, &level_l, &level_r);
    if (e->is_filt >= 0) {
      int saved_i = e->filt_i;
      level_l = ay_apply_filter(level_l, e->filt_xl, e->filt_k, e->filt_m,
                                 &e->filt_i);
      e->filt_i = saved_i;
      level_r = ay_apply_filter(level_r, e->filt_xr, e->filt_k, e->filt_m,
                                 &e->filt_i);
    }
    e->prev_left = e->left_chan;
    e->left_chan = level_l;
    e->left_chan1 += level_l;
    e->prev_right = e->right_chan;
    e->right_chan = level_r;
    e->right_chan1 += level_r;

    e->current_tik++;
    e->tick_counter_hi++;
  } while (!((uint32_t)e->current_tik >= number_of_tiks_hi(e)));
  e->number_of_tiks = 0;
  e->current_tik = 0;
}

/* AY.pas:810-855, Synthesizer_Mono16. */
void ay_synthesizer_mono16(ay_engine* e) {
  int16_t* out = (int16_t*)e->buf;
  do {
    int level_l = 0;
    if (tick_counter_re(e) >= e->tik_re) {
      do {
        int16_t m16;
        if (e->is_filt > 0) {
          int64_t tmp = e->tik_re - tick_counter_re(e) + 65536;
          m16 = (int16_t)ay_interpolator16(e->left_chan, e->prev_left, tmp);
        } else {
          m16 = (int16_t)ay_averager16(e->left_chan1, e->tick_counter_hi);
        }
        out[e->buf_len] = m16;
        e->tik_re += e->delay_in_tiks;
        e->buf_len++;
        if (e->buf_len == e->buffer_length) {
          if (e->current_tik < number_of_tiks_hi(e)) e->int_flag = true;
          return;
        }
      } while (!(tick_counter_re(e) < e->tik_re));
      e->tik_re -= tick_counter_re(e);
      e->left_chan1 = 0;
      e->tick_counter_hi = 0;
    }
    ay_chip_synthesizer_logic_q(&e->chip);
    ay_chip_synthesizer_mixer_q_mono(&e->chip, e, &level_l);
    if (e->is_filt >= 0) {
      level_l = ay_apply_filter(level_l, e->filt_xl, e->filt_k, e->filt_m,
                                 &e->filt_i);
    }
    e->prev_left = e->left_chan;
    e->left_chan = level_l;
    e->left_chan1 += level_l;

    e->current_tik++;
    e->tick_counter_hi++;
  } while (!((uint32_t)e->current_tik >= number_of_tiks_hi(e)));
  e->number_of_tiks = 0;
  e->current_tik = 0;
}

/* AY.pas:711-770, Synthesizer_Stereo8. */
void ay_synthesizer_stereo8(ay_engine* e) {
  uint8_t* out = (uint8_t*)e->buf;
  do {
    int level_l = 0, level_r = 0;
    if (tick_counter_re(e) >= e->tik_re) {
      do {
        uint8_t l8, r8;
        if (e->is_filt > 0) {
          int64_t tmp = e->tik_re - tick_counter_re(e) + 65536;
          l8 = (uint8_t)ay_interpolator8(e->left_chan, e->prev_left, tmp);
          r8 = (uint8_t)ay_interpolator8(e->right_chan, e->prev_right, tmp);
        } else {
          l8 = (uint8_t)ay_averager8(e->left_chan1, e->tick_counter_hi);
          r8 = (uint8_t)ay_averager8(e->right_chan1, e->tick_counter_hi);
        }
        out[e->buf_len * 2] = l8;
        out[e->buf_len * 2 + 1] = r8;
        e->tik_re += e->delay_in_tiks;
        e->buf_len++;
        if (e->buf_len == e->buffer_length) {
          if (e->current_tik < number_of_tiks_hi(e)) e->int_flag = true;
          return;
        }
      } while (!(tick_counter_re(e) < e->tik_re));
      e->tik_re -= tick_counter_re(e);
      e->left_chan1 = 0;
      e->right_chan1 = 0;
      e->tick_counter_hi = 0;
    }
    ay_chip_synthesizer_logic_q(&e->chip);
    ay_chip_synthesizer_mixer_q(&e->chip, e, &level_l, &level_r);
    if (e->is_filt >= 0) {
      int saved_i = e->filt_i;
      level_l = ay_apply_filter(level_l, e->filt_xl, e->filt_k, e->filt_m,
                                 &e->filt_i);
      e->filt_i = saved_i;
      level_r = ay_apply_filter(level_r, e->filt_xr, e->filt_k, e->filt_m,
                                 &e->filt_i);
    }
    e->prev_left = e->left_chan;
    e->left_chan = level_l;
    e->left_chan1 += level_l;
    e->prev_right = e->right_chan;
    e->right_chan = level_r;
    e->right_chan1 += level_r;

    e->current_tik++;
    e->tick_counter_hi++;
  } while (!((uint32_t)e->current_tik >= number_of_tiks_hi(e)));
  e->number_of_tiks = 0;
  e->current_tik = 0;
}

/* AY.pas:857-902, Synthesizer_Mono8. */
void ay_synthesizer_mono8(ay_engine* e) {
  uint8_t* out = (uint8_t*)e->buf;
  do {
    int level_l = 0;
    if (tick_counter_re(e) >= e->tik_re) {
      do {
        uint8_t m8;
        if (e->is_filt > 0) {
          int64_t tmp = e->tik_re - tick_counter_re(e) + 65536;
          m8 = (uint8_t)ay_interpolator8(e->left_chan, e->prev_left, tmp);
        } else {
          m8 = (uint8_t)ay_averager8(e->left_chan1, e->tick_counter_hi);
        }
        out[e->buf_len] = m8;
        e->tik_re += e->delay_in_tiks;
        e->buf_len++;
        if (e->buf_len == e->buffer_length) {
          if (e->current_tik < number_of_tiks_hi(e)) e->int_flag = true;
          return;
        }
      } while (!(tick_counter_re(e) < e->tik_re));
      e->tik_re -= tick_counter_re(e);
      e->left_chan1 = 0;
      e->tick_counter_hi = 0;
    }
    ay_chip_synthesizer_logic_q(&e->chip);
    ay_chip_synthesizer_mixer_q_mono(&e->chip, e, &level_l);
    if (e->is_filt >= 0) {
      level_l = ay_apply_filter(level_l, e->filt_xl, e->filt_k, e->filt_m,
                                 &e->filt_i);
    }
    e->prev_left = e->left_chan;
    e->left_chan = level_l;
    e->left_chan1 += level_l;

    e->current_tik++;
    e->tick_counter_hi++;
  } while (!((uint32_t)e->current_tik >= number_of_tiks_hi(e)));
  e->number_of_tiks = 0;
  e->current_tik = 0;
}
