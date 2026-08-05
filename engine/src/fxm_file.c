#include "ay_engine/fxm_file.h"

#include <string.h>

#include "ay_engine/trace_log.h"

/* Players.pas:1000-1011, FXM_Table. */
static const uint16_t FXM_TABLE[84] = {
    0x0FBF, 0x0EDC, 0x0E07, 0x0D3D, 0x0C7F, 0x0BCC, 0x0B22, 0x0A82, 0x09EB,
    0x095D, 0x08D6, 0x0857, 0x07DF, 0x076E, 0x0703, 0x069F, 0x0640, 0x05E6,
    0x0591, 0x0541, 0x04F6, 0x04AE, 0x046B, 0x042C, 0x03F0, 0x03B7, 0x0382,
    0x034F, 0x0320, 0x02F3, 0x02C8, 0x02A1, 0x027B, 0x0257, 0x0236, 0x0216,
    0x01F8, 0x01DC, 0x01C1, 0x01A8, 0x0190, 0x0179, 0x0164, 0x0150, 0x013D,
    0x012C, 0x011B, 0x010B, 0x00FC, 0x00EE, 0x00E0, 0x00D4, 0x00C8, 0x00BD,
    0x00B2, 0x00A8, 0x009F, 0x0096, 0x008D, 0x0085, 0x007E, 0x0077, 0x0070,
    0x006A, 0x0064, 0x005E, 0x0059, 0x0054, 0x004F, 0x004B, 0x0047, 0x0043,
    0x003F, 0x003B, 0x0038, 0x0035, 0x0032, 0x002F, 0x002D, 0x002A, 0x0028,
    0x0025, 0x0023, 0x0021};

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* Players.pas:2237-2240,2249,2257-2261: FXM's on-disk layout is unlike
 * every other format ported so far - the first 6 bytes of the file are
 * NOT part of Module.Index at all. Bytes 4-5 hold a little-endian load
 * address; the real content (starting at file byte 6) gets copied to
 * Module.Index[address..], i.e. the load address is both skipped past
 * on the input side AND applied as the destination offset - unlike
 * GTR/FLS's on-disk-pointer relocation, here the copy destination itself
 * moves. All of FXM's internal pointers (channel Address_In_Pattern,
 * OrnamentPointer, SamplePointer, jump/call targets) are then absolute
 * offsets into Index[] with this address already baked in - confirmed by
 * LoadTrackerModule having no separate FT.FXM relocation branch
 * afterward (unlike PT2/PT3/FLS/GTR/ST1/ST3/STF, all of which do). */
fxm_file_status fxm_file_load(fxm_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;

  memset(f, 0, sizeof(*f));

  if (size < 12) return FXM_FILE_ERR_TRUNCATED;

  uint16_t address = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
  const uint8_t* content = data + 6;
  size_t content_len = size - 6;
  if (content_len > (size_t)(65536 - address))
    content_len = (size_t)(65536 - address);
  memcpy(f->data + address, content, content_len);

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * FXM_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3041-3088, InitTrackerModule's FT.FXM branch. */
  f->noise_base = 0;
  f->chan_a.address_in_pattern = rd16(f->data, address);
  f->chan_b.address_in_pattern = rd16(f->data, (uint32_t)address + 2);
  f->chan_c.address_in_pattern = rd16(f->data, (uint32_t)address + 4);

  f->chan_a.note_skip_counter = 1;
  f->chan_a.fxm_mixer = 8;
  f->chan_b.note_skip_counter = 1;
  f->chan_b.fxm_mixer = 8;
  f->chan_c.note_skip_counter = 1;
  f->chan_c.fxm_mixer = 8;

  f->global_tick_counter = 0;

  return FXM_FILE_OK;
}

/* Players.pas:11702-11713, RealGetRegisters. */
static void real_get_registers(fxm_file* f, fxm_channel* chan) {
  f->ay.chip.reg[6] = (uint8_t)(f->noise_base & 31);
  chan->b2e = false;
  if (chan->ton != 0)
    chan->amplitude = (uint8_t)(chan->volume & 15);
  else
    chan->amplitude = 0;
}

