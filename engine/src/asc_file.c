#include "ay_engine/asc_file.h"

#include <string.h>

#include "ay_engine/trace_log.h"

/* Players.pas:959-970, ASM_Table. */
static const uint16_t ASM_TABLE[86] = {
    0x0EDC, 0x0E07, 0x0D3E, 0x0C80, 0x0BCC, 0x0B22, 0x0A82, 0x09EC, 0x095C,
    0x08D6, 0x0858, 0x07E0, 0x076E, 0x0704, 0x069F, 0x0640, 0x05E6, 0x0591,
    0x0541, 0x04F6, 0x04AE, 0x046B, 0x042C, 0x03F0, 0x03B7, 0x0382, 0x034F,
    0x0320, 0x02F3, 0x02C8, 0x02A1, 0x027B, 0x0257, 0x0236, 0x0216, 0x01F8,
    0x01DC, 0x01C1, 0x01A8, 0x0190, 0x0179, 0x0164, 0x0150, 0x013D, 0x012C,
    0x011B, 0x010B, 0x00FC, 0x00EE, 0x00E0, 0x00D4, 0x00C8, 0x00BD, 0x00B2,
    0x00A8, 0x009F, 0x0096, 0x008D, 0x0085, 0x007E, 0x0077, 0x0070, 0x006A,
    0x0064, 0x005E, 0x0059, 0x0054, 0x0050, 0x004B, 0x0047, 0x0043, 0x003F,
    0x003C, 0x0038, 0x0035, 0x0032, 0x002F, 0x002D, 0x002A, 0x0028, 0x0026,
    0x0024, 0x0022, 0x0020, 0x001E, 0x001C};

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

/* Copies a fixed-width, space-padded (not NUL-terminated) field, then
 * trims leading/trailing bytes <= ' ' - see gtr_file.c's identical
 * helper for the full rationale (duplicated per this project's
 * per-file convention). */
static void copy_fixed_field(const uint8_t* src, size_t src_size,
                              size_t field_offset, size_t field_len,
                              char* out, size_t cap) {
  out[0] = '\0';
  if (cap == 0 || field_offset + field_len > src_size) return;
  size_t n = field_len;
  if (n >= cap) n = cap - 1;
  memcpy(out, src + field_offset, n);
  out[n] = '\0';
  while (n > 0 && (unsigned char)out[n - 1] <= ' ') n--;
  out[n] = '\0';
  size_t start = 0;
  while (start < n && (unsigned char)out[start] <= ' ') start++;
  if (start > 0) memmove(out, out + start, n - start + 1);
}

/* ModTypes variant 2 (Players.pas:116-119, the ASC1 layout every FT.ASC/
 * FT.ASC0 file is normalized to): ASC1_Delay@0 ASC1_LoopingPosition@1
 * ASC1_PatternsPointers@2 (word) ASC1_SamplesPointers@4 (word)
 * ASC1_OrnamentsPointers@6 (word) ASC1_Number_Of_Positions@8
 * ASC1_Positions@9. */
