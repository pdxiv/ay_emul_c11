#include "ay_engine/formats/ftc_file.h"

#include <string.h>

/* Players.pas:1063-1071, PT3NoteTable_ST - same table PT1/GTR/PT2 use
 * (duplicated here per this project's per-file convention). */
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

/* Players.pas:973-984, ST_Table - same table FLS/STC/STP use. */
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

/* Players.pas:1025-1033, FTCNoteTable2. */
static const uint16_t FTC_NOTE_TABLE2[96] = {
    0x0D10, 0x0C58, 0x0BA0, 0x0B00, 0x0A60, 0x09C8, 0x0940, 0x08B8, 0x0840,
    0x07C0, 0x0750, 0x06F0, 0x0688, 0x062C, 0x05D0, 0x0580, 0x0530, 0x04E4,
    0x04A0, 0x045C, 0x0420, 0x03E0, 0x03A8, 0x0378, 0x0344, 0x0316, 0x02E8,
    0x02C0, 0x0298, 0x0272, 0x0250, 0x022E, 0x0210, 0x01F0, 0x01D4, 0x01BC,
    0x01A2, 0x018B, 0x0174, 0x0160, 0x014C, 0x0139, 0x0128, 0x0117, 0x0108,
    0x00F8, 0x00EA, 0x00DE, 0x00D1, 0x00C5, 0x00BA, 0x00B0, 0x00A6, 0x009C,
    0x0094, 0x008B, 0x0084, 0x007C, 0x0075, 0x006F, 0x0068, 0x0062, 0x005D,
    0x0058, 0x0053, 0x004E, 0x004A, 0x0045, 0x0042, 0x003E, 0x003A, 0x0037,
    0x0034, 0x0031, 0x002E, 0x002C, 0x0029, 0x0027, 0x0025, 0x0022, 0x0021,
    0x001F, 0x001D, 0x001B, 0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013,
    0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D};

/* Players.pas:11058: `Amplitude := round((Volume*17+...) * j / 256)` -
 * same round-half-to-even division PT1/PT2 use (duplicated per this
 * project's per-file convention). */
static int round_div256(int numerator) {
  int quotient = numerator / 256;
  int remainder = numerator % 256;
  if (remainder < 128) return quotient;
  if (remainder > 128) return quotient + 1;
  return (quotient & 1) ? quotient + 1 : quotient; /* tie: round to even */
}

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* Copies a fixed-width, space-padded (not NUL-terminated) field, then
 * trims leading/trailing bytes <= ' ' - see gtr_file.c's identical helper for
 * the full rationale (duplicated per this project's per-file
 * convention). */
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

/* Players.pas:10847-10858, GetNoteFreq. */
static int get_note_freq(const ftc_file* f, uint8_t j) {
  if (f->version < 7) return PT3_NOTE_TABLE_ST[j];
  if (f->data[0x32] == 2) return FTC_NOTE_TABLE2[j];
  return ST_TABLE[j];
}

/* Players.pas:15769-15887, GetTimeFTC's shared per-channel opcode-walk
 * (used for all of channels A/B/C - all three share the exact same
 * opcode ranges: $30/$60..$CB and $40..$5F are the two terminal cases
 * (the latter also sets the new note-skip count), $EE/$EF are 1-byte-
 * payload no-ops, $31..$3E/$ED are 2-byte-payload no-ops, $F0..$FF reads
 * a new shared delay byte `b`, anything else is a bare 1-byte no-op).
 * `j` only ever increases, so the 65536-bound check below already caps
 * this at <=65536 iterations without needing a separate guard counter
 * (unlike PSC's ported get-time function, which has no such natural
 * bound and needs one) - matches the task's own framing that FTC's
 * outer note-walk already has Pascal's own DLCatcher bound, so no
 * additional cap is needed here. Returns false on any bounds violation
 * (caller must bail: Tm=Lp=0). */
