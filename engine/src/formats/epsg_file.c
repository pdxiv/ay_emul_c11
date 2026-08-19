#include "ay_engine/formats/epsg_file.h"

#include <stdlib.h>
#include <string.h>

#define EPSG_HEADER_LEN 16
#define EPSG_RECORD_LEN 5

static uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/* TEPSGRec's own 3-byte-LE TSt field (see epsg_file.h's header comment
 * on why the top byte is always implicitly 0). */
static int32_t rd_tst(const uint8_t* p) {
  return (int32_t)((uint32_t)p[2] | ((uint32_t)p[3] << 8) |
                    ((uint32_t)p[4] << 16));
}
static bool is_sentinel(const uint8_t* p) {
  return p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF && p[3] == 0xFF &&
         p[4] == 0xFF;
}

/* AY.pas:1137-1157, SynthesizerEPSG. */
static void synthesizer_epsg(epsg_file* f) {
  ay_engine* ay = &f->ay;
  if (!ay->int_flag) {
    int32_t number_of_ay_takts = f->ay_takt - f->previous_ay_takt;
    int64_t n_of_tiks =
        ay->number_of_tiks + (int64_t)number_of_ay_takts * ay->frq_ay_by_frq_z80;
    if ((n_of_tiks >> 32) == 0) return; /* Pascal: if N_Of_Tiks.hi = 0 */
    ay->number_of_tiks = n_of_tiks;
    f->previous_ay_takt = f->ay_takt;
  } else {
    ay->int_flag = false;
  }
  ay_synthesizer_dispatch(ay);
}

/* Players.pas:4034-4038's InitForAllTypes FT.EPSG branch, replayed on
 * loop-restart. */
static void reset_for_loop(epsg_file* f) {
  f->pos = EPSG_HEADER_LEN;
  f->previous_ay_takt = 0;
  ay_engine_reset_chip(&f->ay, true);
}

epsg_file_status epsg_file_load(epsg_file* f, const uint8_t* data,
                                 size_t size, int ay_freq, int frq_z80,
                                 int sample_rate) {
  uint8_t selector;

  memset(f, 0, sizeof(*f));

  if (size < 6) return EPSG_FILE_ERR_TRUNCATED;
  if (memcmp(data, "EPSG", 4) != 0 || data[4] != 0x1A)
    return EPSG_FILE_ERR_BAD_HEADER;

  selector = data[5];
  if (selector == 0) {
    f->epsg_tstate_max = 70908;
  } else if (selector == 1) {
    f->epsg_tstate_max = 71680;
  } else if (selector == 255) {
    if (size < 10) return EPSG_FILE_ERR_TRUNCATED;
    f->epsg_tstate_max = (int32_t)le32(data + 6);
  } else {
    return EPSG_FILE_ERR_BAD_HEADER; /* Mes_UnsupportedEPSGCType */
  }
  if (size < EPSG_HEADER_LEN) return EPSG_FILE_ERR_TRUNCATED;

  f->data = (uint8_t*)malloc(size);
  memcpy(f->data, data, size);
  f->data_size = size;
  f->pos = EPSG_HEADER_LEN;

  f->frq_z80 = frq_z80;

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks = (uint32_t)(8192.0 / sample_rate * ay_freq + 0.5);
  f->ay.tik_re = f->ay.delay_in_tiks;
  f->ay.frq_ay_by_frq_z80 = (int64_t)(
      (double)ay_freq / frq_z80 / 8.0 * 4294967296.0 + 0.5);
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  f->previous_ay_takt = 0;
  f->ay_takt = 0;
  f->ay_reg = 0;
  f->ay_data = 0;
  f->flg = 0;

  /* Players.pas:16942-16965's GetTime scan -> Convs.pas:763's EPSG2PSG
   * ProgrMax formula (see epsg_file.h's own comment). */
  {
    int64_t tm_count = 0;
    size_t p = EPSG_HEADER_LEN;
    bool last_was_sentinel = false;
    int32_t last_tst = 0;
    double time_ms, progr_max;

    while (p + EPSG_RECORD_LEN <= size) {
      last_was_sentinel = is_sentinel(data + p);
      if (last_was_sentinel) {
        tm_count++;
        last_tst = 0;
      } else {
        last_tst = rd_tst(data + p);
      }
      p += EPSG_RECORD_LEN;
    }
    {
      double j = last_was_sentinel ? 0.0 : (double)last_tst;
      time_ms = (double)((int64_t)(((double)tm_count /
                                        ((double)frq_z80 / f->epsg_tstate_max) +
                                    j / (double)frq_z80) *
                                        1000.0 +
                                    0.5));
    }
    progr_max = (time_ms / 1000.0) * ((double)frq_z80 / (double)f->epsg_tstate_max);
    f->global_tick_max = (int64_t)(progr_max + 0.5);
    if (f->global_tick_max < 0) f->global_tick_max = 0;
  }
  f->global_tick_counter = 0;
  f->do_loop = false;
  f->real_end_all = false;
  return EPSG_FILE_OK;
}

