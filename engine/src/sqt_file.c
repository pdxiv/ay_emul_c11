#include "ay_engine/sqt_file.h"

#include <string.h>

/* Players.pas:987-997, SQT_Table. */
static const uint16_t SQT_TABLE[96] = {
    0x0D5D, 0x0C9C, 0x0BE7, 0x0B3C, 0x0A9B, 0x0A02, 0x0973, 0x08EB, 0x086B,
    0x07F2, 0x0780, 0x0714, 0x06AE, 0x064E, 0x05F4, 0x059E, 0x054F, 0x0501,
    0x04B9, 0x0475, 0x0435, 0x03F9, 0x03C0, 0x038A, 0x0357, 0x0327, 0x02FA,
    0x02CF, 0x02A7, 0x0281, 0x025D, 0x023B, 0x021B, 0x01FC, 0x01E0, 0x01C5,
    0x01AC, 0x0194, 0x017D, 0x0168, 0x0153, 0x0140, 0x012E, 0x011D, 0x010D,
    0x00FE, 0x00F0, 0x00E2, 0x00D6, 0x00CA, 0x00BE, 0x00B4, 0x00AA, 0x00A0,
    0x0097, 0x008F, 0x0087, 0x007F, 0x0078, 0x0071, 0x006B, 0x0065, 0x005F,
    0x005A, 0x0055, 0x0050, 0x004C, 0x0047, 0x0043, 0x0040, 0x003C, 0x0039,
    0x0035, 0x0032, 0x0030, 0x002D, 0x002A, 0x0028, 0x0026, 0x0024, 0x0022,
    0x0020, 0x001E, 0x001C, 0x001B, 0x0019, 0x0018, 0x0016, 0x0015, 0x0014,
    0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E};

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

static void wr16(uint8_t* d, uint32_t addr, uint16_t v) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  d[addr] = (uint8_t)(v & 0xFF);
  d[a2] = (uint8_t)(v >> 8);
}

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* ModTypes variant 11 (Players.pas:177-178): SQT_Size@0 (word, unused)
 * SQT_SamplesPointer@2 SQT_OrnamentsPointer@4 SQT_PatternsPointer@6
 * SQT_PositionsPointer@8 SQT_LoopPointer@10 (all word). */
sqt_file_status sqt_file_load(sqt_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;

  memset(f, 0, sizeof(*f));

  if (size < 12) return SQT_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);
  int mlen = (int)size;

  uint16_t samples_ptr_raw = rd16(f->data, 2);
  uint16_t patterns_ptr_raw = rd16(f->data, 6);
  uint16_t positions_ptr_raw = rd16(f->data, 8);

  /* Players.pas:2393-2423: heuristic base-address detection + bulk word
   * relocation - see this file's header comment for the mechanics. */
  int base = (int)samples_ptr_raw - 10;
  if (base < 0) return SQT_FILE_ERR_BAD_HEADER;
  int i1 = 0;
  int i2 = (int)positions_ptr_raw - base;
  if (i2 < 0) return SQT_FILE_ERR_BAD_HEADER;
  while (rb(f->data, (uint32_t)i2) != 0) {
    int v;
    if (i2 > mlen - 8) return SQT_FILE_ERR_BAD_HEADER;
    v = rb(f->data, (uint32_t)i2) & 0x7F;
    if (i1 < v) i1 = v;
    i2 += 2;
    v = rb(f->data, (uint32_t)i2) & 0x7F;
    if (i1 < v) i1 = v;
    i2 += 2;
    v = rb(f->data, (uint32_t)i2) & 0x7F;
    if (i1 < v) i1 = v;
    i2 += 3;
  }
  int words_to_fix = ((int)patterns_ptr_raw - base + i1 * 2) / 2;
  if (words_to_fix < 1 || words_to_fix >= (65536 - 2) / 2)
    return SQT_FILE_ERR_BAD_HEADER;

  {
    uint32_t woff = 2;
    int j;
    for (j = 0; j < words_to_fix; j++) {
      wr16(f->data, woff, (uint16_t)(rd16(f->data, woff) - base));
      woff += 2;
    }
  }

  f->samples_pointer = rd16(f->data, 2);
  f->ornaments_pointer = rd16(f->data, 4);
  f->patterns_pointer = rd16(f->data, 6);
  f->positions_pointer = rd16(f->data, 8);
  f->loop_pointer = rd16(f->data, 10);

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * SQT_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3809-3842, InitTrackerModule's FT.SQT branch. */
  f->delay_counter = 1;
  f->delay = 1;
  f->lines_counter = 1;
  f->positions_pointer = rd16(f->data, 8); /* SQT_PositionsPointer, post-relocation */

  f->global_tick_counter = 0;

  return SQT_FILE_OK;
}