static bool ftc_time_channel_walk(const uint8_t* d, uint32_t* j, int* a,
                                   uint8_t* b) {
  for (;;) {
    if (*j >= 65536) return false;
    uint8_t op = d[*j];
    if (op == 0x30 || (op >= 0x60 && op <= 0xCB)) {
      *a = 0;
      (*j)++;
      return true;
    } else if (op >= 0x40 && op <= 0x5F) {
      *a = op - 0x40;
      (*j)++;
      return true;
    } else if (op == 0xEE || op == 0xEF) {
      (*j)++;
    } else if ((op >= 0x31 && op <= 0x3E) || op == 0xED) {
      *j += 2;
    } else if (op >= 0xF0) {
      (*j)++;
      if (*j >= 65536) return false;
      *b = d[*j];
    }
    (*j)++;
  }
}

typedef enum {
  FTC_TIME_STEP_CONTINUE,
  FTC_TIME_STEP_END_OF_POSITION,
  FTC_TIME_STEP_ERROR,
} ftc_time_step_result;

/* Channel A's own step: `Dec(a1); if a1<0 then begin if Index[j1]=255
 * then break; <walk>; end;` - the `Index[j1]=255` end-of-position check
 * only fires exactly when a1 counts down to trigger a new note lookup
 * (matching pt3_get_time's own PatInt/check_end precedent: only channel
 * A detects "no more pattern data" and signals the caller to stop the
 * whole per-position tick loop). */
static ftc_time_step_result ftc_time_step_a(const uint8_t* d, uint32_t* j,
                                             int* a, uint8_t* b) {
  (*a)--;
  if (*a >= 0) return FTC_TIME_STEP_CONTINUE;
  if (*j >= 65536) return FTC_TIME_STEP_ERROR;
  if (d[*j] == 255) return FTC_TIME_STEP_END_OF_POSITION;
  if (!ftc_time_channel_walk(d, j, a, b)) return FTC_TIME_STEP_ERROR;
  return FTC_TIME_STEP_CONTINUE;
}

/* Channels B/C's own step: `Dec(a2); if a2<0 then <walk>;` - no
 * end-of-position check (see ftc_time_step_a). */
static ftc_time_step_result ftc_time_step_bc(const uint8_t* d, uint32_t* j,
                                              int* a, uint8_t* b) {
  (*a)--;
  if (*a >= 0) return FTC_TIME_STEP_CONTINUE;
  if (!ftc_time_channel_walk(d, j, a, b)) return FTC_TIME_STEP_ERROR;
  return FTC_TIME_STEP_CONTINUE;
}

/* Players.pas:15769-15887, GetTimeFTC - computes the song's total
 * duration and loop-point tick, walking the position list (at
 * f->positions_offset, the same table FTC_Get_Registers's own position
 * advance consumes at playback time) exactly once. The DLCatcher(256)-
 * bounded inner loop matches Players.pas's own per-position note-walk
 * safety net exactly (unlike PSC's ported get-time function, this one
 * already had an explicit bound in the original, so nothing extra is
 * added here beyond the bounds checks MIG-0103's safety note requires
 * everywhere else). On a malformed file, returns 0/0 rather than
 * raising, matching pt3_get_time's/psc_get_time's own precedent. */
