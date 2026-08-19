#include "ay_engine/formats/stc_file.h"

#include <string.h>

/* Players.pas:973-984, ST_Table - same table FLS uses (duplicated here
 * per this project's per-file convention; NOT the same as PT3/PT1/GTR's
 * PT3NoteTable_ST, which differs at several entries). */
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

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* Players.pas:15003-15040, GetTimeSTC - walks the position list opcode-
 * only (no audio synthesis) to compute the song's total duration. STC
 * has no loop-point output, unlike GTR/PT3/PSM's GetTimeXXX (matching
 * stc_file.h, whose new field is global_tick_max only, no loop_tick).
 * Bails to tm=0 on: (1) RaiseBadFileStructure's own trigger here - the
 * pattern-id search exceeding a fixed 7-byte-per-slot upper bound
 * (Pascal's own `if j1 >= 65535 then RaiseBadFileStructure`, tightened
 * to `>= 65534` here since the immediately following read is a word AT
 * j1+1 - i.e. bytes j1+1 and j1+2 - and this port's data buffer is one
 * byte smaller than Pascal's oversized Index[0..65536] array, see the
 * CRITICAL SAFETY NOTE in this port's task ledger, MIG-0103); (2) any
 * other bounds violation this port's fixed 65536-byte buffer can't
 * tolerate the way Pascal's does; or (3) a pathologically long walk (an
 * added iteration cap - Pascal's own `until j = Index[ST_PositionsPointer]`
 * has no cap and could spin forever on a corrupt position-count byte). */
static void stc_get_time(const stc_file* f, int64_t* out_tm) {
  const uint8_t* d = f->data;
  int64_t tm = 0;
  int j = -1;
  long iterations = 0;
  uint8_t last_position_count = d[f->positions_pointer];

  for (;;) {
    if (++iterations > (1 << 20)) goto fail;
    j++;
    {
      uint32_t j2_addr = (uint32_t)f->positions_pointer + (uint32_t)j * 2 + 1;
      uint8_t target;
      int i;
      uint32_t j1;

      if (j2_addr >= 65536) goto fail;
      target = d[j2_addr];

      i = -1;
      for (;;) {
        i++;
        j1 = f->patterns_pointer + 7u * (uint32_t)i;
        if (j1 >= 65534) goto fail; /* RaiseBadFileStructure, tightened */
        if (d[j1] == target) break;
      }
      j1 = rd16(d, j1 + 1);

      {
        int a = 1;
        for (;;) {
          uint8_t op;
          if (++iterations > (1 << 20)) goto fail;
          if (j1 >= 65536) goto fail;
          op = d[j1];
          if (op == 255) break;
          if (op <= 0x5F || op == 0x80 || op == 0x81) {
            tm += a;
          } else if (op >= 0xA1 && op <= 0xE0) {
            a = op - 0xA0;
          } else if (op >= 0x83 && op <= 0x8E) {
            j1++;
          }
          j1++;
        }
      }
    }
    if (j == last_position_count) break;
  }

  *out_tm = tm * f->delay;
  return;
fail:
  *out_tm = 0;
}

/* ModTypes variant 1 (Players.pas:112-115): ST_Delay@0 (byte)
 * ST_PositionsPointer@1 (word) ST_OrnamentsPointer@3 (word)
 * ST_PatternsPointer@5 (word) ST_Name[0..17]@7 ST_Size@25 (word, unused
 * at runtime). Unlike GTR/FLS, no load-time pointer relocation is needed
 * (LoadTrackerModule has no FT.STC-specific branch). */