/* Players.pas:10434-10495, Call_LC1D1. */
static void call_lc1d1(sqt_file* f, sqt_channel* chan, uint16_t* ptr, uint8_t a) {
  (*ptr)++;
  if (chan->b6ix0) {
    chan->address_in_pattern = (uint16_t)(*ptr + 1);
    chan->b6ix0 = false;
  }
  uint8_t case_val = (uint8_t)(a - 1);
  if (case_val == 0) {
    if (chan->b4ix0) chan->volume = (uint8_t)(rb(f->data, *ptr) & 15);
  } else if (case_val == 1) {
    if (chan->b4ix0)
      chan->volume = (uint8_t)((chan->volume + rb(f->data, *ptr)) & 15);
  } else if (case_val == 2) {
    if (chan->b4ix0) {
      uint8_t v = rb(f->data, *ptr);
      f->chan_a.volume = v;
      f->chan_b.volume = v;
      f->chan_c.volume = v;
    }
  } else if (case_val == 3) {
    if (chan->b4ix0) {
      uint8_t v = rb(f->data, *ptr);
      f->chan_a.volume = (uint8_t)((f->chan_a.volume + v) & 15);
      f->chan_b.volume = (uint8_t)((f->chan_b.volume + v) & 15);
      f->chan_c.volume = (uint8_t)((f->chan_c.volume + v) & 15);
    }
  } else if (case_val == 4) {
    if (chan->b4ix0) {
      uint8_t v = (uint8_t)(rb(f->data, *ptr) & 31);
      if (v == 0) v = 32;
      f->delay_counter = v;
      f->delay = v;
    }
  } else if (case_val == 5) {
    if (chan->b4ix0) {
      uint8_t v = (uint8_t)((f->delay_counter + rb(f->data, *ptr)) & 31);
      if (v == 0) v = 32;
      f->delay_counter = v;
      f->delay = v;
    }
  } else if (case_val == 6) {
    chan->current_ton_sliding = 0;
    chan->gliss = true;
    chan->ton_slide_step = (int16_t)(-(int)rb(f->data, *ptr));
  } else if (case_val == 7) {
    chan->current_ton_sliding = 0;
    chan->gliss = true;
    chan->ton_slide_step = rb(f->data, *ptr);
  } else {
    chan->envelope_enabled = true;
    ay_chip_set_ay_register_fast(&f->ay.chip, 13, (uint8_t)(case_val & 15));
    f->ay.chip.reg[11] = rb(f->data, *ptr);
  }
}

/* Players.pas:10497-10512, Call_LC2A8. */
static void call_lc2a8(sqt_file* f, sqt_channel* chan, uint8_t a) {
  chan->envelope_enabled = false;
  chan->ornament_enabled = false;
  chan->gliss = false;
  chan->enabled = true;
  chan->sample_pointer = rd16(f->data, (uint32_t)a * 2 + f->samples_pointer);
  chan->point_in_sample = (uint16_t)(chan->sample_pointer + 2);
  chan->sample_tik_counter = 32;
  chan->mix_noise = true;
  chan->mix_ton = true;
}

/* Players.pas:10514-10524, Call_LC2D9. */
static void call_lc2d9(sqt_file* f, sqt_channel* chan, uint8_t a) {
  chan->ornament_pointer = rd16(f->data, (uint32_t)a * 2 + f->ornaments_pointer);
  chan->point_in_ornament = (uint16_t)(chan->ornament_pointer + 2);
  chan->ornament_tik_counter = 32;
  chan->ornament_enabled = true;
}