static void ftc_get_time(const ftc_file* f, int64_t* out_tm,
                          int64_t* out_lp) {
  const uint8_t* d = f->data;
  int64_t tm = 0, lp = 0;
  uint8_t b = f->delay;
  int i = 0;

  for (;;) {
    uint32_t pos_addr = f->positions_offset + (uint32_t)i * 2;
    uint8_t pattern;
    uint32_t base;
    uint32_t j1, j2, j3;
    int a1 = 0, a2 = 0, a3 = 0;
    int dl_catcher = 256;

    if (pos_addr >= 65536) goto fail;
    pattern = d[pos_addr];
    if (pattern == 255) break;
    if (i == f->data[70] /* FTC_Loop_Position */) lp = tm;

    base = f->patterns_pointer + (uint32_t)pattern * 6;
    if (base + 5 >= 65536) goto fail;
    j1 = rd16(d, base);
    j2 = rd16(d, base + 2);
    j3 = rd16(d, base + 4);

    i++;
    if (i >= (65536 - 0xD4) / 2) goto fail;

    for (;;) {
      ftc_time_step_result ra = ftc_time_step_a(d, &j1, &a1, &b);
      if (ra == FTC_TIME_STEP_ERROR) goto fail;
      if (ra == FTC_TIME_STEP_END_OF_POSITION) break;
      if (ftc_time_step_bc(d, &j2, &a2, &b) == FTC_TIME_STEP_ERROR) goto fail;
      if (ftc_time_step_bc(d, &j3, &a3, &b) == FTC_TIME_STEP_ERROR) goto fail;
      tm += b;
      if (--dl_catcher < 0) goto fail;
    }
  }
  *out_tm = tm;
  *out_lp = lp;
  return;

fail:
  *out_tm = 0;
  *out_lp = 0;
}

/* ModTypes variant 8 (Players.pas:151-162): FTC_MusicName[0..68]@0
 * FTC_Delay@69 FTC_Loop_Position@70 FTC_Slack(4B)@71
 * FTC_PatternsPointer@75 (word) FTC_Slack2[5]@77
 * FTC_SamplesPointers[0..31]@82 (64B) FTC_OrnamentsPointers[0..32]@146
 * (66B) FTC_Positions@212 (2B/entry: Pattern byte, Transposition
 * shortint). */
ftc_file_status ftc_file_load(ftc_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;
  int i;

  memset(f, 0, sizeof(*f));

  if (size < 214) return FTC_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  /* Players.pas: "else if FType = FT.FTC" (7372-7380). */
  copy_fixed_field(f->data, size, 8, 42, f->title, sizeof(f->title));

  f->delay = f->data[69];
  f->patterns_pointer = rd16(f->data, 75);
  for (i = 0; i < 32; i++)
    f->samples_pointers[i] = rd16(f->data, 82 + (uint32_t)i * 2);
  for (i = 0; i < 33; i++)
    f->ornaments_pointers[i] = rd16(f->data, 146 + (uint32_t)i * 2);
  f->positions_offset = 212;

  /* Players.pas:2611-2617: Version derivation (folded in at load time,
   * since it never changes per tick). */
  f->version = 0;
  if (f->data[67] == '0' && f->data[68] >= '0' && f->data[68] <= '9')
    f->version = f->data[68] - '0';

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * FTC_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3368-3420ish, InitTrackerModule's FT.FTC branch. */
  f->delay_counter = 1;
  f->transposition = (uint8_t)rb(f->data, f->positions_offset + 1);
  {
    uint8_t pat = rb(f->data, f->positions_offset);
    uint32_t base = f->patterns_pointer + (uint32_t)pat * 6;
    f->chan_a.address_in_pattern = rd16(f->data, base);
    f->chan_b.address_in_pattern = rd16(f->data, base + 2);
    f->chan_c.address_in_pattern = rd16(f->data, base + 4);
  }

  f->chan_a.ornament_pointer = f->ornaments_pointers[0];
  f->chan_a.sample_pointer = 0x52;
  f->chan_a.ornament_length = 1;
  f->chan_a.volume = 15;
  f->chan_b.ornament_pointer = f->ornaments_pointers[0];
  f->chan_b.sample_pointer = 0x52;
  f->chan_b.ornament_length = 1;
  f->chan_b.volume = 15;
  f->chan_c.ornament_pointer = f->ornaments_pointers[0];
  f->chan_c.sample_pointer = 0x52;
  f->chan_c.ornament_length = 1;
  f->chan_c.volume = 15;

  f->global_tick_counter = 0;
  f->do_loop = false;
  f->real_end_all = false;
  ftc_get_time(f, &f->global_tick_max, &f->loop_tick); /* MIG-0103 */

  return FTC_FILE_OK;
}

