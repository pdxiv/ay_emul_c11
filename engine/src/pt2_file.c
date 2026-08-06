#include "ay_engine/pt2_file.h"

#include <string.h>

/* Players.pas:1063-1071, PT3NoteTable_ST - same table PT1/GTR use
 * (duplicated here per this project's per-file convention). */
static const uint16_t PT3_NOTE_TABLE_ST[96] = {
    0x0EF8, 0x0E10, 0x0D60, 0x0C80, 0x0BD8, 0x0B28, 0x0A88, 0x09F0, 0x0960,
    0x08E0, 0x0858, 0x07E0, 0x077C, 0x0708, 0x06B0, 0x0640, 0x05EC, 0x0594,
    0x0544, 0x04F8, 0x04B0, 0x0470, 0x042C, 0x03FD, 0x03BE, 0x0384, 0x0358,
    0x0320, 0x02F6, 0x02CA, 0x02A2, 0x027C, 0x0258, 0x0238, 0x0216, 0x01F8,
    0x01DF, 0x01C2, 0x01AC, 0x0190, 0x017B, 0x0165, 0x0151, 0x013E, 0x012C,
    0x011C, 0x010A, 0x00FC, 0x00EF, 0x00E1, 0x00D6, 0x00C8, 0x00BD, 0x00B2,
    0x00A8, 0x009F, 0x0096, 0x008E, 0x0085, 0x007E, 0x0077, 0x0070, 0x006B,
    0x0064, 0x005E, 0x0059, 0x0054, 0x004F, 0x004B, 0x0047, 0x0042, 0x003F,
    0x003B, 0x0038, 0x0035, 0x0032, 0x002F, 0x002C, 0x002A, 0x0027, 0x0025,
    0x0023, 0x0021, 0x001F, 0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016,
    0x0015, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F};

/* Players.pas:9240: `Amplitude := round((Volume*17+...) * (b1 shr 4) /
 * 256)` - same round-half-to-even division PT1 uses (duplicated per
 * this project's per-file convention). */
static int round_div256(int numerator) {
  int quotient = numerator / 256;
  int remainder = numerator % 256;
  if (remainder < 128) return quotient;
  if (remainder > 128) return quotient + 1;
  return (quotient & 1) ? quotient + 1 : quotient; /* tie: round to even */
}

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* Copies a fixed-width, space-padded (not NUL-terminated) field, then
 * trims leading/trailing bytes <= ' ' - see gtr_file.c's identical helper for
 * the full rationale (duplicated per this project's per-file
 * convention). */
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

/* ModTypes variant 5 (Players.pas:128-135): PT2_Delay@0
 * PT2_NumberOfPositions@1 PT2_LoopPosition@2
 * PT2_SamplesPointers[0..31]@3 (64B) PT2_OrnamentsPointers[0..15]@67
 * (32B) PT2_PatternsPointer@99 (word) PT2_MusicName[0..29]@101
 * PT2_PositionList@131. */