/* Players.pas:10526-10548, Call_LC283. */
static void call_lc283(sqt_file* f, sqt_channel* chan, uint16_t* ptr) {
  uint8_t op = rb(f->data, *ptr);
  if (op <= 0x7F) {
    call_lc1d1(f, chan, ptr, op);
  } else {
    if (((op >> 1) & 31) != 0) call_lc2a8(f, chan, (uint8_t)((op >> 1) & 31));
    if (op & 64) {
      int temp = rb(f->data, (uint32_t)*ptr + 1) >> 4;
      if (op & 1) temp |= 16;
      if (temp != 0) call_lc2d9(f, chan, (uint8_t)temp);
      (*ptr)++;
      if (rb(f->data, *ptr) & 15)
        call_lc1d1(f, chan, ptr, (uint8_t)(rb(f->data, *ptr) & 15));
    }
  }
  (*ptr)++;
}

/* Players.pas:10550-10567, Call_LC191. */
static void call_lc191(sqt_file* f, sqt_channel* chan) {
  uint16_t ptr = chan->ix27;
  chan->b6ix0 = false;
  uint8_t op = rb(f->data, ptr);
  if (op <= 0x7F) {
    ptr++;
    call_lc283(f, chan, &ptr);
  } else {
    call_lc2a8(f, chan, (uint8_t)(op & 31));
  }
}

/* Players.pas:10429-10638, PatternInterpreter. Uses real C `break`
 * statements mirroring Pascal's own `break` 1:1 (unlike this project's
 * other tracker ports, which flatten into if/elseif chains with a quit
 * flag) because this format's break placement is genuinely irregular -
 * one sub-branch inside the $80-$BF opcode conditionally breaks BEFORE
 * reaching code that every other path in that same opcode falls through
 * to (Call_LC191), so a flattened quit-flag translation would risk
 * silently losing that early-exit. */
static void pattern_interpreter(sqt_file* f, sqt_channel* chan) {
  if (chan->ix21 != 0) {
    chan->ix21--;
    if (chan->b7ix0) call_lc191(f, chan);
    return;
  }

  uint16_t ptr = chan->address_in_pattern;
  chan->b6ix0 = true;
  chan->b7ix0 = false;

  for (;;) {
    uint8_t op = rb(f->data, ptr);
    if (op <= 0x5F) {
      chan->note = op;
      chan->ix27 = ptr;
      ptr = (uint16_t)(ptr + 1);
      call_lc283(f, chan, &ptr);
      if (chan->b6ix0) chan->address_in_pattern = ptr;
      break;
    } else if (op <= 0x6E) {
      call_lc1d1(f, chan, &ptr, (uint8_t)(op - 0x60));
      break;
    } else if (op <= 0x7F) {
      chan->mix_noise = false;
      chan->mix_ton = false;
      chan->enabled = false;
      if (op != 0x6F) {
        call_lc1d1(f, chan, &ptr, (uint8_t)(op - 0x6F));
      } else {
        chan->address_in_pattern = (uint16_t)(ptr + 1);
      }
      break;
    } else if (op <= 0xBF) {
      chan->address_in_pattern = (uint16_t)(ptr + 1);
      if (op <= 0x9F) {
        if ((op & 16) == 0)
          chan->note = (uint8_t)(chan->note + (op & 15));
        else
          chan->note = (uint8_t)(chan->note - (op & 15));
      } else {
        chan->ix21 = (uint8_t)(op & 15);
        if ((op & 16) == 0) break;
        if (chan->ix21 != 0) chan->b7ix0 = true;
      }
      call_lc191(f, chan);
      break;
    } else { /* $C0..$FF */
      chan->address_in_pattern = (uint16_t)(ptr + 1);
      chan->ix27 = ptr;
      call_lc2a8(f, chan, (uint8_t)(op & 31));
      break;
    }
  }
}

