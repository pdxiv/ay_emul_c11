#include "ay_engine/psc_file.h"

#include <string.h>

/* Players.pas:959-970, ASM_Table - same table ASC uses (duplicated here
 * per this project's per-file convention). */
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

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* ModTypes variant 7 (Players.pas:145-150): PSC_MusicName[0..68]@0
 * PSC_UnknownPointer@69 (word) PSC_PatternsPointer@71 (word)
 * PSC_Delay@73 (byte) PSC_OrnamentsPointer@74 (word)
 * PSC_SamplesPointers[0..31]@76 (64B). */
psc_file_status psc_file_load(psc_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;
  int i;

  memset(f, 0, sizeof(*f));

  if (size < 140) return PSC_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  uint16_t patterns_pointer = rd16(f->data, 71);
  uint8_t delay = f->data[73];
  f->ornaments_pointer_base = rd16(f->data, 74);
  for (i = 0; i < 32; i++)
    f->samples_pointers[i] = rd16(f->data, 76 + (uint32_t)i * 2);

  /* Players.pas:2600-2606: Version derivation (folded in at load time). */
  f->version = 7;
  if (f->data[8] >= '0' && f->data[8] <= '9') f->version = f->data[8] - '0';

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * PSC_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3516-3569, InitTrackerModule's FT.PSC branch. */
  f->delay_counter = 1;
  f->delay = delay;
  f->positions_pointer = patterns_pointer;
  f->lines_counter = 1;

  {
    uint16_t sp = (uint16_t)(f->samples_pointers[0] + 0x4C);
    f->chan_a.sample_pointer = sp;
    f->chan_b.sample_pointer = sp;
    f->chan_c.sample_pointer = sp;
  }
  {
    uint16_t op = (uint16_t)(rd16(f->data, f->ornaments_pointer_base) +
                              f->ornaments_pointer_base);
    f->chan_a.ornament_pointer = op;
    f->chan_b.ornament_pointer = op;
    f->chan_c.ornament_pointer = op;
  }

  f->chan_a.note_skip_counter = 1;
  f->chan_b.note_skip_counter = 1;
  f->chan_c.note_skip_counter = 1;

  f->global_tick_counter = 0;

  return PSC_FILE_OK;
}

/* Players.pas:10057-10235, PatternInterpreter. Like FTC (and unlike ASC/
 * GTR/STC), there is no `break` anywhere - the loop is `repeat...until
 * quit`, so the common trailing address increment always fires, and
 * every branch's own inner increments (if any) are additive on top,
 * with no double-counting risk. chan_id (0=A,1=B,2=C) replicates the
 * Pascal source's `@Chan = @PlParams[CNum].PSC_B` pointer-identity
 * checks for opcodes $7A/$7B. */