asc_file_status asc_file_load(asc_file* f, const uint8_t* data, size_t size,
                               bool is_asc0, int sample_rate) {
  (void)sample_rate;

  memset(f, 0, sizeof(*f));

  if (size < (is_asc0 ? 8u : 9u)) return ASC_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  if (is_asc0) {
    /* Players.pas:2383-2392: shift bytes [1..) right by 1 to open up
     * ASC1's LoopingPosition slot, then compensate the 3 (now-shifted)
     * pointer fields' VALUES by +1 each. */
    if (size < 65535) {
      size_t move_len = size - 1; /* Players.pas: MLen - 1 */
      if (move_len > 65534) move_len = 65534;
      memmove(f->data + 2, f->data + 1, move_len);
    }
    f->data[1] = 0; /* ASC1_LoopingPosition := 0 */
    wr16(f->data, 2, (uint16_t)(rd16(f->data, 2) + 1));
    wr16(f->data, 4, (uint16_t)(rd16(f->data, 4) + 1));
    wr16(f->data, 6, (uint16_t)(rd16(f->data, 6) + 1));
  }

  f->delay = f->data[0];
  f->looping_position = f->data[1];
  f->patterns_pointer = rd16(f->data, 2);
  f->samples_pointer = rd16(f->data, 4);
  f->ornaments_pointer = rd16(f->data, 6);
  f->number_of_positions = f->data[8];

  /* Players.pas: "else if FType = FT.ASC"/"FT.ASC0" (7436-7473). Both
   * branches reduce to the SAME check/offsets once is_asc0's own
   * normalization above has already run: ASC1's check is
   * `PatternsPointers - NumberOfPositions = 72`; ASC0's is `= 71`
   * against its own PRE-shift PatternsPointers value - but this port's
   * ASC0 path already added +1 to patterns_pointer during
   * normalization, so `(asc0_patterns_pointer_raw + 1) -
   * number_of_positions = 71 + 1 = 72`, the exact same check ASC1 uses
   * - one shared check/offset pair covers both formats correctly. */
  if (f->patterns_pointer >= f->number_of_positions &&
      (uint32_t)(f->patterns_pointer - f->number_of_positions) == 72 &&
      f->patterns_pointer >= 44) {
    copy_fixed_field(f->data, size, (size_t)f->patterns_pointer - 44, 20,
                      f->title, sizeof(f->title));
    copy_fixed_field(f->data, size, (size_t)f->patterns_pointer - 20, 20,
                      f->author, sizeof(f->author));
  }

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * ASC_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3352-3365, InitTrackerModule's shared FT.ASC/FT.ASC0
   * branch (almost entirely FillChar-redundant otherwise). */
  f->delay_counter = 1;
  {
    uint8_t pos0 = rb(f->data, 9);
    uint32_t base = f->patterns_pointer + (uint32_t)pos0 * 6;
    f->chan_a.address_in_pattern =
        (uint16_t)(rd16(f->data, base) + f->patterns_pointer);
    f->chan_b.address_in_pattern =
        (uint16_t)(rd16(f->data, base + 2) + f->patterns_pointer);
    f->chan_c.address_in_pattern =
        (uint16_t)(rd16(f->data, base + 4) + f->patterns_pointer);
  }

  f->current_position = 0;
  f->global_tick_counter = 0;

  return ASC_FILE_OK;
}