/* Players.pas:11715-11789, GetRegisters. */
static void get_registers(fxm_file* f, fxm_channel* chan) {
  chan->sample_tik_counter--;
  if (chan->sample_tik_counter == 0) {
    for (;;) {
      uint8_t op = rb(f->data, chan->point_in_sample);
      if (op <= 0x1D) {
        chan->volume = op;
        chan->point_in_sample = (uint16_t)(chan->point_in_sample + 1);
        chan->sample_tik_counter = (int8_t)rb(f->data, chan->point_in_sample);
        chan->point_in_sample = (uint16_t)(chan->point_in_sample + 1);
        break;
      } else if (op == 0x80) {
        chan->point_in_sample =
            rd16(f->data, (uint32_t)chan->point_in_sample + 1);
      } else {
        chan->volume = (uint8_t)(op - 0x32);
        chan->point_in_sample = (uint16_t)(chan->point_in_sample + 1);
        chan->sample_tik_counter = 1;
        break;
      }
    }
  }
  if (chan->ton != 0 && !chan->b2e) {
    for (;;) {
      uint8_t op = rb(f->data, chan->point_in_ornament);
      if (op == 0x80) {
        chan->point_in_ornament =
            rd16(f->data, (uint32_t)chan->point_in_ornament + 1);
      } else if (op == 0x82) {
        chan->point_in_ornament = (uint16_t)(chan->point_in_ornament + 1);
        chan->b3e = true;
      } else if (op == 0x83) {
        chan->point_in_ornament = (uint16_t)(chan->point_in_ornament + 1);
        chan->b3e = false;
      } else if (op == 0x84) {
        chan->point_in_ornament = (uint16_t)(chan->point_in_ornament + 1);
        chan->fxm_mixer = (uint8_t)(chan->fxm_mixer ^ 9);
      } else {
        if (chan->b3e) {
          chan->note = (uint8_t)(chan->note + op);
          uint8_t b = chan->note > 0x53 ? 0x53 : chan->note;
          chan->ton = FXM_TABLE[b];
        } else {
          chan->ton = (uint16_t)(chan->ton + (int8_t)op);
        }
        chan->point_in_ornament = (uint16_t)(chan->point_in_ornament + 1);
        break;
      }
    }
  }
  real_get_registers(f, chan);
}

/* Players.pas:11791-11949, PatternInterpreter. Unlike every other format
 * ported so far, there is no common trailing address increment - every
 * branch below is fully self-contained (matches the Pascal literally:
 * `repeat case ... end until False`, no statement between `end` and
 * `until`), and the only way out of the loop is the note branch's
 * `return` (Pascal `exit`). */
