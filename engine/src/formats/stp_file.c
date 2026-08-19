#include "ay_engine/formats/stp_file.h"

#include <string.h>

/* Players.pas:973-984, ST_Table - same table FLS/STC use (duplicated here
 * per this project's per-file convention). */
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

/* Players.pas:954: KsaId = 'KSA SOFTWARE COMPILATION OF ' (28 chars) -
 * the signature "else if FType = FT.STP" (7498-7530) checks for at
 * file offset 10 before a title is present at all. */
static const char STP_KSA_ID[] = "KSA SOFTWARE COMPILATION OF "; /* 28 chars + NUL */

/* Players.pas:15184-15214, GetTimeSTP - computes the song's total duration
 * (Tm) and loop-point tick (Lp) by walking the position list exactly once
 * following ONLY channel A's pattern opcode stream (the original really
 * does only one PWord lookup / one Index[j1] walk - STP note-skip/tempo
 * is a single shared value, not per-channel, so this suffices to derive
 * the real elapsed-row count). See stp_file.h's `loop_tick` comment for
 * the Lp/Tm scaling asymmetry this replicates verbatim from the original.
 * The original has NO iteration cap at all on the `while Index[j1] <> 0`
 * scan (unlike GetTimePT3/GetTimeASC's own DLCatcher) - this port adds
 * one (iter_cap) purely as a hostile-input safety net, matching this
 * project's other GetTimeXXX ports; a real/well-formed file never gets
 * close to it. On a malformed file (this safety cap tripping), returns
 * 0/0 rather than raising - stp_file_load has already succeeded by the
 * time this runs, so a duration-precompute failure degrades to "no known
 * duration" rather than failing the whole load. */
static void stp_get_time(const stp_file* f, int64_t* out_tm,
                          int64_t* out_lp) {
  const uint8_t* d = f->data;
  int64_t tm = 0, lp = 0;
  int a = 1;
  int num_positions = rb(d, f->positions_pointer);
  int loop_pos = rb(d, (uint32_t)f->positions_pointer + 1);
  int64_t iterations = 0;
  const int64_t iter_cap = 1 << 20; /* MIG safety cap - not in the original */
  int i;

  for (i = 0; i < num_positions; i++) {
    uint32_t j1;
    uint8_t off;

    if (i == loop_pos) lp = tm * f->delay;

    off = rb(d, (uint32_t)f->positions_pointer + 2 + (uint32_t)i * 2);
    j1 = rd16(d, (uint32_t)f->patterns_pointer + off);

    for (;;) {
      uint8_t op = rb(d, j1);
      if (op == 0) break;
      if (++iterations > iter_cap) {
        *out_tm = 0;
        *out_lp = 0;
        return;
      }
      if ((op >= 1 && op <= 0x60) || (op >= 0xd0 && op <= 0xef)) {
        tm += a;
      } else if (op >= 0x80 && op <= 0xbf) {
        a = op - 0x7f;
      } else if ((op >= 0xc0 && op <= 0xcf) || op == 0xf0) {
        j1 = (j1 + 1) & 0xFFFF;
      }
      j1 = (j1 + 1) & 0xFFFF;
    }
  }
  tm *= f->delay;
  *out_tm = tm;
  *out_lp = lp;
}

/* ModTypes variant 4 (Players.pas:124-127): STP_Delay@0 (byte)
 * STP_PositionsPointer@1 (word) STP_PatternsPointer@3 (word)
 * STP_OrnamentsPointer@5 (word) STP_SamplesPointer@7 (word)
 * STP_Init_Id@9 (byte, unused at runtime - see file comment). */