/* Players.pas:9713-9887, PatternInterpreter. */
static void pattern_interpreter(asc_file* f, asc_channel* chan) {
  bool init_sample_disabled = false;
  bool init_ornament_disabled = false;
  bool quit = false;

  chan->ton_sliding_counter = 0;
  chan->amplitude_delay_counter = 0;

  do {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op <= 0x55) {
      /* Players.pas:9727-9754: the note byte's own advance and (if
       * present) the envelope byte's advance are the ONLY increments
       * here - Pascal's `break` skips the common trailing Inc entirely,
       * so (unlike every non-terminal branch below) this relies on the
       * common trailing below to account for the note byte itself,
       * adding only ONE extra inner increment when an envelope byte
       * must also be skipped. */
      chan->note = op;
      chan->current_noise = chan->initial_noise;
      if ((int8_t)chan->ton_sliding_counter <= 0) chan->current_ton_sliding = 0;
      if (!init_sample_disabled) {
        chan->addition_to_amplitude = 0;
        chan->ton_deviation = 0;
        chan->point_in_sample = chan->initial_point_in_sample;
        chan->sound_enabled = true;
        chan->sample_finished = false;
        chan->break_sample_loop = false;
      }
      if (!init_ornament_disabled) {
        chan->point_in_ornament = chan->initial_point_in_ornament;
        chan->addition_to_note = 0;
      }
      if (chan->envelope_enabled) {
        f->ay.chip.reg[11] = rb(f->data, (uint32_t)chan->address_in_pattern + 1);
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      }
      quit = true;
    } else if (op >= 0x56 && op <= 0x5D) {
      quit = true;
    } else if (op == 0x5E) {
      chan->break_sample_loop = true;
      quit = true;
    } else if (op == 0x5F) {
      chan->sound_enabled = false;
      quit = true;
    } else if (op >= 0x60 && op <= 0x9F) {
      chan->number_of_notes_to_skip = (uint8_t)(op - 0x60);
    } else if (op >= 0xA0 && op <= 0xBF) {
      uint32_t off = (uint32_t)(op - 0xA0) * 2 + f->samples_pointer;
      chan->initial_point_in_sample =
          (uint16_t)(rd16(f->data, off) + f->samples_pointer);
    } else if (op >= 0xC0 && op <= 0xDF) {
      uint32_t off = (uint32_t)(op - 0xC0) * 2 + f->ornaments_pointer;
      chan->initial_point_in_ornament =
          (uint16_t)(rd16(f->data, off) + f->ornaments_pointer);
    } else if (op == 0xE0) {
      chan->volume = 15;
      chan->envelope_enabled = true;
    } else if (op >= 0xE1 && op <= 0xEF) {
      chan->volume = (uint8_t)(op - 0xE0);
      chan->envelope_enabled = false;
    } else if (op == 0xF0) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->initial_noise = rb(f->data, chan->address_in_pattern);
    } else if (op == 0xF1) {
      init_sample_disabled = true;
    } else if (op == 0xF2) {
      init_ornament_disabled = true;
    } else if (op == 0xF3) {
      init_sample_disabled = true;
      init_ornament_disabled = true;
    } else if (op == 0xF4) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->delay = rb(f->data, chan->address_in_pattern);
    } else if (op == 0xF5) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->substruction_for_ton_sliding =
          (int16_t)(-(int8_t)rb(f->data, chan->address_in_pattern) * 16);
      chan->ton_sliding_counter = 255;
    } else if (op == 0xF6) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->substruction_for_ton_sliding =
          (int16_t)((int8_t)rb(f->data, chan->address_in_pattern) * 16);
      chan->ton_sliding_counter = 255;
    } else if (op == 0xF7 || op == 0xF9) {
      /* Players.pas:9715 declares `delta_ton: smallint` (16-bit) - every
       * assignment to it, including the pre-shift ASM_Table difference
       * AND the `shl 4` below, wraps modulo 65536 with range checking
       * off. A wide `int delta_ton` (as this used to be) never wraps,
       * which normally doesn't matter (ASM_Table differences are small)
       * but the `shl 4` (*16) can push a legitimately large difference
       * (e.g. note=0, a very long/low-pitch period, sliding to a much
       * shorter one) well past +-32767 - Pascal wraps at that point,
       * this must too. int16_t replicates the 16-bit width exactly. */
      int16_t delta_ton;
      int8_t speed;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      if (op == 0xF7) init_sample_disabled = true;
      if (rb(f->data, (uint32_t)chan->address_in_pattern + 1) < 0x56) {
        uint8_t peek = rb(f->data, (uint32_t)chan->address_in_pattern + 1);
        delta_ton = (int16_t)(ASM_TABLE[chan->note] - ASM_TABLE[peek]);
        if (op == 0xF7)
          delta_ton = (int16_t)(delta_ton + chan->current_ton_sliding / 16);
      } else {
        delta_ton = (int16_t)(chan->current_ton_sliding / 16);
      }
      delta_ton = (int16_t)(delta_ton << 4);
      speed = (int8_t)rb(f->data, chan->address_in_pattern);
      chan->substruction_for_ton_sliding = (int16_t)(-delta_ton / speed);
      chan->current_ton_sliding = (int16_t)(delta_ton - delta_ton % speed);
      chan->ton_sliding_counter = (uint8_t)speed;
    } else if (op == 0xF8) {
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, 8);
    } else if (op == 0xFA) {
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, 10);
    } else if (op == 0xFB) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      if ((rb(f->data, chan->address_in_pattern) & 32) == 0) {
        chan->amplitude_delay =
            (uint8_t)(rb(f->data, chan->address_in_pattern) << 3);
        chan->amplitude_delay_counter = chan->amplitude_delay;
      } else {
        chan->amplitude_delay = (uint8_t)(
            ((rb(f->data, chan->address_in_pattern) << 3) ^ 0xF8) + 9);
        chan->amplitude_delay_counter = chan->amplitude_delay;
      }
    } else if (op == 0xFC) {
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, 12);
    } else if (op == 0xFE) {
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, 14);
    }
    chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
  } while (!quit);

  chan->note_skip_counter = (int8_t)chan->number_of_notes_to_skip;
}