static void pattern_interpreter(psc_file* f, psc_channel* chan, int chan_id) {
  bool quit = false;
  bool b1b = false, b2b = false, b3b = false, b4b = false, b5b = false,
       b6b = false, b7b = false;

  do {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op >= 0xC0) {
      chan->note_skip_counter = (int8_t)(op - 0xBF);
      quit = true;
    } else if (op >= 0xA0) { /* $A0..$BF */
      uint32_t off = f->ornaments_pointer_base + (uint32_t)(op - 0xA0) * 2;
      chan->ornament_pointer = rd16(f->data, off);
      if (f->version > 3)
        chan->ornament_pointer =
            (uint16_t)(chan->ornament_pointer + f->ornaments_pointer_base);
    } else if (op >= 0x7E) { /* $7E..$9F */
      if (op >= 0x80) {
        chan->sample_pointer = f->samples_pointers[op - 0x80];
        if (f->version > 3)
          chan->sample_pointer = (uint16_t)(chan->sample_pointer + 0x4C);
      }
    } else if (op == 0x6B) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->addition_to_ton = rb(f->data, chan->address_in_pattern);
      b5b = true;
    } else if (op == 0x6C) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->addition_to_ton =
          (int16_t)(-(int8_t)rb(f->data, chan->address_in_pattern));
      b5b = true;
    } else if (op == 0x6D) {
      b4b = true;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->addition_to_ton = rb(f->data, chan->address_in_pattern);
    } else if (op == 0x6E) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->delay = rb(f->data, chan->address_in_pattern);
    } else if (op == 0x6F) {
      b1b = true;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x70) {
      b3b = true;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->volume_counter1 = rb(f->data, chan->address_in_pattern);
    } else if (op == 0x71) {
      chan->break_ornament_loop = true;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x7A) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      if (chan_id == 1) {
        ay_chip_set_ay_register_fast(
            &f->ay.chip, 13, (uint8_t)(rb(f->data, chan->address_in_pattern) & 15));
        uint16_t env = rd16(f->data, (uint32_t)chan->address_in_pattern + 1);
        f->ay.chip.reg[11] = (uint8_t)(env & 0xFF);
        f->ay.chip.reg[12] = (uint8_t)(env >> 8);
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 2);
      }
    } else if (op == 0x7B) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      if (chan_id == 1) f->noise_base = rb(f->data, chan->address_in_pattern);
    } else if (op == 0x7C) {
      b1b = false;
      b2b = true;
      b3b = false;
      b4b = false;
      b5b = false;
      b6b = false;
      b7b = false;
    } else if (op == 0x7D) {
      chan->break_sample_loop = true;
    } else if (op >= 0x58 && op <= 0x66) {
      chan->initial_volume = (int8_t)(op - 0x57);
      chan->envelope_enabled = false;
      b6b = true;
    } else if (op == 0x57) {
      chan->initial_volume = 0xF;
      chan->envelope_enabled = true;
      b6b = true;
    } else if (op <= 0x56) {
      chan->note = op;
      b6b = true;
      b7b = true;
    } else {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    }
    chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
  } while (!quit);

  if (b7b) {
    chan->break_ornament_loop = false;
    chan->ornament_enabled = true;
    chan->enabled = true;
    chan->break_sample_loop = false;
    chan->ton_slide_enabled = false;
    chan->ton_accumulator = 0;
    chan->current_ton_sliding = 0;
    chan->noise_accumulator = 0;
    chan->volume_counter = 0;
    chan->position_in_sample = 0;
    chan->position_in_ornament = 0;
  }
  if (b6b) chan->volume = (uint8_t)chan->initial_volume;
  if (b5b) {
    chan->gliss = false;
    chan->ton_slide_enabled = true;
  }
  if (b4b) {
    chan->current_ton_sliding = (int16_t)(chan->ton - ASM_TABLE[chan->note]);
    chan->gliss = true;
    if (chan->current_ton_sliding >= 0)
      chan->addition_to_ton = (int16_t)(-chan->addition_to_ton);
    chan->ton_slide_enabled = true;
  }
  if (b3b) {
    chan->volume_counter = chan->volume_counter1;
    chan->volume_inc = true;
    if (chan->volume_counter & 0x40) {
      chan->volume_counter = (uint8_t)(-(int8_t)(chan->volume_counter | 128));
      chan->volume_inc = false;
    }
    chan->volume_counter_init = chan->volume_counter;
  }
  if (b2b) {
    chan->break_ornament_loop = false;
    chan->ornament_enabled = false;
    chan->enabled = false;
    chan->break_sample_loop = false;
    chan->ton_slide_enabled = false;
  }
  if (b1b) chan->ornament_enabled = false;
}

