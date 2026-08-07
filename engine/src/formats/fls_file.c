#include "ay_engine/formats/fls_file.h"

#include <stdbool.h>
#include <string.h>

/* Players.pas:973-984, ST_Table - NOT the same table as PT3/PT1/GTR's
 * PT3NoteTable_ST (a real, deliberate difference between FLS and those
 * formats - e.g. entry 23 is 0x3F0 here vs 0x3FD in PT3NoteTable_ST). */
static const uint16_t ST_TABLE[96] = {
    0x0EF8, 0x0E10, 0x0D60, 0x0C80, 0x0BD8, 0x0B28, 0x0A88, 0x09F0, 0x0960,
    0x08E0, 0x0858, 0x07E0, 0x077C, 0x0708, 0x06B0, 0x0640, 0x05EC, 0x0594,
    0x0544, 0x04F8, 0x04B0, 0x0470, 0x042C, 0x03F0, 0x03BE, 0x0384, 0x0358,
    0x0320, 0x02F6, 0x02CA, 0x02A2, 0x027C, 0x0258, 0x0238, 0x0216, 0x01F8,
    0x01DF, 0x01C2, 0x01AC, 0x0190, 0x017B, 0x0165, 0x0151, 0x013E, 0x012C,
    0x011C, 0x010B, 0x00FC, 0x00EF, 0x00E1, 0x00D6, 0x00C8, 0x00BD, 0x00B2,
    0x00A8, 0x009F, 0x0096, 0x008E, 0x0085, 0x007E, 0x0077, 0x0070, 0x006B,
    0x0064, 0x005E, 0x0059, 0x0054, 0x004F, 0x004B, 0x0047, 0x0042, 0x003F,
    0x003B, 0x0038, 0x0035, 0x0032, 0x002F, 0x002C, 0x002A, 0x0027, 0x0025,
    0x0023, 0x0021, 0x001F, 0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016,
    0x0015, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F};

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

/* Module.FLS_PatternsPointers[n] (1-based, Players.pas:174-176): each
 * entry is 3 words (PatternA/B/C) starting at byte offset 6. */
static uint16_t fls_pattern_ptr(const uint8_t* d, int n, int which) {
  uint32_t off = 6 + (uint32_t)(n - 1) * 6 + (uint32_t)which * 2;
  return rd16(d, off);
}

/* ModTypes variant 10 (Players.pas:171-176): FLS_PositionsPointer@0
 * FLS_OrnamentsPointer@2 FLS_SamplesPointer@4 FLS_PatternsPointers[1..]@6
 * (3 words/6B per entry, no explicit count - open-ended). Unlike GTR,
 * there is no on-disk load-address field, so LoadTrackerModule's FT.FLS
 * branch (Players.pas:2463-2532) brute-force searches for the base
 * offset "i" that makes the header internally consistent, then applies
 * it in place. */