void epsg_file_free(epsg_file* f) {
  free(f->data);
  f->data = NULL;
}

/* Players.pas:8842-8905, MakeBufferEPSG. */
int epsg_file_make_buffer(epsg_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;

  ay->buf = buf;
  ay->buf_len = 0;
  ay->buffer_length = buffer_length;
  ay->sample_bits = 16;

  if (ay->int_flag) {
    synthesizer_epsg(f);
    if (ay->int_flag) return ay->buf_len;
    if (f->flg != 0) {
      ay_chip_set_ay_register(&ay->chip, f->ay_reg, f->ay_data);
    }
  }

  if (f->pos >= f->data_size) {
    if (!f->do_loop) {
      f->real_end_all = true;
      return ay->buf_len;
    }
    reset_for_loop(f);
  }

  while (!f->real_end_all && ay->buf_len < buffer_length) {
    if (f->pos + EPSG_RECORD_LEN > f->data_size) {
      f->real_end_all = true; /* truncated trailing partial record */
      break;
    }
    if (is_sentinel(f->data + f->pos)) {
      f->pos += EPSG_RECORD_LEN;
      f->global_tick_counter++;
      f->flg = 0;
      f->ay_takt = 0;
      f->previous_ay_takt -= f->epsg_tstate_max;
      synthesizer_epsg(f);
    } else {
      f->ay_reg = f->data[f->pos];
      f->ay_data = f->data[f->pos + 1];
      f->ay_takt = rd_tst(f->data + f->pos);
      f->pos += EPSG_RECORD_LEN;
      f->flg = 1;
      synthesizer_epsg(f);
      if (!ay->int_flag) {
        ay_chip_set_ay_register(&ay->chip, f->ay_reg, f->ay_data);
      }
    }
    if (f->pos >= f->data_size && !ay->int_flag) {
      if (!f->do_loop) {
        f->real_end_all = true;
      } else {
        reset_for_loop(f);
      }
    }
  }
  return ay->buf_len;
}

/* Players.pas:8907-8922, EPSG_Get_Registers. */
bool epsg_file_step_registers(epsg_file* f) {
  if (f->real_end_all) return false;
  if (f->global_tick_counter >= f->global_tick_max) {
    if (f->do_loop) {
      f->global_tick_counter = f->global_tick_max;
    } else {
      f->real_end_all = true;
      return false;
    }
  }

  if (f->pos < f->data_size) {
    for (;;) {
      if (f->pos + EPSG_RECORD_LEN > f->data_size) break;
      {
        bool sentinel = is_sentinel(f->data + f->pos);
        if (!sentinel) {
          ay_chip_set_ay_register(&f->ay.chip, f->data[f->pos],
                                   f->data[f->pos + 1]);
        }
        f->pos += EPSG_RECORD_LEN;
        if (sentinel) break;
      }
      if (f->pos >= f->data_size) break;
    }
  }

  f->global_tick_counter++;
  if (f->global_tick_counter >= f->global_tick_max) {
    if (f->do_loop) {
      f->global_tick_counter = f->global_tick_max;
    } else {
      f->real_end_all = true;
    }
  }
  return true;
}