stc_file_status stc_file_load(stc_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;

  memset(f, 0, sizeof(*f));

  if (size < 7) return STC_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  f->delay = f->data[0];
  f->positions_pointer = rd16(f->data, 1);
  f->ornaments_pointer = rd16(f->data, 3);
  f->patterns_pointer = rd16(f->data, 5);

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * STC_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3160-3214, InitTrackerModule's shared FT.STC/ST1/ST3
   * branch. */
  f->delay_counter = 1;
  f->transposition = rb(f->data, (uint32_t)f->positions_pointer + 2);

  {
    uint8_t target = rb(f->data, (uint32_t)f->positions_pointer + 1);
    uint32_t i = 0;
    while (rb(f->data, f->patterns_pointer + 7 * i) != target) i++;
    f->chan_a.address_in_pattern = rd16(f->data, f->patterns_pointer + 7 * i + 1);
    f->chan_b.address_in_pattern = rd16(f->data, f->patterns_pointer + 7 * i + 3);
    f->chan_c.address_in_pattern = rd16(f->data, f->patterns_pointer + 7 * i + 5);
  }

  f->chan_a.sample_tik_counter = -1;
  f->chan_a.ornament_pointer = (uint16_t)(f->ornaments_pointer + 1);
  f->chan_b.sample_tik_counter = -1;
  f->chan_b.ornament_pointer = (uint16_t)(f->ornaments_pointer + 1);
  f->chan_c.sample_tik_counter = -1;
  f->chan_c.ornament_pointer = (uint16_t)(f->ornaments_pointer + 1);

  f->global_tick_counter = 0;

  f->do_loop = false;
  f->real_end_all = false;
  stc_get_time(f, &f->global_tick_max); /* MIG-0101 */

  return STC_FILE_OK;
}

/* Players.pas:9329-9396, PatternInterpreter. */
static void pattern_interpreter(stc_file* f, ay_chip* chip, stc_channel* chan) {
  bool quit = false;

  do {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op <= 0x5F) {
      chan->note = op;
      chan->sample_tik_counter = 32;
      chan->position_in_sample = 0;
      quit = true;
    } else if (op <= 0x6F) {
      uint8_t target = (uint8_t)(op - 0x60);
      uint32_t k = 0;
      while (rb(f->data, 0x1B + 0x63 * k) != target) k++;
      chan->sample_pointer = (uint16_t)(0x1C + 0x63 * k);
    } else if (op <= 0x7F) {
      uint8_t target = (uint8_t)(op - 0x70);
      uint32_t k = 0;
      while (rb(f->data, f->ornaments_pointer + 0x21 * k) != target) k++;
      chan->ornament_pointer = (uint16_t)(f->ornaments_pointer + 0x21 * k + 1);
      chan->envelope_enabled = false;
    } else if (op == 0x80) {
      chan->sample_tik_counter = -1;
      quit = true;
    } else if (op == 0x81) {
      quit = true;
    } else if (op == 0x82) {
      uint32_t k = 0;
      while (rb(f->data, f->ornaments_pointer + 0x21 * k) != 0) k++;
      chan->ornament_pointer = (uint16_t)(f->ornaments_pointer + 0x21 * k + 1);
      chan->envelope_enabled = false;
    } else if (op <= 0x8E) {
      ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(op - 0x80));
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chip->reg[11] = rb(f->data, chan->address_in_pattern);
      chan->envelope_enabled = true;
      {
        uint32_t k = 0;
        while (rb(f->data, f->ornaments_pointer + 0x21 * k) != 0) k++;
        chan->ornament_pointer = (uint16_t)(f->ornaments_pointer + 0x21 * k + 1);
      }
    } else {
      chan->number_of_notes_to_skip = (uint8_t)(op - 0xA1);
    }
    chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
  } while (!quit);
  chan->note_skip_counter = (int8_t)chan->number_of_notes_to_skip;
}

/* Players.pas:9398-9443, GetRegisters. */
static void get_registers(stc_file* f, ay_chip* chip, stc_channel* chan,
                           uint8_t* temp_mixer) {
  if (chan->sample_tik_counter >= 0) {
    chan->sample_tik_counter--;
    chan->position_in_sample = (uint8_t)((chan->position_in_sample + 1) & 0x1F);
    if (chan->sample_tik_counter == 0) {
      uint8_t loop_byte = rb(f->data, (uint32_t)chan->sample_pointer + 0x60);
      if (loop_byte != 0) {
        chan->position_in_sample = (uint8_t)(loop_byte & 0x1F);
        chan->sample_tik_counter =
            (int8_t)(rb(f->data, (uint32_t)chan->sample_pointer + 0x61) + 1);
      } else {
        chan->sample_tik_counter = -1;
      }
    }
  }
  if (chan->sample_tik_counter >= 0) {
    uint32_t i = (uint32_t)(((chan->position_in_sample - 1) & 0x1F) * 3) +
                 chan->sample_pointer;
    uint8_t b1 = rb(f->data, i + 1);
    if (b1 & 0x80) {
      *temp_mixer |= 64;
    } else {
      chip->reg[6] = (uint8_t)(b1 & 0x1F);
    }
    if (b1 & 0x40) *temp_mixer |= 8;
    chan->amplitude = (uint8_t)(rb(f->data, i) & 15);

    uint8_t j = (uint8_t)(
        chan->note +
        rb(f->data, (uint32_t)chan->ornament_pointer +
                        ((chan->position_in_sample - 1) & 0x1F)) +
        f->transposition);
    if (j > 95) j = 95;

    int hi_nib = ((int)rb(f->data, i) & 0xF0) << 4;
    int ton;
    if (b1 & 0x20)
      ton = ST_TABLE[j] + rb(f->data, i + 2) + hi_nib;
    else
      ton = ST_TABLE[j] - rb(f->data, i + 2) - hi_nib;
    chan->ton = (uint16_t)(ton & 0xFFF);

    if (chan->envelope_enabled) chan->amplitude = (uint8_t)(chan->amplitude | 16);
  } else {
    chan->amplitude = 0;
  }
  *temp_mixer >>= 1;
}