fls_file_status fls_file_load(fls_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;
  int i, i1, i2, j;

  memset(f, 0, sizeof(*f));

  if (size < 8) return FLS_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);
  int mlen = (int)size;

  uint16_t positions_ptr_raw = rd16(f->data, 0);
  uint16_t ornaments_ptr_raw = rd16(f->data, 2);
  uint16_t samples_ptr_raw = rd16(f->data, 4);
  uint16_t pattern1_a_raw = fls_pattern_ptr(f->data, 1, 0);
  uint16_t pattern1_b_raw = fls_pattern_ptr(f->data, 1, 1);

  bool found = false;
  for (i = (int)ornaments_ptr_raw - 16; i >= 0; i--) {
    i2 = (int)samples_ptr_raw + 2 - i;
    if (i2 < 8 || i2 >= mlen) continue;
    i1 = (int)rd16(f->data, (uint32_t)i2) - i;
    if (i1 < 8 || i1 >= mlen) continue;
    int i2b = (int)rd16(f->data, (uint32_t)(i2 - 4)) - i;
    if (i2b < 6 || i2b >= mlen) continue;
    if (i1 - i2b != 0x20) continue;
    int i2c = (int)pattern1_b_raw - i;
    if (i2c <= 21 || i2c >= mlen) continue;
    int i1c = (int)pattern1_a_raw - i;
    if (i1c <= 20 || i1c >= mlen) continue;
    if (f->data[i1c - 1] != 0) continue;

    /* Players.pas:2488-2503: walk pattern 1's opcode stream (mirrors
     * PatternInterpreter's own note/rest-vs-envelope opcode widths) until
     * a note/rest terminator (0..$5F, $80, $81) or the pattern-end marker
     * (255) is hit, and confirm it lands exactly at pattern 2's start. */
    int scan = i1c;
    while (scan < mlen && f->data[scan] != 255) {
      for (;;) {
        uint8_t op = f->data[scan];
        if (op <= 0x5F || op == 0x80 || op == 0x81) {
          scan++;
          break;
        } else if (op >= 0x82 && op <= 0x8E) {
          scan++;
        }
        scan++;
        if (scan >= mlen) break;
      }
    }
    if (scan + 1 == i2c) {
      found = true;
      break;
    }
  }
  if (!found) return FLS_FILE_ERR_ADDR_NOT_DETECTED;

  /* Players.pas:2513-2532: fix up the header's on-disk pointer words
   * (relative to the detected base "i") in place. */
  i1 = (int)samples_ptr_raw - i;
  if (i1 & 1) return FLS_FILE_ERR_BAD_HEADER;
  i2 = (int)positions_ptr_raw - i;
  if ((i2 - i1) & 3) return FLS_FILE_ERR_BAD_HEADER;

  uint32_t woff = 0;
  for (j = 0; j < i1 / 2; j++) {
    wr16(f->data, woff, (uint16_t)(rd16(f->data, woff) - i));
    woff += 2;
  }
  woff += 2;
  for (j = 0; j < (i2 - i1) / 4; j++) {
    wr16(f->data, woff, (uint16_t)(rd16(f->data, woff) - i));
    woff += 4;
  }

  f->positions_pointer = rd16(f->data, 0);
  f->ornaments_pointer = rd16(f->data, 2);
  f->samples_pointer = rd16(f->data, 4);

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * FLS_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3216-3260, InitTrackerModule's FT.FLS branch. */
  f->delay = rb(f->data, f->positions_pointer);
  f->delay_counter = 1;
  {
    int n = rb(f->data, (uint32_t)f->positions_pointer + 1);
    f->chan_a.address_in_pattern = fls_pattern_ptr(f->data, n, 0);
    f->chan_b.address_in_pattern = fls_pattern_ptr(f->data, n, 1);
    f->chan_c.address_in_pattern = fls_pattern_ptr(f->data, n, 2);
  }
  f->chan_a.sample_tik_counter = -1;
  f->chan_b.sample_tik_counter = -1;
  f->chan_c.sample_tik_counter = -1;

  f->global_tick_counter = 0;

  return FLS_FILE_OK;
}

/* Players.pas:11341-11404, PatternInterpreter. */
static void pattern_interpreter(fls_file* f, fls_channel* chan) {
  bool quit = false;

  do {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op <= 0x5F) {
      chan->note = op;
      chan->position_in_sample = 0;
      chan->sample_tik_counter = 0x20;
      quit = true;
    } else if (op <= 0x6F) {
      uint32_t base = f->samples_pointer + (uint32_t)(op - 0x60) * 4;
      chan->loop_sample_position = rb(f->data, base);
      chan->sample_length = rb(f->data, base + 1);
      chan->sample_pointer = rd16(f->data, base + 2);
    } else if (op == 0x70) {
      chan->ornament_enabled = false;
      chan->envelope_enabled = false;
    } else if (op <= 0x7F) {
      uint32_t base = f->ornaments_pointer + (uint32_t)(op - 0x71) * 2;
      chan->ornament_pointer = rd16(f->data, base);
      chan->ornament_enabled = true;
      chan->envelope_enabled = false;
    } else if (op == 0x80) {
      chan->sample_tik_counter = -1;
      quit = true;
    } else if (op == 0x81) {
      quit = true;
    } else if (op <= 0x8E) {
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, (uint8_t)(op - 0x80));
      chan->envelope_enabled = true;
      chan->ornament_enabled = false;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->ay.chip.reg[11] = rb(f->data, chan->address_in_pattern);
    } else {
      chan->number_of_notes_to_skip = (uint8_t)(op - 0xA1);
    }
    chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
  } while (!quit);
  chan->note_skip_counter = (int8_t)chan->number_of_notes_to_skip;
}