/* Players.pas:10237-10354, GetRegisters. */
static void get_registers(psc_file* f, psc_channel* chan, uint8_t* temp_mixer) {
  if (chan->enabled) {
    uint8_t j = chan->note;
    if (chan->ornament_enabled) {
      uint8_t b = rb(f->data, (uint32_t)chan->ornament_pointer +
                                  (uint32_t)chan->position_in_ornament * 2);
      chan->noise_accumulator = (uint8_t)(chan->noise_accumulator + b);
      j = (uint8_t)(j + rb(f->data, (uint32_t)chan->ornament_pointer +
                                        (uint32_t)chan->position_in_ornament * 2 + 1));
      if ((int8_t)j < 0) j = (uint8_t)(j + 0x56);
      if (j > 0x55) j = (uint8_t)(j - 0x56);
      if (j > 0x55) j = 0x55;
      if ((b & 128) == 0) chan->loop_ornament_position = chan->position_in_ornament;
      if ((b & 64) == 0) {
        if (!chan->break_ornament_loop) {
          chan->position_in_ornament = chan->loop_ornament_position;
        } else {
          chan->break_ornament_loop = false;
          if ((b & 32) == 0) chan->ornament_enabled = false;
          chan->position_in_ornament++;
        }
      } else {
        if ((b & 32) == 0) chan->ornament_enabled = false;
        chan->position_in_ornament++;
      }
    }
    chan->note = j;
    chan->ton = rd16(f->data, (uint32_t)chan->sample_pointer +
                                  (uint32_t)chan->position_in_sample * 6);
    chan->ton_accumulator = (int16_t)(chan->ton_accumulator + chan->ton);
    chan->ton = (uint16_t)(ASM_TABLE[j] + chan->ton_accumulator);
    if (chan->ton_slide_enabled) {
      chan->current_ton_sliding =
          (int16_t)(chan->current_ton_sliding + chan->addition_to_ton);
      if (chan->gliss &&
          (((chan->current_ton_sliding < 0) && (chan->addition_to_ton <= 0)) ||
           ((chan->current_ton_sliding >= 0) && (chan->addition_to_ton >= 0))))
        chan->ton_slide_enabled = false;
      chan->ton = (uint16_t)(chan->ton + chan->current_ton_sliding);
    }
    chan->ton = (uint16_t)(chan->ton & 0xFFF);

    uint8_t b = rb(f->data, (uint32_t)chan->sample_pointer +
                                (uint32_t)chan->position_in_sample * 6 + 4);
    *temp_mixer = (uint8_t)(*temp_mixer | ((b & 9) << 3));

    int jj = 0;
    if (b & 2) jj++;
    if (b & 4) jj--;
    if (chan->volume_counter > 0) {
      chan->volume_counter--;
      if (chan->volume_counter == 0) {
        if (chan->volume_inc)
          jj++;
        else
          jj--;
        chan->volume_counter = chan->volume_counter_init;
      }
    }
    chan->volume = (uint8_t)(chan->volume + jj);
    if ((int8_t)chan->volume < 0)
      chan->volume = 0;
    else if (chan->volume > 15)
      chan->volume = 15;

    chan->amplitude = (uint8_t)(
        ((chan->volume + 1) *
         (rb(f->data, (uint32_t)chan->sample_pointer +
                          (uint32_t)chan->position_in_sample * 6 + 3) &
          15)) >>
        4);
    if (chan->envelope_enabled && (b & 16) == 0)
      chan->amplitude = (uint8_t)(chan->amplitude | 16);

    if ((chan->amplitude & 16) && (b & 8)) {
      uint16_t env = (uint16_t)(f->ay.chip.reg[11] | (f->ay.chip.reg[12] << 8));
      env = (uint16_t)(env + (int8_t)rb(f->data, (uint32_t)chan->sample_pointer +
                                                      (uint32_t)chan->position_in_sample * 6 + 2));
      f->ay.chip.reg[11] = (uint8_t)(env & 0xFF);
      f->ay.chip.reg[12] = (uint8_t)(env >> 8);
    } else {
      chan->noise_accumulator = (uint8_t)(
          chan->noise_accumulator +
          rb(f->data, (uint32_t)chan->sample_pointer +
                          (uint32_t)chan->position_in_sample * 6 + 2));
      if ((b & 8) == 0)
        f->ay.chip.reg[6] = (uint8_t)(chan->noise_accumulator & 31);
    }

    if ((b & 128) == 0) chan->loop_sample_position = chan->position_in_sample;
    if ((b & 64) == 0) {
      if (!chan->break_sample_loop) {
        chan->position_in_sample = chan->loop_sample_position;
      } else {
        chan->break_sample_loop = false;
        if ((b & 32) == 0) chan->enabled = false;
        chan->position_in_sample++;
      }
    } else {
      if ((b & 32) == 0) chan->enabled = false;
      chan->position_in_sample++;
    }
  } else {
    chan->amplitude = 0;
  }
  *temp_mixer >>= 1;
}

/* Players.pas:10053-10423, PSC_Get_Registers (minus CheckLoopAndStop -
 * matches this project's other tracker-format ports' own precedent). */
static void psc_get_registers(psc_file* f) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->lines_counter--;
    if (f->lines_counter == 0) {
      if (rb(f->data, (uint32_t)f->positions_pointer + 1) == 255)
        f->positions_pointer = rd16(f->data, (uint32_t)f->positions_pointer + 2);
      f->lines_counter = rb(f->data, (uint32_t)f->positions_pointer + 1);
      f->chan_a.address_in_pattern =
          rd16(f->data, (uint32_t)f->positions_pointer + 2);
      f->chan_b.address_in_pattern =
          rd16(f->data, (uint32_t)f->positions_pointer + 4);
      f->chan_c.address_in_pattern =
          rd16(f->data, (uint32_t)f->positions_pointer + 6);
      f->positions_pointer = (uint16_t)(f->positions_pointer + 8);
      f->chan_a.note_skip_counter = 1;
      f->chan_b.note_skip_counter = 1;
      f->chan_c.note_skip_counter = 1;
    }
    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter == 0) pattern_interpreter(f, &f->chan_a, 0);
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter == 0) pattern_interpreter(f, &f->chan_b, 1);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter == 0) pattern_interpreter(f, &f->chan_c, 2);

    f->chan_a.noise_accumulator =
        (uint8_t)(f->chan_a.noise_accumulator + f->noise_base);
    f->chan_b.noise_accumulator =
        (uint8_t)(f->chan_b.noise_accumulator + f->noise_base);
    f->chan_c.noise_accumulator =
        (uint8_t)(f->chan_c.noise_accumulator + f->noise_base);
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

int psc_file_make_buffer(psc_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(PSC_FILE_AY_FREQ_DEF /
                    (PSC_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    psc_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
