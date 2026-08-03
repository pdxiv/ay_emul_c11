#include "ay_engine/pt3_file.h"

#include <string.h>

/* Players.pas:1063-1071, PT3NoteTable_ST - the only note-pitch table this
 * milestone ports (both real test files use PT3_TonTableId=1). */
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

/* Players.pas:1118-1134, PT3VolumeTable_33_34 (version <= 4). */
static const uint8_t PT3_VOL_33_34[16][16] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02},
    {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03},
    {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04},
    {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05},
    {0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09},
    {0x00, 0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A},
    {0x00, 0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x09, 0x09, 0x0A, 0x0B},
    {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A, 0x0B, 0x0C},
    {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D},
    {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}};

/* Players.pas:1137-1154, PT3VolumeTable_35 (version >= 5). */
static const uint8_t PT3_VOL_35[16][16] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
    {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02},
    {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03},
    {0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04},
    {0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07},
    {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08},
    {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09},
    {0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x09, 0x0A},
    {0x00, 0x01, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B},
    {0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B, 0x0C},
    {0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}};

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

static int get_note_freq(pt3_file* f, int j) {
  (void)f;
  return PT3_NOTE_TABLE_ST[j];
}

pt3_file_status pt3_file_load(pt3_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  int i;
  uint8_t ton_table_id;

  memset(f, 0, sizeof(*f));

  if (size < 202) return PT3_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  ton_table_id = f->data[99];
  if (ton_table_id != 1) {
    /* Only PT3NoteTable_ST (TonTableId=1) is ported - see pt3_file.h. */
    return PT3_FILE_ERR_BAD_HEADER;
  }

  f->delay = f->data[100];
  f->number_of_positions = f->data[101];
  f->loop_position = f->data[102];
  f->patterns_pointer = rd16(f->data, 103);
  for (i = 0; i < 32; i++)
    f->samples_pointers[i] = rd16(f->data, 105 + (uint32_t)i * 2);
  for (i = 0; i < 16; i++)
    f->ornaments_pointers[i] = rd16(f->data, 169 + (uint32_t)i * 2);
  f->position_list_offset = 201;

  f->version = 6;
  if (f->data[13] >= '0' && f->data[13] <= '9') f->version = f->data[13] - '0';

  ay_engine_init(&f->ay);
  /* AY.pas: ChType's own declared global default is YM_Chip (AY.pas:151),
   * and PT3 never overrides it (only VTX's loader sets Chip_Type) - so
   * standalone PT3 playback uses the YM amplitude curve, matching
   * ay_engine_init's own default; no override needed here. */
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * PT3_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3717-3806, InitTrackerModule's FT.PT3 branch. */
  f->delay_counter = 1;

  i = f->data[f->position_list_offset];
  f->chan_a.address_in_pattern = rd16(f->data, f->patterns_pointer + (uint32_t)i * 2);
  f->chan_b.address_in_pattern = rd16(f->data, f->patterns_pointer + (uint32_t)i * 2 + 2);
  f->chan_c.address_in_pattern = rd16(f->data, f->patterns_pointer + (uint32_t)i * 2 + 4);

  {
    pt3_channel* a = &f->chan_a;
    a->ornament_pointer = f->ornaments_pointers[0];
    a->loop_ornament_position = f->data[a->ornament_pointer];
    a->ornament_pointer++;
    a->ornament_length = f->data[a->ornament_pointer];
    a->ornament_pointer++;
    a->sample_pointer = f->samples_pointers[1];
    a->loop_sample_position = f->data[a->sample_pointer];
    a->sample_pointer++;
    a->sample_length = f->data[a->sample_pointer];
    a->sample_pointer++;
    a->volume = 15;
    a->note_skip_counter = 1;
  }
  f->chan_b.ornament_pointer = f->chan_a.ornament_pointer;
  f->chan_b.loop_ornament_position = f->chan_a.loop_ornament_position;
  f->chan_b.ornament_length = f->chan_a.ornament_length;
  f->chan_b.sample_pointer = f->chan_a.sample_pointer;
  f->chan_b.loop_sample_position = f->chan_a.loop_sample_position;
  f->chan_b.sample_length = f->chan_a.sample_length;
  f->chan_b.volume = 15;
  f->chan_b.note_skip_counter = 1;
  f->chan_c.ornament_pointer = f->chan_a.ornament_pointer;
  f->chan_c.loop_ornament_position = f->chan_a.loop_ornament_position;
  f->chan_c.ornament_length = f->chan_a.ornament_length;
  f->chan_c.sample_pointer = f->chan_a.sample_pointer;
  f->chan_c.loop_sample_position = f->chan_a.loop_sample_position;
  f->chan_c.sample_length = f->chan_a.sample_length;
  f->chan_c.volume = 15;
  f->chan_c.note_skip_counter = 1;

  f->global_tick_counter = 0;

  return PT3_FILE_OK;
}