pt2_file_status pt2_file_load(pt2_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;
  int i;

  memset(f, 0, sizeof(*f));

  if (size < 131) return PT2_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  /* Players.pas: "else if FType = FT.PT2" (7394-7402). */
  copy_fixed_field(f->data, size, 101, 30, f->title, sizeof(f->title));

  f->delay = f->data[0];
  f->loop_position = f->data[2];
  for (i = 0; i < 32; i++)
    f->samples_pointers[i] = rd16(f->data, 3 + (uint32_t)i * 2);
  for (i = 0; i < 16; i++)
    f->ornaments_pointers[i] = rd16(f->data, 67 + (uint32_t)i * 2);
  f->patterns_pointer = rd16(f->data, 99);
  f->position_list_offset = 131;

  /* Players.pas:2313-2358: this project's headless-loading convention
   * (PPLItem=nil) always takes this branch, re-deriving
   * PT2_NumberOfPositions from the position list itself (overwriting
   * whatever was on disk); the pointer-relocation half is gated on
   * MAddr<>0 and is therefore a no-op here (MAddr is always 0). */
  {
    uint32_t idx = 0;
    while (idx < 65535 - 131 && rb(f->data, f->position_list_offset + idx) < 128)
      idx++;
    if (idx >= 65535 - 131) return PT2_FILE_ERR_BAD_HEADER;
    f->number_of_positions = (idx > 255) ? 255 : (uint8_t)idx;
  }

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * PT2_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3622-3708ish, InitTrackerModule's FT.PT2 branch. */
  f->delay_counter = 1;
  f->current_position = 0;

  {
    uint8_t pos0 = rb(f->data, f->position_list_offset);
    uint32_t base = f->patterns_pointer + (uint32_t)pos0 * 6;
    f->chan_a.address_in_pattern = rd16(f->data, base);
    f->chan_b.address_in_pattern = rd16(f->data, base + 2);
    f->chan_c.address_in_pattern = rd16(f->data, base + 4);
  }

  {
    uint16_t sp = f->samples_pointers[1];
    uint8_t len_s = rb(f->data, sp);
    sp = (uint16_t)(sp + 1);
    uint8_t loop_s = rb(f->data, sp);
    sp = (uint16_t)(sp + 1);

    uint16_t op = f->ornaments_pointers[0];
    uint8_t len_o = rb(f->data, op);
    op = (uint16_t)(op + 1);
    uint8_t loop_o = rb(f->data, op);
    op = (uint16_t)(op + 1);

    pt2_channel* chans[3] = {&f->chan_a, &f->chan_b, &f->chan_c};
    for (i = 0; i < 3; i++) {
      chans[i]->sample_pointer = sp;
      chans[i]->sample_length = len_s;
      chans[i]->loop_sample_position = loop_s;
      chans[i]->ornament_pointer = op;
      chans[i]->ornament_length = len_o;
      chans[i]->loop_ornament_position = loop_o;
      chans[i]->volume = 15;
    }
  }

  f->global_tick_counter = 0;

  return PT2_FILE_OK;
}

/* Players.pas:9095-9210, PatternInterpreter. */
static void pattern_interpreter(pt2_file* f, pt2_channel* chan) {
  bool quit = false;
  bool gliss = false;

  do {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op >= 0xE1) {
      chan->sample_pointer = f->samples_pointers[op - 0xE0];
      chan->sample_length = rb(f->data, chan->sample_pointer);
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
      chan->loop_sample_position = rb(f->data, chan->sample_pointer);
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
    } else if (op == 0xE0) {
      chan->position_in_sample = 0;
      chan->position_in_ornament = 0;
      chan->current_ton_sliding = 0;
      chan->gliss_type = 0;
      chan->enabled = false;
      quit = true;
    } else if (op >= 0x80) { /* $80..$DF */
      chan->position_in_sample = 0;
      chan->position_in_ornament = 0;
      chan->current_ton_sliding = 0;
      if (gliss) {
        chan->slide_to_note = (uint8_t)(op - 0x80);
        if (chan->gliss_type == 1) chan->note = chan->slide_to_note;
      } else {
        chan->note = (uint8_t)(op - 0x80);
        chan->gliss_type = 0;
      }
      chan->enabled = true;
      quit = true;
    } else if (op == 0x7F) {
      chan->envelope_enabled = false;
    } else if (op >= 0x71) { /* $71..$7E */
      chan->envelope_enabled = true;
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, (uint8_t)(op - 0x70));
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->ay.chip.reg[11] = rb(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->ay.chip.reg[12] = rb(f->data, chan->address_in_pattern);
    } else if (op == 0x70) {
      quit = true;
    } else if (op >= 0x60) { /* $60..$6F */
      chan->ornament_pointer = f->ornaments_pointers[op - 0x60];
      chan->ornament_length = rb(f->data, chan->ornament_pointer);
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->loop_ornament_position = rb(f->data, chan->ornament_pointer);
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->position_in_ornament = 0;
    } else if (op >= 0x20) { /* $20..$5F */
      chan->number_of_notes_to_skip = (uint8_t)(op - 0x20);
    } else if (op >= 0x10) { /* $10..$1F */
      chan->volume = (uint8_t)(op - 0x10);
    } else if (op == 0x0F) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->delay = rb(f->data, chan->address_in_pattern);
    } else if (op == 0x0E) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->glissade = (int8_t)rb(f->data, chan->address_in_pattern);
      chan->gliss_type = 1;
      gliss = true;
    } else if (op == 0x0D) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      {
        int8_t raw = (int8_t)rb(f->data, chan->address_in_pattern);
        chan->glissade = (int8_t)(raw < 0 ? -raw : raw);
      }
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 2);
      chan->gliss_type = 2;
      gliss = true;
    } else if (op == 0x0C) {
      chan->gliss_type = 0;
    } else { /* 0..$0B */
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->addition_to_noise = (int8_t)rb(f->data, chan->address_in_pattern);
    }
    chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
  } while (!quit);

  /* Players.pas:9201-9207, "Alternative Ton_Delta calc". */
  if (gliss && chan->gliss_type == 2) {
    int delta =
        PT3_NOTE_TABLE_ST[chan->slide_to_note] - PT3_NOTE_TABLE_ST[chan->note];
    chan->ton_delta = (int16_t)(delta < 0 ? -delta : delta);
    if (chan->slide_to_note > chan->note)
      chan->glissade = (int8_t)(-chan->glissade);
  }
  chan->note_skip_counter = (int8_t)chan->number_of_notes_to_skip;
}