static void pattern_interpreter(fxm_file* f, fxm_channel* chan) {
  chan->note_skip_counter--;
  if (chan->note_skip_counter != 0) {
    get_registers(f, chan);
    return;
  }

  for (;;) {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op <= 0x7F) {
      if (op != 0) {
        chan->note = (uint8_t)(op - 1 + chan->transposit);
        uint8_t b = chan->note > 0x53 ? 0x53 : chan->note;
        chan->ton = FXM_TABLE[b];
        chan->b3e = false;
      } else {
        chan->ton = 0;
      }
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->note_skip_counter = (int8_t)rb(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->point_in_ornament = chan->ornament_pointer;
      if (!chan->b1e) {
        chan->b1e = chan->b0e;
        chan->point_in_sample = chan->sample_pointer;
        chan->volume = rb(f->data, chan->point_in_sample);
        chan->point_in_sample = (uint16_t)(chan->point_in_sample + 1);
        chan->sample_tik_counter = (int8_t)rb(f->data, chan->point_in_sample);
        chan->point_in_sample = (uint16_t)(chan->point_in_sample + 1);
        real_get_registers(f, chan);
      } else {
        get_registers(f, chan);
      }
      return;
    } else if (op == 0x80) {
      chan->address_in_pattern =
          rd16(f->data, (uint32_t)chan->address_in_pattern + 1);
    } else if (op == 0x81) {
      if (chan->stek_len < FXM_STEK_MAX)
        chan->stek[chan->stek_len++] = (uint16_t)(chan->address_in_pattern + 3);
      chan->address_in_pattern =
          rd16(f->data, (uint32_t)chan->address_in_pattern + 1);
    } else if (op == 0x82) {
      if (chan->stek_len + 2 <= FXM_STEK_MAX) {
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
        chan->stek[chan->stek_len] = rb(f->data, chan->address_in_pattern);
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
        chan->stek[chan->stek_len + 1] = chan->address_in_pattern;
        chan->stek_len += 2;
      } else {
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 2);
      }
    } else if (op == 0x83) {
      int i = chan->stek_len;
      chan->stek[i - 2] = (uint16_t)(chan->stek[i - 2] - 1);
      if ((chan->stek[i - 2] & 255) != 0) {
        chan->address_in_pattern = chan->stek[i - 1];
      } else {
        chan->stek_len -= 2;
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      }
    } else if (op == 0x84) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->noise_base = rb(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x85) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->fxm_mixer = rb(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x86) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->ornament_pointer = rd16(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 2);
    } else if (op == 0x87) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->sample_pointer = rd16(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 2);
    } else if (op == 0x88) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->transposit = (int8_t)rb(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x89) {
      int i = chan->stek_len;
      chan->address_in_pattern = chan->stek[i - 1];
      chan->stek_len = i - 1;
    } else if (op == 0x8A) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->b0e = true;
      chan->b1e = false;
    } else if (op == 0x8B) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->b0e = false;
      chan->b1e = false;
    } else if (op == 0x8C) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 3);
    } else if (op == 0x8D) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->noise_base =
          (uint8_t)((f->noise_base + rb(f->data, chan->address_in_pattern)) & 31);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x8E) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->transposit =
          (int8_t)(chan->transposit + rb(f->data, chan->address_in_pattern));
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x8F) {
      if (chan->stek_len < FXM_STEK_MAX)
        chan->stek[chan->stek_len++] = (uint16_t)(int16_t)chan->transposit;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x90) {
      int i = chan->stek_len;
      chan->transposit = (int8_t)chan->stek[i - 1];
      chan->stek_len = i - 1;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    }
  }
}

/* Players.pas:11700-11972, FXM_Get_Registers (minus CheckLoopAndStop -
 * matches this project's other tracker-format ports' own precedent). */
static void fxm_get_registers(fxm_file* f) {
  pattern_interpreter(f, &f->chan_a);
  pattern_interpreter(f, &f->chan_b);
  pattern_interpreter(f, &f->chan_c);

  uint16_t ton_a = (uint16_t)(f->chan_a.ton & 0xFFF);
  uint16_t ton_b = (uint16_t)(f->chan_b.ton & 0xFFF);
  uint16_t ton_c = (uint16_t)(f->chan_c.ton & 0xFFF);
  f->ay.chip.reg[0] = (uint8_t)(ton_a & 0xFF);
  f->ay.chip.reg[1] = (uint8_t)(ton_a >> 8);
  f->ay.chip.reg[2] = (uint8_t)(ton_b & 0xFF);
  f->ay.chip.reg[3] = (uint8_t)(ton_b >> 8);
  f->ay.chip.reg[4] = (uint8_t)(ton_c & 0xFF);
  f->ay.chip.reg[5] = (uint8_t)(ton_c >> 8);

  ay_chip_set_ay_register_fast(&f->ay.chip, 8, f->chan_a.amplitude);
  ay_chip_set_ay_register_fast(&f->ay.chip, 9, f->chan_b.amplitude);
  ay_chip_set_ay_register_fast(&f->ay.chip, 10, f->chan_c.amplitude);

  uint8_t mixer = (uint8_t)((f->chan_a.fxm_mixer | (f->chan_b.fxm_mixer << 1) |
                             (f->chan_c.fxm_mixer << 2)) &
                            0x3F);
  ay_chip_set_ay_register_fast(&f->ay.chip, 7, mixer);

  trace_log_ay(f->global_tick_counter, "fxm_a", ton_a, f->chan_a.amplitude);
  trace_log_ay(f->global_tick_counter, "fxm_b", ton_b, f->chan_b.amplitude);
  trace_log_ay(f->global_tick_counter, "fxm_c", ton_c, f->chan_c.amplitude);
  trace_log_ay(f->global_tick_counter, "fxm_mixer", mixer, 0);

  f->global_tick_counter++;
}

int fxm_file_make_buffer(fxm_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(FXM_FILE_AY_FREQ_DEF /
                    (FXM_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    fxm_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