/* Players.pas:9325-9515, STC_Get_Registers. MIG-0108: the
 * CheckLoopAndStop-equivalent check now lives in stc_file_make_buffer's
 * tick loop instead of here (see its own comment) - functionally
 * equivalent since nothing else touches global_tick_counter in
 * between. MIG-0112: `chip` is the target AY register file this frame's
 * writes land in - `&f->ay.chip` for standalone/self playback, or an
 * EXTERNAL chip (another player's ay_engine.chip2) when this format is
 * playlist-paired as Turbosound's second voice (Players.pas's own
 * SoundChip[CNum] addressing, generalized here as an explicit pointer
 * instead of an index since this port's per-format structs don't share
 * one global SoundChip array - see player.h's player_step_registers). */
static void stc_get_registers(stc_file* f, ay_chip* chip) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->delay_counter = f->delay;

    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter < 0) {
      if (rb(f->data, f->chan_a.address_in_pattern) == 255) {
        if (f->current_position == rb(f->data, f->positions_pointer))
          f->current_position = 0;
        else
          f->current_position++;
        f->transposition = rb(f->data, (uint32_t)f->positions_pointer + 2 +
                                            (uint32_t)f->current_position * 2);
        {
          uint8_t target = rb(f->data, (uint32_t)f->positions_pointer + 1 +
                                            (uint32_t)f->current_position * 2);
          uint32_t i = 0;
          while (rb(f->data, f->patterns_pointer + 7 * i) != target) i++;
          f->chan_a.address_in_pattern =
              rd16(f->data, f->patterns_pointer + 7 * i + 1);
          f->chan_b.address_in_pattern =
              rd16(f->data, f->patterns_pointer + 7 * i + 3);
          f->chan_c.address_in_pattern =
              rd16(f->data, f->patterns_pointer + 7 * i + 5);
        }
      }
      pattern_interpreter(f, chip, &f->chan_a);
    }
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter < 0) pattern_interpreter(f, chip, &f->chan_b);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter < 0) pattern_interpreter(f, chip, &f->chan_c);
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

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) + one stc_get_registers
 * call - the reusable "advance one interrupt frame's worth of registers
 * into `chip`" building block MIG-0112's playlist-pairing driver needs
 * (player_step_registers, player.c). Returns false once this format's
 * own natural end is reached (mirrors Real_End[CNum] going true) - the
 * caller (whether stc_file_make_buffer below, standalone, or a pairing
 * driver combining two formats) stops calling this once it returns
 * false, exactly matching MakeBufferTracker's own `Real_End_All :=
 * Real_End_All and Real_End[CNum]` accumulation. */
bool stc_file_step_registers(stc_file* f, ay_chip* chip) {
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
  stc_get_registers(f, chip);
  return true;
}

int stc_file_make_buffer(stc_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(STC_FILE_AY_FREQ_DEF /
                    (STC_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    /* Players.pas: STC_Get_Registers's own first statement, `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0108/MIG-0112) - stc_file_step_registers is the shared
     * building block player_step_registers (player.c) also uses when
     * this format is playlist-paired as Turbosound's second voice; here
     * it targets this file's own private chip (standalone use). */
    if (!stc_file_step_registers(f, &f->ay.chip)) break;
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