/* Players.pas:9889-9982, GetRegisters. */
static void get_registers(asc_file* f, asc_channel* chan, uint8_t* temp_mixer) {
  if (chan->sample_finished || !chan->sound_enabled) {
    chan->amplitude = 0;
  } else {
    bool sample_ok_for_envelope;

    if (chan->amplitude_delay_counter != 0) {
      if (chan->amplitude_delay_counter >= 16) {
        chan->amplitude_delay_counter =
            (uint8_t)(chan->amplitude_delay_counter - 8);
        if (chan->addition_to_amplitude < -15)
          chan->addition_to_amplitude++;
        else if (chan->addition_to_amplitude > 15)
          chan->addition_to_amplitude--;
      } else {
        if (chan->amplitude_delay_counter & 1) {
          if (chan->addition_to_amplitude > -15) chan->addition_to_amplitude--;
        } else {
          if (chan->addition_to_amplitude < 15) chan->addition_to_amplitude++;
        }
        chan->amplitude_delay_counter = chan->amplitude_delay;
      }
    }

    if (rb(f->data, chan->point_in_sample) & 128)
      chan->loop_point_in_sample = chan->point_in_sample;
    if ((rb(f->data, chan->point_in_sample) & 96) == 32)
      chan->sample_finished = true;
    chan->ton_deviation = (uint16_t)(
        chan->ton_deviation +
        (int8_t)rb(f->data, (uint32_t)chan->point_in_sample + 1));

    uint8_t s2 = rb(f->data, (uint32_t)chan->point_in_sample + 2);
    *temp_mixer = (uint8_t)(((s2 & 9) << 3) | *temp_mixer);

    sample_ok_for_envelope = ((s2 & 6) == 2);
    if ((s2 & 6) == 4) {
      if (chan->addition_to_amplitude > -15) chan->addition_to_amplitude--;
    }
    if ((s2 & 6) == 6) {
      if (chan->addition_to_amplitude < 15) chan->addition_to_amplitude++;
    }

    chan->amplitude = (uint8_t)((uint8_t)chan->addition_to_amplitude + (s2 >> 4));
    if ((int8_t)chan->amplitude < 0)
      chan->amplitude = 0;
    else if (chan->amplitude > 15)
      chan->amplitude = 15;
    chan->amplitude = (uint8_t)((chan->amplitude * (chan->volume + 1)) >> 4);

    {
      uint8_t s0 = rb(f->data, chan->point_in_sample);
      int8_t delta = (int8_t)((uint8_t)(s0 << 3));
      int div8 = delta / 8;
      if (sample_ok_for_envelope && (*temp_mixer & 64)) {
        f->ay.chip.reg[11] = (uint8_t)(f->ay.chip.reg[11] + div8);
      } else {
        chan->current_noise = (uint8_t)(chan->current_noise + div8);
      }
    }

    chan->point_in_sample = (uint16_t)(chan->point_in_sample + 3);
    if (rb(f->data, (uint32_t)chan->point_in_sample - 3) & 64) {
      if (!chan->break_sample_loop) {
        chan->point_in_sample = chan->loop_point_in_sample;
      } else if (rb(f->data, (uint32_t)chan->point_in_sample - 3) & 32) {
        chan->sample_finished = true;
      }
    }

    if (rb(f->data, chan->point_in_ornament) & 128)
      chan->loop_point_in_ornament = chan->point_in_ornament;
    chan->addition_to_note = (uint8_t)(
        chan->addition_to_note +
        rb(f->data, (uint32_t)chan->point_in_ornament + 1));

    {
      uint8_t o0 = rb(f->data, chan->point_in_ornament);
      int m = (o0 & 0x10) ? -16 : 0;
      int se = m | o0;
      chan->current_noise = (uint8_t)(chan->current_noise + se);
    }

    chan->point_in_ornament = (uint16_t)(chan->point_in_ornament + 2);
    if (rb(f->data, (uint32_t)chan->point_in_ornament - 2) & 64)
      chan->point_in_ornament = chan->loop_point_in_ornament;

    if ((*temp_mixer & 64) == 0)
      f->ay.chip.reg[6] = (uint8_t)(
          ((uint8_t)(chan->current_ton_sliding >> 8) + chan->current_noise) &
          0x1F);

    uint8_t j_byte = (uint8_t)(chan->note + chan->addition_to_note);
    int8_t j = (int8_t)j_byte;
    if (j < 0)
      j = 0;
    else if (j > 0x55)
      j = 0x55;
    int ton = (ASM_TABLE[j] + chan->ton_deviation +
               (chan->current_ton_sliding / 16)) &
              0xFFF;
    chan->ton = (uint16_t)ton;

    if (chan->ton_sliding_counter != 0) {
      if ((int8_t)chan->ton_sliding_counter > 0) chan->ton_sliding_counter--;
      chan->current_ton_sliding =
          (int16_t)(chan->current_ton_sliding + chan->substruction_for_ton_sliding);
    }
    if (chan->envelope_enabled && sample_ok_for_envelope)
      chan->amplitude = (uint8_t)(chan->amplitude | 0x10);
  }
  *temp_mixer >>= 1;
}