/* Players.pas:12324-12583, PatternInterpreter. */
static void pattern_interpreter(pt3_file* f, pt3_channel* chan) {
  bool quit = false;
  uint8_t flag9 = 0, flag8 = 0, flag5 = 0, flag4 = 0, flag3 = 0, flag2 = 0,
          flag1 = 0;
  uint8_t counter = 0;
  int pr_note = chan->note;
  int pr_sliding = chan->current_ton_sliding;
  uint8_t* d = f->data;

  do {
    uint8_t op = d[chan->address_in_pattern];
    if (op >= 0xF0) {
      chan->ornament_pointer = f->ornaments_pointers[op - 0xF0];
      chan->loop_ornament_position = d[chan->ornament_pointer];
      chan->ornament_pointer++;
      chan->ornament_length = d[chan->ornament_pointer];
      chan->ornament_pointer++;
      chan->address_in_pattern++;
      chan->sample_pointer = f->samples_pointers[d[chan->address_in_pattern] / 2];
      chan->loop_sample_position = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->sample_length = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->envelope_enabled = false;
      chan->position_in_ornament = 0;
    } else if (op >= 0xD1) { /* 0xD1..0xEF */
      chan->sample_pointer = f->samples_pointers[op - 0xD0];
      chan->loop_sample_position = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->sample_length = d[chan->sample_pointer];
      chan->sample_pointer++;
    } else if (op == 0xD0) {
      quit = true;
    } else if (op >= 0xC1) { /* 0xC1..0xCF */
      chan->volume = (uint8_t)(op - 0xC0);
    } else if (op == 0xC0) {
      chan->position_in_sample = 0;
      chan->current_amplitude_sliding = 0;
      chan->current_noise_sliding = 0;
      chan->current_envelope_sliding = 0;
      chan->position_in_ornament = 0;
      chan->ton_slide_count = 0;
      chan->current_ton_sliding = 0;
      chan->ton_accumulator = 0;
      chan->current_onoff = 0;
      chan->enabled = false;
      quit = true;
    } else if (op >= 0xB2) { /* 0xB2..0xBF */
      chan->envelope_enabled = true;
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, (uint8_t)(op - 0xB1));
      chan->address_in_pattern++;
      f->env_base = (int16_t)(((uint16_t)d[chan->address_in_pattern]) << 8);
      chan->address_in_pattern++;
      f->env_base = (int16_t)((uint16_t)f->env_base | d[chan->address_in_pattern]);
      chan->position_in_ornament = 0;
      f->cur_env_slide = 0;
      f->cur_env_delay = 0;
    } else if (op == 0xB1) {
      chan->address_in_pattern++;
      chan->number_of_notes_to_skip = d[chan->address_in_pattern];
    } else if (op == 0xB0) {
      chan->envelope_enabled = false;
      chan->position_in_ornament = 0;
    } else if (op >= 0x50) { /* 0x50..0xAF */
      chan->note = (uint8_t)(op - 0x50);
      chan->position_in_sample = 0;
      chan->current_amplitude_sliding = 0;
      chan->current_noise_sliding = 0;
      chan->current_envelope_sliding = 0;
      chan->position_in_ornament = 0;
      chan->ton_slide_count = 0;
      chan->current_ton_sliding = 0;
      chan->ton_accumulator = 0;
      chan->current_onoff = 0;
      chan->enabled = true;
      quit = true;
    } else if (op >= 0x40) { /* 0x40..0x4F */
      chan->ornament_pointer = f->ornaments_pointers[op - 0x40];
      chan->loop_ornament_position = d[chan->ornament_pointer];
      chan->ornament_pointer++;
      chan->ornament_length = d[chan->ornament_pointer];
      chan->ornament_pointer++;
      chan->position_in_ornament = 0;
    } else if (op >= 0x20) { /* 0x20..0x3F */
      f->noise_base = (uint8_t)(op - 0x20);
    } else if (op >= 0x10) { /* 0x10..0x1F */
      if (op == 0x10) {
        chan->envelope_enabled = false;
      } else {
        ay_chip_set_ay_register_fast(&f->ay.chip, 13, (uint8_t)(op - 0x10));
        chan->address_in_pattern++;
        f->env_base = (int16_t)(((uint16_t)d[chan->address_in_pattern]) << 8);
        chan->address_in_pattern++;
        f->env_base = (int16_t)((uint16_t)f->env_base | d[chan->address_in_pattern]);
        chan->envelope_enabled = true;
        f->cur_env_slide = 0;
        f->cur_env_delay = 0;
      }
      chan->address_in_pattern++;
      chan->sample_pointer = f->samples_pointers[d[chan->address_in_pattern] / 2];
      chan->loop_sample_position = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->sample_length = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->position_in_ornament = 0;
    } else if (op == 9) {
      counter++;
      flag9 = counter;
    } else if (op == 8) {
      counter++;
      flag8 = counter;
    } else if (op == 5) {
      counter++;
      flag5 = counter;
    } else if (op == 4) {
      counter++;
      flag4 = counter;
    } else if (op == 3) {
      counter++;
      flag3 = counter;
    } else if (op == 2) {
      counter++;
      flag2 = counter;
    } else if (op == 1) {
      counter++;
      flag1 = counter;
    }
    chan->address_in_pattern++;
  } while (!quit);

  while (counter > 0) {
    if (counter == flag1) {
      chan->ton_slide_delay = d[chan->address_in_pattern];
      chan->ton_slide_count = chan->ton_slide_delay;
      chan->address_in_pattern++;
      chan->ton_slide_step = (int16_t)rd16(d, chan->address_in_pattern);
      chan->address_in_pattern += 2;
      chan->simple_gliss = true;
      chan->current_onoff = 0;
      if (chan->ton_slide_count == 0 && f->version >= 7) chan->ton_slide_count++;
    } else if (counter == flag2) {
      chan->simple_gliss = false;
      chan->current_onoff = 0;
      chan->ton_slide_delay = d[chan->address_in_pattern];
      chan->ton_slide_count = chan->ton_slide_delay;
      chan->address_in_pattern += 3;
      {
        int16_t raw = (int16_t)rd16(d, chan->address_in_pattern);
        chan->ton_slide_step = (int16_t)(raw < 0 ? -raw : raw);
      }
      chan->address_in_pattern += 2;
      chan->ton_delta = (int16_t)(get_note_freq(f, chan->note) - get_note_freq(f, pr_note));
      chan->slide_to_note = chan->note;
      chan->note = (uint8_t)pr_note;
      if (f->version >= 6) chan->current_ton_sliding = (int16_t)pr_sliding;
      if (chan->ton_delta - chan->current_ton_sliding < 0)
        chan->ton_slide_step = (int16_t)-chan->ton_slide_step;
    } else if (counter == flag3) {
      chan->position_in_sample = d[chan->address_in_pattern];
      chan->address_in_pattern++;
    } else if (counter == flag4) {
      chan->position_in_ornament = d[chan->address_in_pattern];
      chan->address_in_pattern++;
    } else if (counter == flag5) {
      chan->onoff_delay = d[chan->address_in_pattern];
      chan->address_in_pattern++;
      chan->offon_delay = d[chan->address_in_pattern];
      chan->current_onoff = chan->onoff_delay;
      chan->address_in_pattern++;
      chan->ton_slide_count = 0;
      chan->current_ton_sliding = 0;
    } else if (counter == flag8) {
      f->env_delay = (int8_t)d[chan->address_in_pattern];
      f->cur_env_delay = f->env_delay;
      chan->address_in_pattern++;
      f->env_slide_add = (int16_t)rd16(d, chan->address_in_pattern);
      chan->address_in_pattern += 2;
    } else if (counter == flag9) {
      uint8_t b = d[chan->address_in_pattern];
      f->delay = b; /* TSMode paired-delay sync not ported - MIG-0007 */
      chan->address_in_pattern++;
    }
    counter--;
  }
  chan->note_skip_counter = (int8_t)chan->number_of_notes_to_skip;
}