/* Players.pas:11406-11455, GetRegisters. */
static void get_registers(fls_file* f, fls_channel* chan, uint8_t* temp_mixer) {
  if (chan->sample_tik_counter >= 0) {
    chan->sample_tik_counter--;
    if (chan->sample_tik_counter == 0) {
      if (chan->loop_sample_position == 0) {
        chan->sample_tik_counter--;
        chan->amplitude = 0;
        *temp_mixer >>= 1;
        return;
      } else {
        chan->sample_tik_counter = (int8_t)chan->sample_length;
        chan->position_in_sample = (uint8_t)(chan->loop_sample_position - 1);
      }
    }
    uint8_t b0 = rb(f->data, (uint32_t)chan->sample_pointer +
                                  (uint32_t)chan->position_in_sample * 3);
    uint8_t b1 = rb(f->data, (uint32_t)chan->sample_pointer +
                                  (uint32_t)chan->position_in_sample * 3 + 1);
    chan->amplitude = (uint8_t)(b0 & 15);
    if (chan->envelope_enabled) chan->amplitude = (uint8_t)(chan->amplitude | 16);
    if ((int8_t)b1 < 0) {
      *temp_mixer |= 64;
    } else {
      f->ay.chip.reg[6] = (uint8_t)(b1 & 31);
    }
    if (b1 & 64) *temp_mixer |= 8;

    uint8_t j = chan->ornament_enabled
                    ? rb(f->data, (uint32_t)chan->ornament_pointer +
                                      chan->position_in_sample)
                    : 0;
    j = (uint8_t)(j + chan->note);
    if (j > 0x5F) j = 0x5F;

    int ton = (((int)b0 << 4) & 0xF00) +
              rb(f->data, (uint32_t)chan->sample_pointer +
                              (uint32_t)chan->position_in_sample * 3 + 2);
    if ((b1 & 32) == 0) ton = -ton;
    ton = (ton + ST_TABLE[j]) & 0xFFF;
    chan->ton = (uint16_t)ton;

    chan->position_in_sample = (uint8_t)((chan->position_in_sample + 1) & 31);
  } else {
    chan->amplitude = 0;
  }
  *temp_mixer >>= 1;
}

/* Players.pas:11337-11521, FLS_Get_Registers (minus CheckLoopAndStop -
 * matches pt3/pt1/gtr_get_registers's own precedent). */
static void fls_get_registers(fls_file* f) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter < 0) {
      if (rb(f->data, f->chan_a.address_in_pattern) == 255) {
        f->current_position++;
        if (rb(f->data, (uint32_t)f->current_position + f->positions_pointer +
                             1) == 0)
          f->current_position = 0;
        {
          int n = rb(f->data, (uint32_t)f->current_position +
                                   f->positions_pointer + 1);
          f->chan_a.address_in_pattern = fls_pattern_ptr(f->data, n, 0);
          f->chan_b.address_in_pattern = fls_pattern_ptr(f->data, n, 1);
          f->chan_c.address_in_pattern = fls_pattern_ptr(f->data, n, 2);
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

int fls_file_make_buffer(fls_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(FLS_FILE_AY_FREQ_DEF /
                    (FLS_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    fls_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