/* Players.pas:10640-10721, GetRegisters. */
static void get_registers(sqt_file* f, sqt_channel* chan, uint8_t* temp_mixer) {
  *temp_mixer = (uint8_t)(*temp_mixer << 1);
  if (!chan->enabled) {
    chan->amplitude = 0;
    return;
  }

  uint8_t b0 = rb(f->data, chan->point_in_sample);
  chan->amplitude = (uint8_t)(b0 & 15);
  if (chan->amplitude != 0) {
    chan->amplitude = (uint8_t)(chan->amplitude - chan->volume);
    if ((int8_t)chan->amplitude < 0) chan->amplitude = 0;
  } else if (chan->envelope_enabled) {
    chan->amplitude = 16;
  }

  uint8_t b1 = rb(f->data, (uint32_t)chan->point_in_sample + 1);
  if (b1 & 32) {
    *temp_mixer |= 8;
    f->ay.chip.reg[6] = (uint8_t)((b0 & 0xF0) >> 3);
    if ((int8_t)b1 < 0) f->ay.chip.reg[6]++;
  }
  if (b1 & 64) *temp_mixer |= 1;

  uint8_t j = chan->note;
  if (chan->ornament_enabled) {
    j = (uint8_t)(j + rb(f->data, chan->point_in_ornament));
    chan->ornament_tik_counter--;
    if (chan->ornament_tik_counter == 0) {
      if (rb(f->data, chan->ornament_pointer) != 32) {
        chan->ornament_tik_counter =
            (int8_t)rb(f->data, (uint32_t)chan->ornament_pointer + 1);
        chan->point_in_ornament = (uint16_t)(
            chan->ornament_pointer + 2 + rb(f->data, chan->ornament_pointer));
      } else {
        chan->ornament_tik_counter =
            (int8_t)rb(f->data, (uint32_t)chan->sample_pointer + 1);
        chan->point_in_ornament = (uint16_t)(
            chan->ornament_pointer + 2 + rb(f->data, chan->sample_pointer));
      }
    } else {
      chan->point_in_ornament = (uint16_t)(chan->point_in_ornament + 1);
    }
  }
  j = (uint8_t)(j + chan->transposit);
  if (j > 0x5F) j = 0x5F;

  int ton;
  int delta = ((int)(b1 & 15) << 8) + rb(f->data, (uint32_t)chan->point_in_sample + 2);
  if ((b1 & 16) == 0)
    ton = SQT_TABLE[j] - delta;
  else
    ton = SQT_TABLE[j] + delta;

  chan->sample_tik_counter--;
  if (chan->sample_tik_counter == 0) {
    chan->sample_tik_counter = (int8_t)rb(f->data, (uint32_t)chan->sample_pointer + 1);
    if (rb(f->data, chan->sample_pointer) == 32) {
      chan->enabled = false;
      chan->ornament_enabled = false;
    }
    chan->point_in_sample = (uint16_t)(
        chan->sample_pointer + 2 + (uint32_t)rb(f->data, chan->sample_pointer) * 3);
  } else {
    chan->point_in_sample = (uint16_t)(chan->point_in_sample + 3);
  }

  if (chan->gliss) {
    ton += chan->current_ton_sliding;
    chan->current_ton_sliding = (int16_t)(chan->current_ton_sliding + chan->ton_slide_step);
  }
  chan->ton = (uint16_t)(ton & 0xFFF);
}

/* Players.pas:10425-10843, SQT_Get_Registers (minus CheckLoopAndStop -
 * matches this project's other tracker-format ports' own precedent).
 * Channel processing order is C, B, A (like PSM, unlike every other
 * format ported so far, which goes A, B, C). */