/* Players.pas:12589-12682, ChangeRegisters. */
static void change_registers(pt3_file* f, pt3_channel* chan, uint8_t* temp_mixer,
                              int8_t* add_to_env) {
  uint8_t* d = f->data;

  if (chan->enabled) {
    uint8_t b0 = d[chan->sample_pointer + (uint32_t)chan->position_in_sample * 4];
    uint8_t b1 = d[chan->sample_pointer + (uint32_t)chan->position_in_sample * 4 + 1];
    int j;

    chan->ton = rd16(d, chan->sample_pointer + (uint32_t)chan->position_in_sample * 4 + 2);
    chan->ton = (uint16_t)(chan->ton + chan->ton_accumulator);
    if (b1 & 0x40) chan->ton_accumulator = (int16_t)chan->ton;

    j = chan->note + d[chan->ornament_pointer + chan->position_in_ornament];
    j &= 0xFF;
    if ((int8_t)j < 0) j = 0;
    else if (j > 95) j = 95;

    {
      uint16_t w = (uint16_t)get_note_freq(f, j);
      chan->ton = (uint16_t)((chan->ton + chan->current_ton_sliding + w) & 0xFFF);
    }

    if (chan->ton_slide_count > 0) {
      chan->ton_slide_count--;
      if (chan->ton_slide_count == 0) {
        chan->current_ton_sliding = (int16_t)(chan->current_ton_sliding + chan->ton_slide_step);
        chan->ton_slide_count = chan->ton_slide_delay;
        if (!chan->simple_gliss) {
          if ((chan->ton_slide_step < 0 && chan->current_ton_sliding <= chan->ton_delta) ||
              (chan->ton_slide_step >= 0 && chan->current_ton_sliding >= chan->ton_delta)) {
            chan->note = chan->slide_to_note;
            chan->ton_slide_count = 0;
            chan->current_ton_sliding = 0;
          }
        }
      }
    }

    {
      int amplitude = b1 & 0xF;
      if (b0 & 0x80) {
        if (b0 & 0x40) {
          if (chan->current_amplitude_sliding < 15) chan->current_amplitude_sliding++;
        } else if (chan->current_amplitude_sliding > -15) {
          chan->current_amplitude_sliding--;
        }
      }
      amplitude += chan->current_amplitude_sliding;
      amplitude &= 0xFF;
      if ((int8_t)amplitude < 0) amplitude = 0;
      else if (amplitude > 15) amplitude = 15;

      if (f->version <= 4)
        amplitude = PT3_VOL_33_34[chan->volume][amplitude];
      else
        amplitude = PT3_VOL_35[chan->volume][amplitude];

      if ((b0 & 1) == 0 && chan->envelope_enabled) amplitude |= 16;
      chan->amplitude = (uint8_t)amplitude;
    }

    if (b1 & 0x80) {
      int j2;
      if (b0 & 0x20)
        j2 = (int)(((b0 >> 1) | 0xF0) + chan->current_envelope_sliding);
      else
        j2 = (int)((((b0 >> 1) & 0xF)) + chan->current_envelope_sliding);
      j2 &= 0xFF;
      if (b1 & 0x20) chan->current_envelope_sliding = (uint8_t)j2;
      *add_to_env = (int8_t)(*add_to_env + (int8_t)j2);
    } else {
      f->add_to_noise = (uint8_t)((b0 >> 1) + chan->current_noise_sliding);
      if (b1 & 0x20) chan->current_noise_sliding = f->add_to_noise;
    }

    *temp_mixer = (uint8_t)(((b1 >> 1) & 0x48) | *temp_mixer);

    chan->position_in_sample++;
    if (chan->position_in_sample >= chan->sample_length)
      chan->position_in_sample = chan->loop_sample_position;
    chan->position_in_ornament++;
    if (chan->position_in_ornament >= chan->ornament_length)
      chan->position_in_ornament = chan->loop_ornament_position;
  } else {
    chan->amplitude = 0;
  }

  *temp_mixer = (uint8_t)(*temp_mixer >> 1);
  if (chan->current_onoff > 0) {
    chan->current_onoff--;
    if (chan->current_onoff == 0) {
      chan->enabled = !chan->enabled;
      chan->current_onoff = chan->enabled ? chan->onoff_delay : chan->offon_delay;
    }
  }
}

