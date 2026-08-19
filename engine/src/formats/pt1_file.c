#include "ay_engine/formats/pt1_file.h"

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

/* Players.pas:16679-16770, GetTimePT1's per-channel opcode scan (the
 * `repeat case Index[jN] of ... until False` block nested inside `if
 * aN < 0 then`). `check_end` is true only for channel A: it alone
 * detects "no more pattern data for this position" via the pattern
 * terminator byte 255 (`if Index[j1] = 255 then break`) and signals the
 * caller to stop the whole row-walk for THIS position (channels B/C
 * have no such check in the original - only channel A's). Note byte 0
 * IS a valid opcode here (a plain note, unlike PT2 where 0 is the
 * terminator) - it falls into the `0..$5f` note-terminator case below.
 * PT1_TIME_STEP_ERROR signals either a malformed file that would push
 * `*j` at or past the 65536-byte data buffer (Pascal's own `Index`
 * array is one byte larger - 0..65536 - and this port's `data` buffer
 * is exactly `[65536]`, so offset 65536 itself must be rejected too,
 * not silently tolerated the way Pascal's oversized static buffer
 * happens to). `b` (shared tempo/delay byte) is mutated in place
 * exactly as the original's closure over its own outer `b` variable
 * does. */
typedef enum {
  PT1_TIME_STEP_CONTINUE,
  PT1_TIME_STEP_END_OF_POSITION,
  PT1_TIME_STEP_ERROR,
} pt1_time_step_result;

static pt1_time_step_result pt1_time_channel_step(const uint8_t* d, uint32_t* j,
                                                    int* a, int* a1x, uint8_t* b,
                                                    bool check_end) {
  (*a)--;
  if (*a >= 0) return PT1_TIME_STEP_CONTINUE;
  if (*j >= 65536) return PT1_TIME_STEP_ERROR;
  if (check_end && d[*j] == 255) return PT1_TIME_STEP_END_OF_POSITION;

  for (;;) {
    uint8_t op;
    if (*j >= 65536) return PT1_TIME_STEP_ERROR;
    op = d[*j];
    if (op == 0x80 || op == 0x90 || op <= 0x5F) {
      *a = *a1x;
      (*j)++;
      return PT1_TIME_STEP_CONTINUE;
    } else if (op >= 0x82 && op <= 0x8F) {
      *j += 2;
    } else if (op >= 0xB1 && op <= 0xFE) {
      *a1x = op - 0xB1;
    } else if (op >= 0x91 && op <= 0xA0) {
      *b = (uint8_t)(op - 0x91);
    }
    (*j)++;
  }
}

/* Players.pas:16679-16770, GetTimePT1 - computes the song's total
 * duration and loop-point tick in the same "global tick" units
 * global_tick_counter uses: it walks the fixed-length position list
 * (0..PT1_NumberOfPositions-1) exactly once, summing each processed
 * row's Delay value. On a malformed file (a bounds violation, or a
 * pathologically long song tripping the same DLCatcher=16384 safety net
 * the original has), returns 0/0 rather than raising - this port's
 * `pt1_file_load` already succeeded by the time this runs, so a
 * duration-precompute failure degrades to "no known duration" rather
 * than failing the whole load, matching pt3_get_time's own precedent. */
static void pt1_get_time(const pt1_file* f, int64_t* out_tm, int64_t* out_lp) {
  const uint8_t* d = f->data;
  int64_t tm = 0, lp = 0;
  uint8_t b = f->delay;
  int a1 = 0, a2 = 0, a3 = 0, a11 = 0, a22 = 0, a33 = 0;
  int dl_catcher = 16384;
  int i;

  for (i = 0; i < f->number_of_positions; i++) {
    uint32_t pl_addr, base, j1, j2, j3;
    uint8_t posb;

    pl_addr = f->position_list_offset + (uint32_t)i;
    if (pl_addr >= 65536) goto fail;
    posb = d[pl_addr];

    if (i == f->loop_position) lp = tm;

    base = f->patterns_pointer + (uint32_t)posb * 6;
    j1 = rd16(d, base);
    j2 = rd16(d, base + 2);
    j3 = rd16(d, base + 4);

    for (;;) {
      pt1_time_step_result r1 =
          pt1_time_channel_step(d, &j1, &a1, &a11, &b, true);
      if (r1 == PT1_TIME_STEP_ERROR) goto fail;
      if (r1 == PT1_TIME_STEP_END_OF_POSITION) break;

      if (pt1_time_channel_step(d, &j2, &a2, &a22, &b, false) ==
          PT1_TIME_STEP_ERROR)
        goto fail;
      if (pt1_time_channel_step(d, &j3, &a3, &a33, &b, false) ==
          PT1_TIME_STEP_ERROR)
        goto fail;

      tm += b;
      if (--dl_catcher < 0) goto fail;
    }
  }

  *out_tm = tm;
  *out_lp = lp;
  return;

fail:
  *out_tm = 0;
  *out_lp = 0;
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

  /* Players.pas: "else if FType = FT.PT1" (7383-7391). */
  copy_fixed_field(f->data, size, 69, 30, f->title, sizeof(f->title));

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
  f->do_loop = false;
  f->real_end_all = false;
  pt1_get_time(f, &f->global_tick_max, &f->loop_tick);

  return PT1_FILE_OK;
}

