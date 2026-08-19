#include "ay_engine/formats/out_file.h"

#include <stdlib.h>
#include <string.h>

/* Z80.pas:31. */
#define OUT_PORT_MASK 0xc002u
/* AY.pas:1092-1123/Players.pas:8801-8840's own fixed wraparound constant -
 * a property of the .out capture format itself (its own 16-bit ZX_Takt
 * counter's native wraparound period), independent of this emulator's
 * configurable MaxTStates setting (used only for out_conv_total_time's
 * OWN, separate frame-quantization threshold - see out_file.h). */
#define OUT_TAKT_WRAP 17472

static int16_t rd_takt(const uint8_t* p) {
  return (int16_t)(uint16_t)(p[0] | (p[1] << 8));
}
static uint16_t rd_port(const uint8_t* p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

/* AY.pas:1092-1123, SynthesizerOUT - live-playback cadence, using the
 * same Number_Of_Tiks Q32.32 accumulator SynthesizerAY uses. */
static void synthesizer_out(out_file* f) {
  ay_engine* ay = &f->ay;
  if (!ay->int_flag) {
    int16_t zx_takt2 = (f->zx_takt == -1) ? 0 : f->zx_takt;
    int16_t number_of_takts = (int16_t)(zx_takt2 - f->previous_ay_takt);
    if (number_of_takts <= 0) {
      number_of_takts = (int16_t)(number_of_takts + OUT_TAKT_WRAP);
    } else if (f->flg > 0) {
      number_of_takts = (int16_t)(number_of_takts + OUT_TAKT_WRAP);
    }
    {
      int64_t n_of_tiks =
          ay->number_of_tiks + (int64_t)number_of_takts * ay->frq_ay_by_frq_z80;
      if ((n_of_tiks >> 32) == 0) { /* Pascal: if N_Of_Tiks.Hi = 0 */
        if (zx_takt2 == 0) f->flg++;
        return;
      }
      f->flg = 0;
      ay->number_of_tiks = n_of_tiks;
      f->previous_ay_takt = zx_takt2;
    }
  } else {
    ay->int_flag = false;
  }
  ay_synthesizer_dispatch(ay);
}

/* Players.pas:3888-3892's InitForAllTypes FT.OUT branch, replayed on
 * loop-restart (MakeBufferOUT's own `InitForAllTypes(False);
 * ResetAYChipEmulation(0, True);` calls). */
static void reset_for_loop(out_file* f) {
  f->pos = 0;
  f->previous_ay_takt = 0;
  ay_engine_reset_chip(&f->ay, true);
}

out_file_status out_file_load(out_file* f, const uint8_t* data, size_t size,
                               int ay_freq, int frq_z80, int max_tstates,
                               int sample_rate) {
  memset(f, 0, sizeof(*f));

  if (size < 5) return OUT_FILE_ERR_TRUNCATED;

  f->data = (uint8_t*)malloc(size);
  memcpy(f->data, data, size);
  f->data_size = size;
  f->pos = 0;

  f->frq_z80 = frq_z80;
  f->max_tstates = max_tstates;

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks = (uint32_t)(8192.0 / sample_rate * ay_freq + 0.5);
  f->ay.tik_re = f->ay.delay_in_tiks;
  f->ay.frq_ay_by_frq_z80 = (int64_t)(
      (double)ay_freq / frq_z80 / 8.0 * 4294967296.0 + 0.5);
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  f->previous_ay_takt = 0;
  f->zx_takt = 0;
  f->flg = 0;
  f->out_conv_total_time = 0;

  /* Players.pas:16929-16941's GetTime scan, reused to derive this port's
   * own global_tick_max (see out_file.h's own comment on why this is a
   * faithful, not invented, per-tick concept) - Convs.pas:728's OUT2PSG
   * ProgrMax formula then converts that duration into MaxTStates-frame
   * ticks. */
  {
    int64_t tm_count = 0;
    size_t p = 0;
    int16_t t = 0;
    double freq_ratio, time_ms, progr_max;

    while (p + 5 <= size) {
      t = rd_takt(data + p);
      if (t == -1 || t == 0) tm_count++;
      p += 5;
    }
    freq_ratio = (double)frq_z80 / (double)OUT_TAKT_WRAP;
    time_ms = (double)((int64_t)((double)tm_count * 1000.0 / freq_ratio + 0.5));
    if (t > 0) {
      time_ms += (double)((int64_t)(((double)t / (double)OUT_TAKT_WRAP) *
                                         1000.0 / freq_ratio +
                                     0.5));
    }
    progr_max = (time_ms / 1000.0) * ((double)frq_z80 / (double)max_tstates);
    f->global_tick_max = (int64_t)(progr_max + 0.5);
    if (f->global_tick_max < 0) f->global_tick_max = 0;
  }
  f->global_tick_counter = 0;
  f->do_loop = false;
  f->real_end_all = false;
  return OUT_FILE_OK;
}

void out_file_free(out_file* f) {
  free(f->data);
  f->data = NULL;
}

/* Players.pas:8748-8799, MakeBufferOUT. */
int out_file_make_buffer(out_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;

  ay->buf = buf;
  ay->buf_len = 0;
  ay->buffer_length = buffer_length;
  ay->sample_bits = 16;

  if (ay->int_flag) {
    synthesizer_out(f);
    if (ay->int_flag) return ay->buf_len;
    if (f->zx_takt != -1 &&
        (f->zx_port & OUT_PORT_MASK) == (0xBFFDu & OUT_PORT_MASK)) {
      ay_chip_set_ay_register(&ay->chip, ay->chip.current_register_ay,
                               f->zx_port_data);
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
    if (f->pos + 5 > f->data_size) {
      f->real_end_all = true; /* truncated trailing partial record */
      break;
    }
    f->zx_takt = rd_takt(f->data + f->pos);
    f->zx_port = rd_port(f->data + f->pos + 2);
    f->zx_port_data = f->data[f->pos + 4];
    f->pos += 5;

    if (f->zx_takt == -1 || f->zx_takt == 0) synthesizer_out(f);
    if (f->zx_takt != -1) {
      if ((f->zx_port & OUT_PORT_MASK) == (0xFFFDu & OUT_PORT_MASK)) {
        ay->chip.current_register_ay = f->zx_port_data;
      } else if ((f->zx_port & OUT_PORT_MASK) == (0xBFFDu & OUT_PORT_MASK)) {
        if (f->zx_takt != 0) synthesizer_out(f);
        if (!ay->int_flag) {
          ay_chip_set_ay_register(&ay->chip, ay->chip.current_register_ay,
                                   f->zx_port_data);
        }
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

/* Players.pas:8801-8840, OUT_Get_Registers - adapted to this port's
 * one-call-per-tick contract. global_tick_counter/global_tick_max (this
 * port's own derived duration, see out_file.h) drive real_end_all the
 * same way every other X_file_step_registers does; past the file's own
 * genuine natural end (a harmless-no-op condition matching the real
 * function's own `if UniFilePos=UniFileSize then exit;` top guard), the
 * chip's register state simply stays frozen at whatever it last was for
 * any remaining ticks up to global_tick_max - matching Convs.pas's own
 * OUT2PSG loop, which iterates exactly ProgrMax times regardless of
 * OUT_Get_Registers's own internal state (it's a procedure with no
 * return value in the original; ProgrMax and the file's own natural
 * tick-length are two independently-derived durations from the same
 * source data that coincide almost exactly but aren't algebraically
 * guaranteed identical to the tick, so a harmless off-by-one at the
 * boundary is expected here, not an error). */
bool out_file_step_registers(out_file* f) {
  ay_engine* ay = &f->ay;

  if (f->real_end_all) return false;
  if (f->global_tick_counter >= f->global_tick_max) {
    if (f->do_loop) {
      f->global_tick_counter = f->global_tick_max;
    } else {
      f->real_end_all = true;
      return false;
    }
  }

  for (;;) {
    if (f->pos + 5 > f->data_size) break; /* natural EOF - freeze */

    if (!ay->int_flag) {
      int16_t zx_takt2;
      int16_t number_of_takts;

      f->zx_takt = rd_takt(f->data + f->pos);
      f->zx_port = rd_port(f->data + f->pos + 2);
      f->zx_port_data = f->data[f->pos + 4];
      f->pos += 5;

      zx_takt2 = (f->zx_takt == -1) ? 0 : f->zx_takt;
      number_of_takts = (int16_t)(zx_takt2 - f->previous_ay_takt);
      f->previous_ay_takt = zx_takt2;
      if (number_of_takts <= 0)
        number_of_takts = (int16_t)(number_of_takts + OUT_TAKT_WRAP);
      f->out_conv_total_time += number_of_takts;
    }
    ay->int_flag = false;
    if (f->out_conv_total_time >= f->max_tstates) {
      f->out_conv_total_time -= f->max_tstates;
      ay->int_flag = true;
      break;
    }
    if (f->zx_takt != -1) {
      if ((f->zx_port & OUT_PORT_MASK) == (0xFFFDu & OUT_PORT_MASK)) {
        ay->chip.current_register_ay = f->zx_port_data;
      } else if ((f->zx_port & OUT_PORT_MASK) == (0xBFFDu & OUT_PORT_MASK)) {
        ay_chip_set_ay_register(&ay->chip, ay->chip.current_register_ay,
                                 f->zx_port_data);
      }
    }
    if (f->pos >= f->data_size) break;
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