/* Players.pas:12684-12766, PT3_Get_Registers's main body (CNum=0 only -
 * no Turbosound, see pt3_file.h). */
static void pt3_get_registers(pt3_file* f) {
  uint8_t temp_mixer = 0;
  int8_t add_to_env = 0;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter == 0) {
      if (f->data[f->chan_a.address_in_pattern] == 0) {
        f->current_position++;
        if (f->current_position == f->number_of_positions)
          f->current_position = f->loop_position;
        {
          int i = f->data[f->position_list_offset + f->current_position];
          f->chan_a.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 2);
          f->chan_b.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 2 + 2);
          f->chan_c.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 2 + 4);
          f->noise_base = 0;
        }
      }
      pattern_interpreter(f, &f->chan_a);
    }
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter == 0) pattern_interpreter(f, &f->chan_b);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter == 0) pattern_interpreter(f, &f->chan_c);
    f->delay_counter = f->delay;
  }

  add_to_env = 0;
  temp_mixer = 0;
  change_registers(f, &f->chan_a, &temp_mixer, &add_to_env);
  change_registers(f, &f->chan_b, &temp_mixer, &add_to_env);
  change_registers(f, &f->chan_c, &temp_mixer, &add_to_env);

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

  f->ay.chip.reg[6] = (uint8_t)((f->noise_base + f->add_to_noise) & 31);

  {
    int16_t env = (int16_t)(f->env_base + add_to_env + f->cur_env_slide);
    f->ay.chip.reg[11] = (uint8_t)(env & 0xFF);
    f->ay.chip.reg[12] = (uint8_t)((env >> 8) & 0xFF);
  }

  if (f->cur_env_delay > 0) {
    f->cur_env_delay--;
    if (f->cur_env_delay == 0) {
      f->cur_env_delay = f->env_delay;
      f->cur_env_slide = (int16_t)(f->cur_env_slide + f->env_slide_add);
    }
  }

  f->global_tick_counter++;
}

int pt3_file_make_buffer(pt3_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  /* AY.pas:2070's AY_Tiks_In_Interrupt: trunc(AY_Freq/(Interrupt_Freq/1000*8)
   * + 0.5) - a fixed per-frame integer tick budget, computed once (not a
   * running accumulation like SynthesizerAY/SynthesizerYM6). */
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(PT3_FILE_AY_FREQ_DEF /
                    (PT3_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
                0.5);

  ay->buf = buf;
  ay->buf_len = 0;
  ay->buffer_length = buffer_length;
  ay->number_of_channels = 2;
  ay->sample_bits = 16;

  /* Players.pas:12283-12300 MakeBufferTracker's own buffer-boundary
   * cutoff handling, mirroring MakeBufferAY/MakeBufferYM5. */
  if (ay->int_flag) {
    ay->int_flag = false;
    ay_synthesizer_stereo16(ay);
  }
  if (ay->int_flag) return ay->buf_len;

  while (ay->buf_len < buffer_length) {
    pt3_get_registers(f);
    /* AY.pas:1075-1082 SynthesizerZX50. */
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