stp_file_status stp_file_load(stp_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;

  memset(f, 0, sizeof(*f));

  if (size < 10) return STP_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);
  int mlen = (int)size;

  /* Players.pas: "else if FType = FT.STP" (7498-7530). */
  if (size >= 38 && memcmp(f->data + 10, STP_KSA_ID, 28) == 0) {
    copy_fixed_field(f->data, size, 38, 25, f->title, sizeof(f->title));
  }

  f->delay = f->data[0];
  f->positions_pointer = rd16(f->data, 1);
  f->patterns_pointer = rd16(f->data, 3);
  f->ornaments_pointer = rd16(f->data, 5);
  f->samples_pointer = rd16(f->data, 7);

  /* Players.pas:2359-2382: with MAddr=0 (this project's headless-loading
   * convention), the pointer-relocation loop is a byte-for-byte no-op;
   * only its bounds-validation half is replicated. */
  int i1 = (mlen - (int)f->patterns_pointer) / 2;
  if (i1 < 0 || i1 > 255) return STP_FILE_ERR_BAD_HEADER;

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * STP_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3439-3488, InitTrackerModule's shared FT.STP/FT.STF
   * branch. */
  f->delay_counter = 1;
  f->transposition = rb(f->data, (uint32_t)f->positions_pointer + 3);
  {
    uint8_t off = rb(f->data, (uint32_t)f->positions_pointer + 2);
    f->chan_a.address_in_pattern = rd16(f->data, f->patterns_pointer + off);
    f->chan_b.address_in_pattern = rd16(f->data, f->patterns_pointer + off + 2);
    f->chan_c.address_in_pattern = rd16(f->data, f->patterns_pointer + off + 4);
  }

  {
    uint16_t sp = rd16(f->data, f->samples_pointer);
    uint8_t loop_s = rb(f->data, sp);
    sp = (uint16_t)(sp + 1);
    uint8_t len_s = rb(f->data, sp);
    sp = (uint16_t)(sp + 1);
    f->chan_a.sample_pointer = sp;
    f->chan_a.loop_sample_position = loop_s;
    f->chan_a.sample_length = len_s;
    f->chan_b.sample_pointer = sp;
    f->chan_b.loop_sample_position = loop_s;
    f->chan_b.sample_length = len_s;
    f->chan_c.sample_pointer = sp;
    f->chan_c.loop_sample_position = loop_s;
    f->chan_c.sample_length = len_s;

    uint16_t op = rd16(f->data, f->ornaments_pointer);
    uint8_t loop_o = rb(f->data, op);
    op = (uint16_t)(op + 1);
    uint8_t len_o = rb(f->data, op);
    op = (uint16_t)(op + 1);
    f->chan_a.ornament_pointer = op;
    f->chan_a.loop_ornament_position = loop_o;
    f->chan_a.ornament_length = len_o;
    f->chan_b.ornament_pointer = op;
    f->chan_b.loop_ornament_position = loop_o;
    f->chan_b.ornament_length = len_o;
    f->chan_c.ornament_pointer = op;
    f->chan_c.loop_ornament_position = loop_o;
    f->chan_c.ornament_length = len_o;
  }

  f->global_tick_counter = 0;
  f->global_tick_max = 0;
  f->loop_tick = 0;
  f->do_loop = false;
  f->real_end_all = false;
  stp_get_time(f, &f->global_tick_max, &f->loop_tick); /* MIG-0101-style */

  return STP_FILE_OK;
}

/* Players.pas:9521-9594, PatternInterpreter. */
static void pattern_interpreter(stp_file* f, ay_chip* chip, stp_channel* chan) {
  bool quit = false;

  do {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op >= 1 && op <= 0x60) {
      chan->note = (uint8_t)(op - 1);
      chan->position_in_sample = 0;
      chan->position_in_ornament = 0;
      chan->current_ton_sliding = 0;
      chan->enabled = true;
      quit = true;
    } else if (op >= 0x61 && op <= 0x6F) {
      uint32_t base = f->samples_pointer + (uint32_t)(op - 0x61) * 2;
      chan->sample_pointer = rd16(f->data, base);
      chan->loop_sample_position = rb(f->data, chan->sample_pointer);
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
      chan->sample_length = rb(f->data, chan->sample_pointer);
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
    } else if (op >= 0x70 && op <= 0x7F) {
      uint32_t base = f->ornaments_pointer + (uint32_t)(op - 0x70) * 2;
      chan->ornament_pointer = rd16(f->data, base);
      chan->loop_ornament_position = rb(f->data, chan->ornament_pointer);
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->ornament_length = rb(f->data, chan->ornament_pointer);
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->envelope_enabled = false;
      chan->glissade = 0;
    } else if (op >= 0x80 && op <= 0xBF) {
      chan->number_of_notes_to_skip = (uint8_t)(op - 0x80);
    } else if (op >= 0xC0 && op <= 0xCF) {
      if (op != 0xC0) {
        ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(op - 0xC0));
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
        chip->reg[11] = rb(f->data, chan->address_in_pattern);
      }
      chan->envelope_enabled = true;
      chan->loop_ornament_position = 0;
      chan->glissade = 0;
      chan->ornament_length = 1;
    } else if (op >= 0xD0 && op <= 0xDF) {
      chan->enabled = false;
      quit = true;
    } else if (op >= 0xE0 && op <= 0xEF) {
      quit = true;
    } else if (op == 0xF0) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->glissade = (int8_t)rb(f->data, chan->address_in_pattern);
    } else if (op >= 0xF1) {
      chan->volume = (uint8_t)(op - 0xF1);
    }
    chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
  } while (!quit);
  chan->note_skip_counter = (int8_t)chan->number_of_notes_to_skip;
}

