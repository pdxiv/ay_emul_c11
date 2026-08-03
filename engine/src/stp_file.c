#include "ay_engine/stp_file.h"

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

  return STP_FILE_OK;
}

/* Players.pas:9521-9594, PatternInterpreter. */
static void pattern_interpreter(stp_file* f, stp_channel* chan) {
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
        ay_chip_set_ay_register_fast(&f->ay.chip, 13, (uint8_t)(op - 0xC0));
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
        f->ay.chip.reg[11] = rb(f->data, chan->address_in_pattern);
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
static void get_registers(stp_file* f, stp_channel* chan, uint8_t* temp_mixer) {
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
    if ((int8_t)b0 >= 0) f->ay.chip.reg[6] = (uint8_t)((b1 >> 1) & 31);

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

/* Players.pas:9517-9702, STP_Get_Registers (minus CheckLoopAndStop -
 * matches this project's other tracker-format ports' own precedent). */
static void stp_get_registers(stp_file* f) {
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
      pattern_interpreter(f, &f->chan_a);
    }
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter < 0) pattern_interpreter(f, &f->chan_b);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter < 0) pattern_interpreter(f, &f->chan_c);
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

int stp_file_make_buffer(stp_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(STP_FILE_AY_FREQ_DEF /
                    (STP_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    stp_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