/* Players.pas:11180-11236, PatternInterpreter. */
static void pattern_interpreter(pt1_file* f, ay_chip* chip, pt1_channel* chan) {
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
      ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(op - 0x81));
      chan->address_in_pattern++;
      {
        uint16_t env = rd16(d, chan->address_in_pattern);
        chan->address_in_pattern++;
        chip->reg[11] = (uint8_t)(env & 0xFF);
        chip->reg[12] = (uint8_t)(env >> 8);
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
static void get_registers(pt1_file* f, ay_chip* chip, pt1_channel* chan,
                           uint8_t* temp_mixer) {
  if (chan->enabled) {
    uint8_t* d = f->data;
    /* Players.pas:11246-11247: `j := Note + Index[...]` - Index is an
     * unsigned byte array (ModTypes variant 0), no signed cast in the
     * source, so this is a plain unsigned add (unlike `shortint(b)`
     * below, which IS explicitly cast signed in the original). BUT `j`
     * itself is declared `var j, b: byte` (Players.pas:11240) - an 8-bit
     * Pascal variable, and with range checking off (confirmed: no {$R+}
     * anywhere, no -Cr in the build), `Note + Index[...]` silently wraps
     * modulo 256 when it exceeds 255, BEFORE the `if j > 95 then j := 95`
     * clamp below ever sees it. Widening to a plain C `int` and clamping
     * the unwrapped sum (as this used to do) gives a completely different
     * - and wrong - note-table index whenever the raw sum exceeds 255:
     * e.g. note=41 + ornament byte=251 -> raw sum 292, Pascal wraps to 36
     * (a valid, small table index), while an unwrapped clamp forces 95
     * (the table's last, near-silent entry) instead. Mask to a byte
     * FIRST to replicate Pascal's own variable width exactly. */
    int j = (chan->note + d[chan->ornament_pointer + chan->position_in_sample]) & 0xFF;
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
      chip->reg[6] = (uint8_t)(b2 & 31);
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

/* Players.pas:11176-11335, PT1_Get_Registers. MIG-0108: the
 * CheckLoopAndStop-equivalent check now lives in pt1_file_make_buffer's
 * tick loop instead of here (see its own comment) - functionally
 * equivalent since nothing else touches global_tick_counter in
 * between. MIG-0112: `chip` is the target AY register file this frame's
 * writes land in - `&f->ay.chip` for standalone/self playback, or an
 * EXTERNAL chip when this format is playlist-paired as Turbosound's
 * second voice (see stc_file.c's own comment on this same shape). */
static void pt1_get_registers(pt1_file* f, ay_chip* chip) {
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
      pattern_interpreter(f, chip, &f->chan_a);
    }
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter < 0) pattern_interpreter(f, chip, &f->chan_b);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter < 0) pattern_interpreter(f, chip, &f->chan_c);
    f->delay_counter = f->delay;
  }

  temp_mixer = 0;
  get_registers(f, chip, &f->chan_a, &temp_mixer);
  get_registers(f, chip, &f->chan_b, &temp_mixer);
  get_registers(f, chip, &f->chan_c, &temp_mixer);

  ay_chip_set_ay_register_fast(chip, 7, temp_mixer);

  chip->reg[0] = (uint8_t)(f->chan_a.ton & 0xFF);
  chip->reg[1] = (uint8_t)(f->chan_a.ton >> 8);
  chip->reg[2] = (uint8_t)(f->chan_b.ton & 0xFF);
  chip->reg[3] = (uint8_t)(f->chan_b.ton >> 8);
  chip->reg[4] = (uint8_t)(f->chan_c.ton & 0xFF);
  chip->reg[5] = (uint8_t)(f->chan_c.ton >> 8);

  ay_chip_set_ay_register_fast(chip, 8, f->chan_a.amplitude);
  ay_chip_set_ay_register_fast(chip, 9, f->chan_b.amplitude);
  ay_chip_set_ay_register_fast(chip, 10, f->chan_c.amplitude);

  f->global_tick_counter++;
}

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) + one pt1_get_registers
 * call - the reusable "advance one interrupt frame's worth of registers
 * into `chip`" building block MIG-0112's playlist-pairing driver needs
 * (player_step_registers, player.c). Returns false once this format's own
 * natural end is reached (mirrors Real_End[CNum] going true). */
bool pt1_file_step_registers(pt1_file* f, ay_chip* chip) {
  /* Players.pas:8730-8746, CheckLoopAndStop(CNum) - Force_Loop
   * (MIG-0114) lets register generation continue past the natural
   * end (so a shorter Turbosound-paired voice keeps looping audibly)
   * while still marking real_end_all true, matching `if Do_Loop or
   * Force_Loop then ...Counter := ...Max; if not Do_Loop then begin
   * Real_End[CNum] := True; if not Force_Loop then Exit(True); end;`
   * exactly. */
  if (f->global_tick_max > 0 && f->global_tick_counter >= f->global_tick_max) {
    if (f->do_loop || f->force_loop) {
      f->global_tick_counter = f->global_tick_max;
    }
    if (!f->do_loop) {
      f->real_end_all = true;
      if (!f->force_loop) return false;
    }
  }
  pt1_get_registers(f, chip);
  return true;
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
  /* See fxm_file.c's make_buffer for why number_of_channels is not
   * reset here (player_set_number_of_channels's load-time override
   * must persist across buffer-fill calls). */
  ay->sample_bits = 16;

  if (ay->int_flag) {
    ay->int_flag = false;
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  if (ay->int_flag) return ay->buf_len;

  while (ay->buf_len < buffer_length) {
    /* Players.pas: PT1_Get_Registers's own first statement, `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0108/MIG-0112) - pt1_file_step_registers is the shared building
     * block player_step_registers (player.c) also uses when this format
     * is playlist-paired as Turbosound's second voice; here it targets
     * this file's own private chip (standalone use). */
    if (!pt1_file_step_registers(f, &f->ay.chip)) break;
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