static void sqt_get_registers(sqt_file* f) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->delay_counter = f->delay;
    f->lines_counter--;
    if (f->lines_counter == 0) {
      if (rb(f->data, f->positions_pointer) == 0)
        f->positions_pointer = f->loop_pointer;

      f->chan_c.b4ix0 = ((int8_t)rb(f->data, f->positions_pointer) < 0);
      {
        uint8_t pat = (uint8_t)(rb(f->data, f->positions_pointer) * 2);
        f->chan_c.address_in_pattern = rd16(f->data, f->patterns_pointer + pat);
      }
      f->lines_counter = rb(f->data, f->chan_c.address_in_pattern);
      f->chan_c.address_in_pattern = (uint16_t)(f->chan_c.address_in_pattern + 1);
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_c.volume = (uint8_t)(rb(f->data, f->positions_pointer) & 15);
      {
        uint8_t hi = (uint8_t)(rb(f->data, f->positions_pointer) >> 4);
        if (hi < 9)
          f->chan_c.transposit = (int8_t)hi;
        else
          f->chan_c.transposit = (int8_t)(-(int)(hi - 9) - 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_c.ix21 = 0;

      if (rb(f->data, f->positions_pointer) == 0)
        f->positions_pointer = f->loop_pointer;
      f->chan_b.b4ix0 = ((int8_t)rb(f->data, f->positions_pointer) < 0);
      {
        uint8_t pat = (uint8_t)(rb(f->data, f->positions_pointer) * 2);
        f->chan_b.address_in_pattern =
            (uint16_t)(rd16(f->data, f->patterns_pointer + pat) + 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_b.volume = (uint8_t)(rb(f->data, f->positions_pointer) & 15);
      {
        uint8_t hi = (uint8_t)(rb(f->data, f->positions_pointer) >> 4);
        if (hi < 9)
          f->chan_b.transposit = (int8_t)hi;
        else
          f->chan_b.transposit = (int8_t)(-(int)(hi - 9) - 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_b.ix21 = 0;

      if (rb(f->data, f->positions_pointer) == 0)
        f->positions_pointer = f->loop_pointer;
      f->chan_a.b4ix0 = ((int8_t)rb(f->data, f->positions_pointer) < 0);
      {
        uint8_t pat = (uint8_t)(rb(f->data, f->positions_pointer) * 2);
        f->chan_a.address_in_pattern =
            (uint16_t)(rd16(f->data, f->patterns_pointer + pat) + 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_a.volume = (uint8_t)(rb(f->data, f->positions_pointer) & 15);
      {
        uint8_t hi = (uint8_t)(rb(f->data, f->positions_pointer) >> 4);
        if (hi < 9)
          f->chan_a.transposit = (int8_t)hi;
        else
          f->chan_a.transposit = (int8_t)(-(int)(hi - 9) - 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_a.ix21 = 0;

      f->delay = rb(f->data, f->positions_pointer);
      f->delay_counter = f->delay;
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
    }
    pattern_interpreter(f, &f->chan_c);
    pattern_interpreter(f, &f->chan_b);
    pattern_interpreter(f, &f->chan_a);
  }

  temp_mixer = 0;
  get_registers(f, &f->chan_c, &temp_mixer);
  get_registers(f, &f->chan_b, &temp_mixer);
  get_registers(f, &f->chan_a, &temp_mixer);
  temp_mixer = (uint8_t)((-(int)(temp_mixer + 1)) & 0x3F);

  if (!f->chan_a.mix_noise) temp_mixer = (uint8_t)(temp_mixer | 8);
  if (!f->chan_a.mix_ton) temp_mixer = (uint8_t)(temp_mixer | 1);
  if (!f->chan_b.mix_noise) temp_mixer = (uint8_t)(temp_mixer | 16);
  if (!f->chan_b.mix_ton) temp_mixer = (uint8_t)(temp_mixer | 2);
  if (!f->chan_c.mix_noise) temp_mixer = (uint8_t)(temp_mixer | 32);
  if (!f->chan_c.mix_ton) temp_mixer = (uint8_t)(temp_mixer | 4);

  ay_chip_set_ay_register_fast(&f->ay.chip, 7, temp_mixer);

  f->ay.chip.reg[0] = (uint8_t)(f->chan_a.ton & 0xFF);
  f->ay.chip.reg[1] = (uint8_t)(f->chan_a.ton >> 8);
  f->ay.chip.reg[2] = (uint8_t)(f->chan_b.ton & 0xFF);
  f->ay.chip.reg[3] = (uint8_t)(f->chan_b.ton >> 8);
  f->ay.chip.reg[4] = (uint8_t)(f->chan_c.ton & 0xFF);
  f->ay.chip.reg[5] = (uint8_t)(f->chan_c.ton >> 8);

  ay_chip_set_ay_register_fast(&f->ay.chip, 8, f->chan_a.amplitude);
  ay_chip_set_ay_register_fast(&f->ay.chip, 9, f->chan_b.amplitude);
  ay_chip_set_ay_register_fast(&f->ay.chip, 10, f->chan_c.amplitude);

  f->global_tick_counter++;
}

int sqt_file_make_buffer(sqt_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(SQT_FILE_AY_FREQ_DEF /
                    (SQT_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
                0.5);

  ay->buf = buf;
  ay->buf_len = 0;
  ay->buffer_length = buffer_length;
  ay->number_of_channels = 2;
  ay->sample_bits = 16;

  if (ay->int_flag) {
    ay->int_flag = false;
    ay_synthesizer_stereo16(ay);
  }
  if (ay->int_flag) return ay->buf_len;

  while (ay->buf_len < buffer_length) {
    sqt_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
