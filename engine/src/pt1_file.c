#include "ay_engine/pt1_file.h"

#include <string.h>

/* Players.pas:1063-1071, PT3NoteTable_ST - same table PT3 uses (PT1_Get_
 * Registers references it directly, Players.pas:11254) - duplicated here
 * rather than shared with pt3_file.c to keep each format file
 * self-contained, matching this project's existing per-file convention. */
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

/* Players.pas:11251: `Amplitude := round((Volume*17+...) * (b and 15) /
 * 256)` - Pascal's `round()` on a real-valued division uses round-half-
 * to-even (banker's rounding, FreePascal's documented default), NOT
 * truncating integer division - a real divergence found via oracle-diff
 * (matched for the first 17281 frames, then drifted, traced to this).
 * Numerator is always a non-negative integer (Volume 0..15, b&15 0..15),
 * so plain integer div/mod suffice to implement the tie-to-even rule
 * exactly, without needing floating point at all. */
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

/* ModTypes variant 9 (Players.pas:163-170): PT1_Delay@0 PT1_NumberOf
 * Positions@1 PT1_LoopPosition@2 PT1_SamplesPointers[0..15]@3 (32B)
 * PT1_OrnamentsPointers[0..15]@35 (32B) PT1_PatternsPointer@67 (word)
 * PT1_MusicName[0..29]@69 PT1_PositionList[]@99. */
pt1_file_status pt1_file_load(pt1_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;
  int i;

  memset(f, 0, sizeof(*f));

  if (size < 100) return PT1_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  f->delay = f->data[0];
  f->number_of_positions = f->data[1];
  f->loop_position = f->data[2];
  for (i = 0; i < 16; i++)
    f->samples_pointers[i] = rd16(f->data, 3 + (uint32_t)i * 2);
  for (i = 0; i < 16; i++)
    f->ornaments_pointers[i] = rd16(f->data, 35 + (uint32_t)i * 2);
  f->patterns_pointer = rd16(f->data, 67);
  f->position_list_offset = 99;

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * PT1_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3570-3621, InitTrackerModule's FT.PT1 branch. */
  f->delay_counter = 1;

  i = f->data[f->position_list_offset];
  f->chan_a.address_in_pattern = rd16(f->data, f->patterns_pointer + (uint32_t)i * 6);
  f->chan_b.address_in_pattern = rd16(f->data, f->patterns_pointer + (uint32_t)i * 6 + 2);
  f->chan_c.address_in_pattern = rd16(f->data, f->patterns_pointer + (uint32_t)i * 6 + 4);

  f->chan_a.ornament_pointer = f->ornaments_pointers[0];
  f->chan_a.volume = 15;
  f->chan_b.ornament_pointer = f->chan_a.ornament_pointer;
  f->chan_b.volume = 15;
  f->chan_c.ornament_pointer = f->chan_a.ornament_pointer;
  f->chan_c.volume = 15;

  f->global_tick_counter = 0;

  return PT1_FILE_OK;
}