/* Players.pas:9709-10051, ASC_Get_Registers (minus CheckLoopAndStop -
 * matches this project's other tracker-format ports' own precedent). */
static void asc_get_registers(asc_file* f) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter < 0) {
      if (rb(f->data, f->chan_a.address_in_pattern) == 255) {
        f->current_position++;
        if (f->current_position >= f->number_of_positions)
          f->current_position = f->looping_position;
        {
          uint8_t pos = rb(f->data, (uint32_t)f->current_position + 9);
          uint32_t base = f->patterns_pointer + (uint32_t)pos * 6;
          f->chan_a.address_in_pattern =
              (uint16_t)(rd16(f->data, base) + f->patterns_pointer);
          f->chan_b.address_in_pattern =
              (uint16_t)(rd16(f->data, base + 2) + f->patterns_pointer);
          f->chan_c.address_in_pattern =
              (uint16_t)(rd16(f->data, base + 4) + f->patterns_pointer);
        }
        f->chan_a.initial_noise = 0;
        f->chan_b.initial_noise = 0;
        f->chan_c.initial_noise = 0;
      }
      pattern_interpreter(f, &f->chan_a);
    }
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter < 0) pattern_interpreter(f, &f->chan_b);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter < 0) pattern_interpreter(f, &f->chan_c);
    f->delay_counter = f->delay;
  }

  temp_mixer = 0;
  get_registers(f, &f->chan_a, &temp_mixer);
  get_registers(f, &f->chan_b, &temp_mixer);
  get_registers(f, &f->chan_c, &temp_mixer);

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

  trace_log_ay(f->global_tick_counter, "asc_a", f->chan_a.ton, f->chan_a.amplitude);
  trace_log_ay(f->global_tick_counter, "asc_b", f->chan_b.ton, f->chan_b.amplitude);
  trace_log_ay(f->global_tick_counter, "asc_c", f->chan_c.ton, f->chan_c.amplitude);
  trace_log_ay(f->global_tick_counter, "asc_mixer", temp_mixer, 0);

  f->global_tick_counter++;
}

int asc_file_make_buffer(asc_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(ASC_FILE_AY_FREQ_DEF /
                    (ASC_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    asc_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