/* Players.pas:9596-9639, GetRegisters. */
static void get_registers(stp_file* f, ay_chip* chip, stp_channel* chan,
                           uint8_t* temp_mixer) {
  if (chan->enabled) {
    chan->current_ton_sliding =
        (int16_t)(chan->current_ton_sliding + chan->glissade);

    uint8_t j;
    if (chan->envelope_enabled) {
      j = (uint8_t)(chan->note + f->transposition);
    } else {
      j = (uint8_t)(chan->note + f->transposition +
                    rb(f->data, (uint32_t)chan->ornament_pointer +
                                    chan->position_in_ornament));
    }
    if (j > 95) j = 95;

    uint8_t b0 = rb(f->data, (uint32_t)chan->sample_pointer +
                                  (uint32_t)chan->position_in_sample * 4);
    uint8_t b1 = rb(f->data, (uint32_t)chan->sample_pointer +
                                  (uint32_t)chan->position_in_sample * 4 + 1);
    int ton = ST_TABLE[j] + chan->current_ton_sliding +
              rd16(f->data, (uint32_t)chan->sample_pointer +
                                (uint32_t)chan->position_in_sample * 4 + 2);
    chan->ton = (uint16_t)(ton & 0xFFF);

    uint8_t amp = (uint8_t)((b0 & 15) - chan->volume);
    if ((int8_t)amp < 0) amp = 0;
    if ((b1 & 1) && chan->envelope_enabled) amp = (uint8_t)(amp | 16);
    chan->amplitude = amp;

    *temp_mixer = (uint8_t)(((b0 >> 1) & 0x48) | *temp_mixer);
    if ((int8_t)b0 >= 0) chip->reg[6] = (uint8_t)((b1 >> 1) & 31);

    chan->position_in_ornament++;
    if (chan->position_in_ornament >= chan->ornament_length)
      chan->position_in_ornament = chan->loop_ornament_position;
    chan->position_in_sample++;
    if (chan->position_in_sample >= chan->sample_length) {
      chan->position_in_sample = chan->loop_sample_position;
      if ((int8_t)chan->loop_sample_position < 0) chan->enabled = false;
    }
  } else {
    *temp_mixer |= 0x48;
    chan->amplitude = 0;
  }
  *temp_mixer >>= 1;
}

/* Players.pas:9517-9702, STP_Get_Registers. MIG-0108: the
 * CheckLoopAndStop-equivalent check now lives in stp_file_make_buffer's
 * tick loop instead of here (see its own comment) - functionally
 * equivalent since nothing else touches global_tick_counter in
 * between. MIG-0112: `chip` is the target AY register file this frame's
 * writes land in - see stc_file.c's stc_get_registers for the full
 * rationale. */
static void stp_get_registers(stp_file* f, ay_chip* chip) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->delay_counter = f->delay;

    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter < 0) {
      if (rb(f->data, f->chan_a.address_in_pattern) == 0) {
        f->current_position++;
        if (f->current_position == rb(f->data, f->positions_pointer))
          f->current_position = rb(f->data, (uint32_t)f->positions_pointer + 1);
        {
          uint8_t off = rb(f->data, (uint32_t)f->positions_pointer + 2 +
                                         (uint32_t)f->current_position * 2);
          f->chan_a.address_in_pattern = rd16(f->data, f->patterns_pointer + off);
          f->chan_b.address_in_pattern =
              rd16(f->data, f->patterns_pointer + off + 2);
          f->chan_c.address_in_pattern =
              rd16(f->data, f->patterns_pointer + off + 4);
        }
        f->transposition = rb(f->data, (uint32_t)f->positions_pointer + 3 +
                                            (uint32_t)f->current_position * 2);
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

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) + one stp_get_registers
 * call - see stc_file.c's stc_file_step_registers for the full
 * rationale. */
bool stp_file_step_registers(stp_file* f, ay_chip* chip) {
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
  stp_get_registers(f, chip);
  return true;
}

int stp_file_make_buffer(stp_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(STP_FILE_AY_FREQ_DEF /
                    (STP_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    /* Players.pas: STP_Get_Registers's own first statement, `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0108/MIG-0112) - stp_file_step_registers is the shared
     * building block player_step_registers (player.c) also uses when
     * this format is playlist-paired as Turbosound's second voice; here
     * it targets this file's own private chip (standalone use). Force_Loop
     * (the original's TS-pair "continue playback of the shorter module"
     * case) doesn't apply - TSMode isn't ported (MIG-0007), so only
     * Do_Loop matters. */
    if (!stp_file_step_registers(f, &f->ay.chip)) break;
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
