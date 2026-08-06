#include "ay_engine/gtr_file.h"

#include <string.h>

/* Players.pas:1063-1071, PT3NoteTable_ST - same table PT3/PT1 use
 * (GTR_Get_Registers references it directly, Players.pas:11609) -
 * duplicated here rather than shared, matching this project's existing
 * per-file convention. */
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

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* Copies a fixed-width, space-padded (not NUL-terminated) field, then
 * trims leading/trailing bytes <= ' ' - Players.pas reads these title/author
 * fields as raw fixed-length strings (SetLength(Title,N); UniRead(...,N))
 * without an explicit Trim in every branch, but every place that
 * actually displays/compares the result does trim it (e.g. ASC/PSM's
 * own `Trim(...) <> ''`), so trimming here for consistency. */
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

/* ModTypes variant 12 (Players.pas:179-190): GTR_Delay@0 GTR_ID[0..3]@1
 * GTR_Address@5 (word) GTR_Name[0..31]@7 GTR_SamplesPointers[0..14]@39
 * (30B) GTR_OrnamentsPointers[0..15]@69 (32B)
 * GTR_PatternsPointers[0..31]@101 (192B, 3 words each)
 * GTR_NumberOfPositions@293 GTR_LoopPosition@294 GTR_Positions@295. */
gtr_file_status gtr_file_load(gtr_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;
  int i;

  memset(f, 0, sizeof(*f));

  if (size < 295) return GTR_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  /* Players.pas: "else if FType = FT.GTR" (7345-7353) - GTR_Name[0..31]@7,
   * read from the raw file bytes before any pointer relocation touches
   * f->data (this region is never written to by that relocation). */
  copy_fixed_field(f->data, size, 7, 32, f->title, sizeof(f->title));

  f->delay = f->data[0];
  memcpy(f->id, &f->data[1], 4);
  uint16_t address = rd16(f->data, 5);
  for (i = 0; i < 15; i++)
    f->samples_pointers[i] = rd16(f->data, 39 + (uint32_t)i * 2);
  for (i = 0; i < 16; i++)
    f->ornaments_pointers[i] = rd16(f->data, 69 + (uint32_t)i * 2);
  for (i = 0; i < 32; i++) {
    f->patterns_pointers[i].a = rd16(f->data, 101 + (uint32_t)i * 6);
    f->patterns_pointers[i].b = rd16(f->data, 101 + (uint32_t)i * 6 + 2);
    f->patterns_pointers[i].c = rd16(f->data, 101 + (uint32_t)i * 6 + 4);
  }
  f->number_of_positions = f->data[293];
  f->loop_position = f->data[294];
  f->positions_offset = 295;

  /* Players.pas:2534-2546: the 127 on-disk pointer words (15 samples + 16
   * ornaments + 32*3 patterns) are relative to wherever the file was
   * originally loaded (GTR_Address) - subtract it so they become plain
   * offsets into Index[0..], then zero it (matches the real loader; no
   * RaiseBadFileStructure equivalent here since we trust the corpus). */
  if (address != 0) {
    for (i = 0; i < 15; i++)
      f->samples_pointers[i] = (uint16_t)(f->samples_pointers[i] - address);
    for (i = 0; i < 16; i++)
      f->ornaments_pointers[i] =
          (uint16_t)(f->ornaments_pointers[i] - address);
    for (i = 0; i < 32; i++) {
      f->patterns_pointers[i].a =
          (uint16_t)(f->patterns_pointers[i].a - address);
      f->patterns_pointers[i].b =
          (uint16_t)(f->patterns_pointers[i].b - address);
      f->patterns_pointers[i].c =
          (uint16_t)(f->patterns_pointers[i].c - address);
    }
  }

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * GTR_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3090-3157, InitTrackerModule's FT.GTR branch. */
  f->delay_counter = 1;
  {
    int pos = f->data[f->positions_offset];
    int grp = pos / 6;
    f->chan_a.address_in_pattern = f->patterns_pointers[grp].a;
    f->chan_b.address_in_pattern = f->patterns_pointers[grp].b;
    f->chan_c.address_in_pattern = f->patterns_pointers[grp].c;
  }
  gtr_channel* chans[3] = {&f->chan_a, &f->chan_b, &f->chan_c};
  for (i = 0; i < 3; i++) {
    chans[i]->sample_pointer = 65536 - 4;
    chans[i]->sample_length = 4;
    chans[i]->ornament_pointer = 65536 - 4;
    chans[i]->ornament_length = 1;
    chans[i]->enabled = true;
  }

  f->global_tick_counter = 0;

  return GTR_FILE_OK;
}