/* Players.pas:10863-11004, PatternInterpreter. No `break`/early-exit in
 * the real source (unlike ASC/GTR/STC) - the repeat loop's condition is
 * literally `until quit`, so the common trailing address increment
 * always fires regardless, and every branch's own inner increments (if
 * any) are needed in full - a genuinely different (and simpler, no
 * double-counting risk) control-flow shape than those formats. */
static void pattern_interpreter(ftc_file* f, ftc_channel* chan, int chan_num) {
  bool quit = false;
  int8_t exxaf = 2;

  do {
    uint8_t op = rb(f->data, chan->address_in_pattern);
    if (op <= 0x1F) {
      chan->sample_pointer = f->samples_pointers[op];
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
      chan->loop_sample_position = rb(f->data, chan->sample_pointer);
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
      chan->sample_length = (uint8_t)(rb(f->data, chan->sample_pointer) + 1);
      chan->sample_pointer = (uint16_t)(chan->sample_pointer + 1);
    } else if (op <= 0x2F) {
      chan->volume = (uint8_t)(op - 0x20);
    } else if (op == 0x30) {
      chan->sample_enabled = false;
      chan->position_in_sample = 0;
      chan->sample_noise_accumulator = 0;
      chan->volume_slide = 0;
      chan->noise_accumulator = 0;
      chan->note_accumulator = 0;
      chan->position_in_ornament = 0;
      chan->ton_accumulator = 0;
      chan->envelope_accumulator = 0;
      if (exxaf > 0) {
        chan->current_ton_sliding = 0;
        chan->ton_slide_direction = 0;
      }
      if (exxaf > 1) chan->ton_slide_step = 0;
      chan->note_skip_counter = 0;
      quit = true;
    } else if (op <= 0x3E) {
      f->env_t = (uint8_t)(op - 0x30);
      chan->envelope_enabled = true;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->envelope = rd16(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0x3F) {
      chan->envelope_enabled = false;
    } else if (op <= 0x5F) {
      chan->note_skip_counter = (int8_t)(op - 0x40);
      exxaf = 1;
      quit = true;
    } else if (op <= 0xCB) {
      chan->previous_note = chan->note;
      chan->note = (uint8_t)(f->transposition + op - 0x60);
      chan->sample_enabled = true;
      chan->position_in_sample = 0;
      chan->sample_noise_accumulator = 0;
      chan->volume_slide = 0;
      chan->noise_accumulator = 0;
      chan->note_accumulator = 0;
      chan->position_in_ornament = 0;
      chan->ton_accumulator = 0;
      chan->envelope_accumulator = 0;
      if (exxaf > 0) {
        chan->current_ton_sliding = 0;
        chan->ton_slide_direction = 0;
      }
      if (exxaf > 1) chan->ton_slide_step = 0;
      chan->note_skip_counter = 0;
      quit = true;
    } else if (op <= 0xEC) {
      chan->ornament_pointer = f->ornaments_pointers[op - 0xCC];
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->loop_ornament_position = rb(f->data, chan->ornament_pointer);
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->ornament_length = (uint8_t)(rb(f->data, chan->ornament_pointer) + 1);
      chan->ornament_pointer = (uint16_t)(chan->ornament_pointer + 1);
      chan->position_in_ornament = 0;
      chan->noise_accumulator = 0;
      chan->note_accumulator = 0;
    } else if (op == 0xED) {
      exxaf = 1;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->ton_slide_step = (int16_t)rd16(f->data, chan->address_in_pattern);
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else if (op == 0xEE) {
      exxaf = 0;
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      chan->ton_slide_step1 = rb(f->data, chan->address_in_pattern);
    } else if (op == 0xEF) {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      if (f->version > 7 && rb(f->data, chan->address_in_pattern) == 0xFE) {
        f->retrig = (uint8_t)chan_num;
      } else {
        chan->noise = rb(f->data, chan->address_in_pattern);
      }
    } else {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      f->delay = rb(f->data, chan->address_in_pattern);
    }
    chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
  } while (!quit);

  if (exxaf == 0) {
    int cts = get_note_freq(f, chan->previous_note) - get_note_freq(f, chan->note);
    chan->current_ton_sliding = (int16_t)cts;
    if (cts < 0) {
      chan->ton_slide_step = chan->ton_slide_step1;
      chan->ton_slide_direction = 1;
    } else {
      chan->ton_slide_step = (int16_t)(-chan->ton_slide_step1);
      chan->ton_slide_direction = 2;
    }
  }
}

/* Players.pas:11006-11092, GetRegisters. */
static void get_registers(ftc_file* f, ay_chip* chip, ftc_channel* chan,
                           uint8_t* temp_mixer) {
  uint8_t add_to_note, add_to_noise;
  uint8_t b;
  int j;

  add_to_note = (uint8_t)(chan->note_accumulator +
                          rb(f->data, (uint32_t)chan->ornament_pointer +
                                          (uint32_t)chan->position_in_ornament * 2 + 1));
  b = rb(f->data, (uint32_t)chan->ornament_pointer +
                      (uint32_t)chan->position_in_ornament * 2);
  if (b & 64) chan->note_accumulator = add_to_note;
  add_to_noise = (uint8_t)(chan->noise_accumulator + b);
  if ((int8_t)b < 0) chan->noise_accumulator = add_to_noise;
  chan->position_in_ornament++;
  if (chan->position_in_ornament == chan->ornament_length)
    chan->position_in_ornament = chan->loop_ornament_position;

  if (chan->sample_enabled) {
    uint32_t base = (uint32_t)chan->sample_pointer + (uint32_t)chan->position_in_sample * 5;
    int k;

    b = rb(f->data, base);
    j = (uint8_t)(chan->sample_noise_accumulator + b);
    if ((int8_t)b < 0) chan->sample_noise_accumulator = (uint8_t)j;
    if ((b & 64) == 0)
      chip->reg[6] = (uint8_t)((j + chan->noise + add_to_noise) & 31);
    else
      *temp_mixer |= 64;

    k = chan->ton_accumulator + rd16(f->data, base + 1);
    b = rb(f->data, base + 2);
    if ((int8_t)b < 0) chan->ton_accumulator = (int16_t)k;
    chan->addition_to_ton = (int16_t)k;
    if (b & 64) *temp_mixer |= 8;

    b = rb(f->data, base + 3);
    if (b & 32) {
      if (b & 16) {
        chan->volume_slide--;
        if (chan->volume_slide < -15) chan->volume_slide = -15;
      } else {
        chan->volume_slide++;
        if (chan->volume_slide > 15) chan->volume_slide = 15;
      }
    }
    j = chan->volume_slide + (b & 15);
    if ((int8_t)j < 0)
      j = 0;
    else if (j > 15)
      j = 15;
    chan->amplitude =
        (uint8_t)round_div256((chan->volume * 17 + (chan->volume > 7 ? 1 : 0)) * j);

    k = chan->envelope_accumulator + (int8_t)rb(f->data, base + 4);
    if ((int8_t)b < 0) chan->envelope_accumulator = (uint16_t)k;
    if ((b & 64) && chan->envelope_enabled) {
      uint16_t env = (uint16_t)(chan->envelope - k);
      chip->reg[11] = (uint8_t)(env & 0xFF);
      chip->reg[12] = (uint8_t)(env >> 8);
      chan->amplitude = (uint8_t)(chan->amplitude | 16);
    }

    chan->position_in_sample++;
    if (chan->position_in_sample == chan->sample_length)
      chan->position_in_sample = chan->loop_sample_position;
  } else {
    chan->amplitude = 0;
    *temp_mixer |= 72;
  }

  {
    uint8_t note_j = (uint8_t)(chan->note + add_to_note);
    if (note_j > 0x5F) note_j = 0x5F;
    int ton = get_note_freq(f, note_j) + chan->addition_to_ton;
    chan->current_ton_sliding = (int16_t)(chan->current_ton_sliding + chan->ton_slide_step);
    if ((chan->ton_slide_direction == 1 && chan->current_ton_sliding >= 0) ||
        (chan->ton_slide_direction == 2 && chan->current_ton_sliding < 0)) {
      chan->current_ton_sliding = 0;
      chan->ton_slide_step = 0;
    } else {
      ton += chan->current_ton_sliding;
    }
    chan->ton = (uint16_t)(ton & 0xFFF);
  }

  *temp_mixer >>= 1;
}

/* Players.pas:10845-11174, FTC_Get_Registers. MIG-0108: the
 * CheckLoopAndStop-equivalent check now lives in ftc_file_make_buffer's
 * tick loop instead of here (see its own comment) - functionally
 * equivalent since nothing else touches global_tick_counter in
 * between. MIG-0112: `chip` is the target AY register file this frame's
 * writes land in - `&f->ay.chip` for standalone/self playback, or an
 * EXTERNAL chip (another player's ay_engine.chip2) when this format is
 * playlist-paired as Turbosound's second voice. */
static void ftc_get_registers(ftc_file* f, ay_chip* chip) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter < 0) {
      if (rb(f->data, f->chan_a.address_in_pattern) == 255) {
        f->current_position++;
        if (rb(f->data, f->positions_offset + (uint32_t)f->current_position * 2) == 255)
          f->current_position = f->data[70]; /* FTC_Loop_Position */
        f->transposition =
            (uint8_t)rb(f->data, f->positions_offset +
                                      (uint32_t)f->current_position * 2 + 1);
        {
          uint8_t pat =
              rb(f->data, f->positions_offset + (uint32_t)f->current_position * 2);
          uint32_t base = f->patterns_pointer + (uint32_t)pat * 6;
          f->chan_a.address_in_pattern = rd16(f->data, base);
          f->chan_b.address_in_pattern = rd16(f->data, base + 2);
          f->chan_c.address_in_pattern = rd16(f->data, base + 4);
        }
      }
      pattern_interpreter(f, &f->chan_a, 1);
    }
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter < 0) pattern_interpreter(f, &f->chan_b, 2);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter < 0) pattern_interpreter(f, &f->chan_c, 3);
    f->delay_counter = f->delay;
  }

  /* Players.pas:11144-11156: retrig imitation + "don't rewrite the same
   * envelope shape" hardware quirk avoidance. */
  if (f->retrig != 0) {
    if (f->retrig == 1) chip->ton_counter_a = 0xFFFE;
    else if (f->retrig == 2) chip->ton_counter_b = 0xFFFE;
    else if (f->retrig == 3) chip->ton_counter_c = 0xFFFE;
  }
  if (chip->reg[13] != f->env_t || f->retrig != 0)
    ay_chip_set_ay_register_fast(chip, 13, f->env_t);
  f->retrig = 0;

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

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) + one ftc_get_registers
 * call - the reusable "advance one interrupt frame's worth of registers
 * into `chip`" building block MIG-0112's playlist-pairing driver needs
 * (player_step_registers, player.c). Returns false once this format's
 * own natural end is reached (mirrors Real_End[CNum] going true) - the
 * caller (whether ftc_file_make_buffer below, standalone, or a pairing
 * driver combining two formats) stops calling this once it returns
 * false, exactly matching MakeBufferTracker's own `Real_End_All :=
 * Real_End_All and Real_End[CNum]` accumulation. */
bool ftc_file_step_registers(ftc_file* f, ay_chip* chip) {
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
  ftc_get_registers(f, chip);
  return true;
}

int ftc_file_make_buffer(ftc_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(FTC_FILE_AY_FREQ_DEF /
                    (FTC_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    /* Players.pas: FTC_Get_Registers's own first statement, `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0108/MIG-0112) - ftc_file_step_registers is the shared
     * building block player_step_registers (player.c) also uses when
     * this format is playlist-paired as Turbosound's second voice; here
     * it targets this file's own private chip (standalone use). */
    if (!ftc_file_step_registers(f, &f->ay.chip)) break;
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