/* Players.pas:11180-11236, PatternInterpreter. */
static void pattern_interpreter(pt1_file* f, pt1_channel* chan) {
  bool quit = false;
  uint8_t* d = f->data;

  do {
    uint8_t op = d[chan->address_in_pattern];
    if (op <= 0x5F) {
      chan->note = op;
      chan->enabled = true;
      chan->position_in_sample = 0;
      quit = true;
    } else if (op <= 0x6F) {
      chan->sample_pointer = f->samples_pointers[op - 0x60];
      chan->sample_length = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->loop_sample_position = d[chan->sample_pointer];
      chan->sample_pointer++;
    } else if (op <= 0x7F) {
      chan->ornament_pointer = f->ornaments_pointers[op - 0x70];
    } else if (op == 0x80) {
      chan->enabled = false;
      quit = true;
    } else if (op == 0x81) {
      chan->envelope_enabled = false;
    } else if (op <= 0x8F) {
      /* Players.pas:11213-11222: `RegisterAY.Envelope :=
       * PWord(@Index[Address_In_Pattern])^` - a genuine native-endian
       * (x86 LE) word read, UNLIKE PT3's explicit .hi/.lo big-endian-style
       * construction (Players.pas:12396-12398/12451-12453) - a real
       * difference between the two formats' on-disk envelope encoding,
       * not a copy-paste of PT3's convention. */
      chan->envelope_enabled = true;
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, (uint8_t)(op - 0x81));
      chan->address_in_pattern++;
      {
        uint16_t env = rd16(d, chan->address_in_pattern);
        chan->address_in_pattern++;
        f->ay.chip.reg[11] = (uint8_t)(env & 0xFF);
        f->ay.chip.reg[12] = (uint8_t)(env >> 8);
      }
    } else if (op == 0x90) {
      quit = true;
    } else if (op <= 0xA0) {
      f->delay = (uint8_t)(op - 0x91);
    } else if (op <= 0xB0) {
      chan->volume = (uint8_t)(op - 0xA1);
    } else {
      chan->number_of_notes_to_skip = (uint8_t)(op - 0xB1);
    }
    chan->address_in_pattern++;
  } while (!quit);
  chan->note_skip_counter = (int8_t)chan->number_of_notes_to_skip;
}

/* Players.pas:11238-11269, GetRegisters. */
static void get_registers(pt1_file* f, pt1_channel* chan, uint8_t* temp_mixer) {
  if (chan->enabled) {
    uint8_t* d = f->data;
    /* Players.pas:11246-11247: `j := Note + Index[...]` - Index is an
     * unsigned byte array (ModTypes variant 0), no signed cast in the
     * source, so this is a plain unsigned add (unlike `shortint(b)`
     * below, which IS explicitly cast signed in the original). */
    int j = chan->note + d[chan->ornament_pointer + chan->position_in_sample];
    if (j > 95) j = 95;
    uint8_t b = d[chan->sample_pointer + (uint32_t)chan->position_in_sample * 3];
    int ton = (((int)b << 4) & 0xF00) +
              d[chan->sample_pointer + (uint32_t)chan->position_in_sample * 3 + 2];
    chan->amplitude = (uint8_t)round_div256(
        (chan->volume * 17 + (chan->volume > 7 ? 1 : 0)) * (b & 15));
    uint8_t b2 = d[chan->sample_pointer + (uint32_t)chan->position_in_sample * 3 + 1];
    if ((b2 & 32) == 0) ton = -ton;
    ton = (ton + PT3_NOTE_TABLE_ST[j] + (j == 46 ? 1 : 0)) & 0xFFF;
    chan->ton = (uint16_t)ton;
    if (chan->envelope_enabled) chan->amplitude |= 16;
    if ((int8_t)b2 < 0) {
      *temp_mixer |= 64;
    } else {
      f->ay.chip.reg[6] = (uint8_t)(b2 & 31);
    }
    if (b2 & 64) *temp_mixer |= 8;
    chan->position_in_sample++;
    if (chan->position_in_sample == chan->sample_length)
      chan->position_in_sample = chan->loop_sample_position;
  } else {
    chan->amplitude = 0;
  }
  *temp_mixer >>= 1;
}

/* Players.pas:11176-11335, PT1_Get_Registers (minus CheckLoopAndStop -
 * matches pt3_get_registers's own precedent, see pt1_file.h). */
static void pt1_get_registers(pt1_file* f) {
  uint8_t temp_mixer = 0;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter < 0) {
      if (f->data[f->chan_a.address_in_pattern] == 255) {
        f->current_position++;
        if (f->current_position == f->number_of_positions)
          f->current_position = f->loop_position;
        {
          int i = f->data[f->position_list_offset + f->current_position];
          f->chan_a.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 6);
          f->chan_b.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 6 + 2);
          f->chan_c.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 6 + 4);
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

int pt1_file_make_buffer(pt1_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(PT1_FILE_AY_FREQ_DEF /
                    (PT1_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    pt1_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