/* Players.pas:9212-9259, GetRegisters. */
static void get_registers(pt2_file* f, pt2_channel* chan, uint8_t* temp_mixer) {
  if (chan->enabled) {
    uint8_t b0 = rb(f->data, (uint32_t)chan->sample_pointer +
                                  (uint32_t)chan->position_in_sample * 3);
    uint8_t b1 = rb(f->data, (uint32_t)chan->sample_pointer +
                                  (uint32_t)chan->position_in_sample * 3 + 1);
    int ton = rb(f->data, (uint32_t)chan->sample_pointer +
                              (uint32_t)chan->position_in_sample * 3 + 2) +
              (((int)b1 & 15) << 8);
    if ((b0 & 4) == 0) ton = -ton;

    uint8_t j = (uint8_t)(chan->note +
                          rb(f->data, (uint32_t)chan->ornament_pointer +
                                          chan->position_in_ornament));
    if ((int8_t)j < 0)
      j = 0;
    else if (j > 95)
      j = 95;

    ton = (ton + chan->current_ton_sliding + PT3_NOTE_TABLE_ST[j]) & 0xFFF;
    chan->ton = (uint16_t)ton;

    if (chan->gliss_type == 2) {
      int abs_gliss = chan->glissade < 0 ? -chan->glissade : chan->glissade;
      chan->ton_delta = (int16_t)(chan->ton_delta - abs_gliss);
      if (chan->ton_delta < 0) {
        chan->note = chan->slide_to_note;
        chan->gliss_type = 0;
        chan->current_ton_sliding = 0;
      }
    }
    if (chan->gliss_type != 0)
      chan->current_ton_sliding =
          (int16_t)(chan->current_ton_sliding + chan->glissade);

    chan->amplitude = (uint8_t)round_div256(
        (chan->volume * 17 + (chan->volume > 7 ? 1 : 0)) * (uint8_t)(b1 >> 4));
    if (chan->envelope_enabled) chan->amplitude = (uint8_t)(chan->amplitude | 16);

    if (b0 & 1) {
      *temp_mixer |= 64;
    } else {
      f->ay.chip.reg[6] =
          (uint8_t)(((b0 >> 3) + (uint8_t)chan->addition_to_noise) & 31);
    }
    if (b0 & 2) *temp_mixer |= 8;

    chan->position_in_sample++;
    if (chan->position_in_sample == chan->sample_length)
      chan->position_in_sample = chan->loop_sample_position;
    chan->position_in_ornament++;
    if (chan->position_in_ornament == chan->ornament_length)
      chan->position_in_ornament = chan->loop_ornament_position;
  } else {
    chan->amplitude = 0;
  }
  *temp_mixer >>= 1;
}

/* Players.pas:9091-9323, PT2_Get_Registers (minus CheckLoopAndStop -
 * matches this project's other tracker-format ports' own precedent). */
static void pt2_get_registers(pt2_file* f) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter < 0) {
      if (rb(f->data, f->chan_a.address_in_pattern) == 0) {
        f->current_position++;
        if (f->current_position == f->number_of_positions)
          f->current_position = f->loop_position;
        {
          uint8_t pos =
              rb(f->data, f->position_list_offset + f->current_position);
          uint32_t base = f->patterns_pointer + (uint32_t)pos * 6;
          f->chan_a.address_in_pattern = rd16(f->data, base);
          f->chan_b.address_in_pattern = rd16(f->data, base + 2);
          f->chan_c.address_in_pattern = rd16(f->data, base + 4);
        }
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

  f->global_tick_counter++;
}

int pt2_file_make_buffer(pt2_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(PT2_FILE_AY_FREQ_DEF /
                    (PT2_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    pt2_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
