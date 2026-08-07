#include "ay_engine/formats/vtx_file.h"

#include <stdlib.h>
#include <string.h>

#include "ay_engine/util/lh5.h"

/* TVTXFileHeader's fields are little-endian (unlike the AY/YM header
 * conventions, which are big-endian) - Players.pas reads them directly
 * into native (x86, LE) Pascal vars with no SwapEndian call anywhere in
 * the VTX loader, confirmed by direct reading. */
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

vtx_file_status vtx_file_load(vtx_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  uint16_t id;
  bool is_short, is_ay_chip;
  size_t pos;
  uint16_t loop;
  uint32_t chip_frq, unpack_size;
  uint8_t inter_frq;
  int str;

  memset(f, 0, sizeof(*f));

  if (size < 2) return VTX_FILE_ERR_TRUNCATED;
  id = le16(data);
  if (id != 0x5941u /* "AY" */ && id != 0x4D59u /* "YM" */ &&
      id != 0x7961u /* "ay" */ && id != 0x6D79u /* "ym" */) {
    return VTX_FILE_ERR_BAD_HEADER;
  }
  is_short = (id == 0x5941u || id == 0x4D59u);
  is_ay_chip = (id == 0x7961u || id == 0x5941u);

  pos = 2;
  if (is_short) {
    if (size < pos + 8 + 4) return VTX_FILE_ERR_TRUNCATED;
    pos += 1; /* Mode - unused (channel-mode UI detail, not needed for
               * register-plane playback correctness) */
    loop = le16(data + pos);
    pos += 2;
    chip_frq = le32(data + pos);
    pos += 4;
    inter_frq = data[pos];
    pos += 1;
    unpack_size = le32(data + pos);
    pos += 4;
  } else {
    if (size < pos + 14) return VTX_FILE_ERR_TRUNCATED;
    pos += 1; /* Mode */
    loop = le16(data + pos);
    pos += 2;
    chip_frq = le32(data + pos);
    pos += 4;
    inter_frq = data[pos];
    pos += 1;
    pos += 2; /* Year - unused (UI-only date display) */
    unpack_size = le32(data + pos);
    pos += 4;
  }

  /* Title, Author: always present (Players.pas:7678-7685). */
  for (str = 0; str < 2; str++) {
    while (pos < size && data[pos] != 0) pos++;
    if (pos >= size) return VTX_FILE_ERR_TRUNCATED;
    pos++;
  }
  /* Programm, Tracker, Comment: only the long ("ay"/"ym") header variant
   * has these (Players.pas:7686-7702). */
  if (!is_short) {
    for (str = 0; str < 3; str++) {
      while (pos < size && data[pos] != 0) pos++;
      if (pos >= size) return VTX_FILE_ERR_TRUNCATED;
      pos++;
    }
  }

  if (unpack_size == 0 || pos > size) return VTX_FILE_ERR_TRUNCATED;

  f->data = (uint8_t*)malloc(unpack_size);
  if (!f->data) return VTX_FILE_ERR_TRUNCATED;
  if (!lh5_decompress(data + pos, (int32_t)(size - pos), f->data,
                       (int32_t)unpack_size)) {
    free(f->data);
    f->data = NULL;
    return VTX_FILE_ERR_LZH_INVALID;
  }
  f->data_size = (int32_t)unpack_size;

  f->number_of_vbls = (int32_t)(unpack_size / 14);
  f->loop_vbl = (int32_t)loop;
  if (f->loop_vbl < 0) f->loop_vbl = 0;

  ay_engine_init(&f->ay);
  f->ay.chip_type = is_ay_chip ? AY_CHIP_TYPE_AY : AY_CHIP_TYPE_YM;
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * (double)chip_frq + 0.5);
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  f->position_in_vtx = 0;
  f->global_tick_counter = 0;
  f->global_tick_max = f->number_of_vbls;
  f->do_loop = false;
  f->real_end_all = false;

  /* MainWin.pas:2070's AY_Tiks_In_Interrupt: trunc(AY_Freq/
   * (Interrupt_Freq/1000*8)+0.5), from this file's own ChipFrq/InterFrq
   * header fields (Interrupt_Freq = InterFrq*1000, Players.pas:7666). */
  {
    double interrupt_freq = (double)inter_frq * 1000.0;
    f->ay_tiks_in_interrupt =
        (int64_t)((double)chip_frq / (interrupt_freq / 1000.0 * 8.0) + 0.5);
    f->interrupt_freq = interrupt_freq;
    f->ay_freq = (double)chip_frq;
  }

  return VTX_FILE_OK;
}

void vtx_file_free(vtx_file* f) {
  free(f->data);
  f->data = NULL;
}

/* Players.pas:12798-12825, VTX_YM3_YM3b_Get_Registers. */
static void vtx_get_registers(vtx_file* f) {
  ay_chip* chip = &f->ay.chip;
  int32_t k = 0; /* VTX_Offset is always 0 for VTX files */
  int i;

  for (i = 0; i <= 12; i++) {
    uint8_t b = f->data[f->position_in_vtx + k];
    switch (i) {
      case 1: case 3: case 5:
        ay_chip_set_ay_register_fast(chip, i, (uint8_t)(b & 15));
        break;
      case 6:
        ay_chip_set_ay_register_fast(chip, 6, (uint8_t)(b & 31));
        break;
      case 7:
        ay_chip_set_ay_register_fast(chip, 7, (uint8_t)(b & 63));
        break;
      case 8:
        ay_chip_set_ay_register_fast(chip, 8, (uint8_t)(b & 31));
        break;
      case 9:
        ay_chip_set_ay_register_fast(chip, 9, (uint8_t)(b & 31));
        break;
      case 10:
        ay_chip_set_ay_register_fast(chip, 10, (uint8_t)(b & 31));
        break;
      default:
        chip->reg[i] = b;
        break;
    }
    k += f->number_of_vbls;
  }
  {
    uint8_t b = f->data[f->position_in_vtx + k];
    if (b != 255) ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(b & 15));
  }

  f->global_tick_counter++;
  f->position_in_vtx++;
}

int vtx_file_make_buffer(vtx_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;

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

  if (f->global_tick_counter >= f->global_tick_max) {
    if (f->do_loop) {
      f->global_tick_counter = f->global_tick_max;
    } else {
      f->real_end_all = true;
      return ay->buf_len;
    }
  }

  while (!f->real_end_all && ay->buf_len < buffer_length) {
    vtx_get_registers(f);
    /* AY.pas:1075-1082 SynthesizerZX50. */
    if (!ay->int_flag) {
      ay->number_of_tiks = f->ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
    if (f->position_in_vtx == f->number_of_vbls)
      f->position_in_vtx = f->loop_vbl;
    if (f->global_tick_counter >= f->global_tick_max && !ay->int_flag) {
      if (f->do_loop) {
        f->global_tick_counter = f->global_tick_max;
      } else {
        f->real_end_all = true;
        return ay->buf_len;
      }
    }
  }
  return ay->buf_len;
}