/* Players.pas:11527-11593, PatternInterpreter. */
static void pattern_interpreter(gtr_file* f, gtr_channel* chan) {
  bool quit = false;
  chan->note_skip_counter = 0;

  do {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op <= 0x5F) {
      chan->note = op;
      chan->position_in_sample = 0;
      chan->position_in_ornament = 0;
      chan->enabled = true;
      quit = true;
    } else if (op <= 0x6F) {
      chan->sample_pointer = f->samples_pointers[op - 0x60];
      chan->loop_sample_position = rb(f->data, chan->sample_pointer);
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
      chan->sample_length = rb(f->data, chan->sample_pointer);
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
    } else if (op <= 0x7F) {
      chan->ornament_pointer = f->ornaments_pointers[op - 0x70];
      chan->loop_ornament_position = rb(f->data, chan->ornament_pointer);
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->ornament_length = rb(f->data, chan->ornament_pointer);
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->position_in_ornament = 0;
      if (f->id[3] != 0x10) chan->envelope_enabled = false;
    } else if (op <= 0xBF) {
      chan->note_skip_counter = (int8_t)(op - 0x80);
    } else if (op <= 0xCF) {
      /* Players.pas:11568-11571: only a single envelope byte (reg 11) is
       * written - unlike PT1's word-sized envelope read - GTR never
       * touches envelope hi (reg 12) at all. */
      ay_chip_set_ay_register_fast(&f->ay.chip, 13, (uint8_t)(op - 0xC0));
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->ay.chip.reg[11] = rb(f->data, chan->address_in_pattern);
      chan->envelope_enabled = true;
    } else if (op <= 0xDF) {
      quit = true;
    } else if (op == 0xE0) {
      chan->enabled = false;
      if (f->id[3] != 0x10) quit = true;
    } else {
      chan->volume = (uint8_t)(15 - (op - 0xE0));
    }
    chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
  } while (!quit);
}

/* Players.pas:11595-11634, GetRegisters. */
static void get_registers(gtr_file* f, gtr_channel* chan, uint8_t* temp_mixer) {
  if (chan->enabled) {
    uint8_t j = (uint8_t)(chan->note +
                           rb(f->data, chan->ornament_pointer +
                                           chan->position_in_ornament));
    if (j > 0x5F) j = 0x5F;
    chan->position_in_ornament++;
    if (chan->position_in_ornament == chan->ornament_length)
      chan->position_in_ornament = chan->loop_ornament_position;

    int ton = (PT3_NOTE_TABLE_ST[j] +
               rd16(f->data, (uint32_t)chan->sample_pointer +
                                 chan->position_in_sample + 2)) &
              0xFFF;
    chan->ton = (uint16_t)ton;

    uint8_t b = rb(f->data, (uint32_t)chan->sample_pointer +
                                 chan->position_in_sample + 1);
    f->ay.chip.reg[6] = (uint8_t)((f->ay.chip.reg[6] | b) & 0x1F);

    uint8_t amp = (uint8_t)(rb(f->data, (uint32_t)chan->sample_pointer +
                                             chan->position_in_sample) -
                             chan->volume);
    if ((int8_t)amp < 0) amp = 0;
    amp = (uint8_t)(amp & 0xF);
    if ((int8_t)b < 0 && chan->envelope_enabled) amp = (uint8_t)(amp | 16);
    chan->amplitude = amp;

    if (b & 64) *temp_mixer |= 64;
    if (b & 32) *temp_mixer |= 8;

    chan->position_in_sample = (uint8_t)(chan->position_in_sample + 4);
    if (chan->position_in_sample == chan->sample_length)
      chan->position_in_sample = chan->loop_sample_position;
  } else {
    chan->amplitude = 0;
    *temp_mixer |= (8 | 64);
  }
  *temp_mixer >>= 1;
}

/* Players.pas:11651-11662: shared position/pattern advance driven off
 * channel A's own Address_In_Pattern hitting the end-of-pattern marker
 * (255), setting all 3 channels' Address_In_Pattern at once. */
static void gtr_pattern_advance(gtr_file* f) {
  while (rb(f->data, f->chan_a.address_in_pattern) == 255) {
    f->current_position++;
    if (f->current_position == f->number_of_positions)
      f->current_position = f->loop_position;
    int pos = rb(f->data, f->positions_offset + f->current_position);
    int grp = pos / 6;
    f->chan_a.address_in_pattern = f->patterns_pointers[grp].a;
    f->chan_b.address_in_pattern = f->patterns_pointers[grp].b;
    f->chan_c.address_in_pattern = f->patterns_pointers[grp].c;
  }
}

/* Players.pas:11523-11698, GTR_Get_Registers (minus CheckLoopAndStop -
 * matches pt3_get_registers/pt1_get_registers's own precedent). */
static void gtr_get_registers(gtr_file* f) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->delay_counter = f->delay;

    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter < 0) {
      gtr_pattern_advance(f);
      pattern_interpreter(f, &f->chan_a);
    }
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter < 0) pattern_interpreter(f, &f->chan_b);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter < 0) pattern_interpreter(f, &f->chan_c);
  }

  temp_mixer = 0;
  f->ay.chip.reg[6] = 0;
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

int gtr_file_make_buffer(gtr_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(GTR_FILE_AY_FREQ_DEF /
                    (GTR_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    gtr_get_registers(f);
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_stereo16(ay);
  }
  return ay->buf_len;
}
