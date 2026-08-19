#include "ay_engine/formats/psc_file.h"

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

/* Players.pas:15664-15767, GetTimePSC's per-channel opcode-walk (channels
 * A and C - identical opcode ranges: $C0..$FF is the only terminal case,
 * $67..$6D/$6F..$7B are 1-byte-payload no-ops, $6E reads a new shared
 * delay byte `b`, anything else is a bare 1-byte no-op). `*a` is the
 * per-channel note-skip counter (mirrors Pascal's a1/a3); when it reaches
 * 0 the opcode stream is walked until a terminal $C0..$FF opcode sets the
 * next skip count. Unlike the original (which has no bound on this
 * per-channel walk beyond the 65536-byte buffer itself, tolerated by
 * Pascal's oversized Index array), this adds an explicit iteration cap
 * (1<<16) per MIG-0103's own safety requirement - PSC's ported
 * get-time function, unlike PT3's/FTC's, has no equivalent to Pascal's
 * own DLCatcher to bound a hostile/corrupt file's walk, so this port adds
 * one. Returns false on any bounds/iteration-cap violation (caller must
 * bail: Tm=Lp=0, matching RaiseBadFileStructure's caller-visible effect
 * without actually raising). */
static bool psc_time_step_ac(const uint8_t* d, uint32_t* j, int* a,
                              uint8_t* b) {
  int guard = 1 << 16;

  (*a)--;
  if (*a != 0) return true;
  for (;;) {
    if (--guard < 0) return false;
    if (*j >= 65536) return false;
    uint8_t op = d[*j];
    if (op >= 0xC0) {
      *a = op - 0xBF;
      (*j)++;
      return true;
    } else if ((op >= 0x67 && op <= 0x6D) || (op >= 0x6F && op <= 0x7B)) {
      (*j)++;
    } else if (op == 0x6E) {
      (*j)++;
      if (*j >= 65536) return false;
      *b = d[*j];
    }
    (*j)++;
  }
}

/* Players.pas:15664-15767, GetTimePSC's channel-B opcode-walk - same
 * shape as psc_time_step_ac but with channel B's own distinct opcode
 * ranges ($67..$6D/$6F..$79/$7B vs A/C's $67..$6D/$6F..$7B, plus the
 * extra $7A: Inc(j2,3) case A/C don't have). See psc_time_step_ac for the
 * safety-cap rationale. */
static bool psc_time_step_b(const uint8_t* d, uint32_t* j, int* a,
                             uint8_t* b) {
  int guard = 1 << 16;

  (*a)--;
  if (*a != 0) return true;
  for (;;) {
    if (--guard < 0) return false;
    if (*j >= 65536) return false;
    uint8_t op = d[*j];
    if (op >= 0xC0) {
      *a = op - 0xBF;
      (*j)++;
      return true;
    } else if ((op >= 0x67 && op <= 0x6D) || (op >= 0x6F && op <= 0x79) ||
               op == 0x7B) {
      (*j)++;
    } else if (op == 0x6E) {
      (*j)++;
      if (*j >= 65536) return false;
      *b = d[*j];
    } else if (op == 0x7A) {
      *j += 3;
    }
    (*j)++;
  }
}

/* Players.pas:15664-15767, GetTimePSC - computes the song's total
 * duration and loop-point tick, walking the position list (at
 * PSC_PatternsPointer, the same table PSC_Get_Registers's own
 * positions_pointer walk consumes at playback time) exactly once. Called
 * from psc_file_load before f->positions_pointer is ever mutated by
 * playback, so f->positions_pointer at that point still holds the raw
 * PSC_PatternsPointer value read from the file header - no separate
 * field is needed to remember it. On a malformed file (any bounds
 * violation the original's own RaiseBadFileStructure guards would have
 * caught, or the added per-channel-walk iteration cap tripping), returns
 * 0/0 rather than raising, matching pt3_get_time's own precedent (a
 * duration-precompute failure degrades to "no known duration" rather
 * than failing the whole load, which has already succeeded by the time
 * this runs). */
static void psc_get_time(const psc_file* f, int64_t* out_tm,
                          int64_t* out_lp) {
  const uint8_t* d = f->data;
  int64_t tm = 0, lp = 0;
  uint8_t b = (uint8_t)f->delay;
  uint32_t patterns_pointer = f->positions_pointer;
  uint32_t pptr, cptr;

  pptr = patterns_pointer + 1;
  if (pptr >= 65536) goto fail;
  while (d[pptr] != 255) {
    pptr += 8;
    if (pptr >= 65536) goto fail;
  }
  if (pptr >= 65536 - 2) goto fail;
  cptr = rd16(d, pptr + 1);
  cptr += 1;

  pptr = patterns_pointer + 1;
  if (pptr >= 65536) goto fail;
  while (d[pptr] != 255) {
    if (pptr == cptr) lp = tm;
    if (pptr >= 65536 - 6) goto fail;
    {
      uint32_t j1 = rd16(d, pptr + 1);
      uint32_t j2 = rd16(d, pptr + 3);
      uint32_t j3 = rd16(d, pptr + 5);
      uint8_t lines = d[pptr];
      int a1 = 1, a2 = 1, a3 = 1;
      int i;

      pptr += 8;
      if (pptr >= 65536) goto fail;
      for (i = 0; i < lines; i++) {
        if (!psc_time_step_ac(d, &j1, &a1, &b)) goto fail;
        if (!psc_time_step_b(d, &j2, &a2, &b)) goto fail;
        if (!psc_time_step_ac(d, &j3, &a3, &b)) goto fail;
        tm += b;
      }
    }
  }
  *out_tm = tm;
  *out_lp = lp;
  return;

fail:
  *out_tm = 0;
  *out_lp = 0;
}

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

  /* Players.pas: "else if FType = FT.PSC" (7354-7370). */
  copy_fixed_field(f->data, size, 0x19, 20, f->title, sizeof(f->title));
  copy_fixed_field(f->data, size, 0x31, 20, f->author, sizeof(f->author));

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
  f->do_loop = false;
  f->real_end_all = false;
  psc_get_time(f, &f->global_tick_max, &f->loop_tick); /* MIG-0103 */

  return PSC_FILE_OK;
}

/* Players.pas:10057-10235, PatternInterpreter. Like FTC (and unlike ASC/
 * GTR/STC), there is no `break` anywhere - the loop is `repeat...until
 * quit`, so the common trailing address increment always fires, and
 * every branch's own inner increments (if any) are additive on top,
 * with no double-counting risk. chan_id (0=A,1=B,2=C) replicates the
 * Pascal source's `@Chan = @PlParams[CNum].PSC_B` pointer-identity
 * checks for opcodes $7A/$7B. */
static void pattern_interpreter(psc_file* f, ay_chip* chip, psc_channel* chan,
                                 int chan_id) {
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
            chip, 13, (uint8_t)(rb(f->data, chan->address_in_pattern) & 15));
        uint16_t env = rd16(f->data, (uint32_t)chan->address_in_pattern + 1);
        chip->reg[11] = (uint8_t)(env & 0xFF);
        chip->reg[12] = (uint8_t)(env >> 8);
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
static void get_registers(psc_file* f, ay_chip* chip, psc_channel* chan,
                           uint8_t* temp_mixer) {
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
      uint16_t env = (uint16_t)(chip->reg[11] | (chip->reg[12] << 8));
      env = (uint16_t)(env + (int8_t)rb(f->data, (uint32_t)chan->sample_pointer +
                                                      (uint32_t)chan->position_in_sample * 6 + 2));
      chip->reg[11] = (uint8_t)(env & 0xFF);
      chip->reg[12] = (uint8_t)(env >> 8);
    } else {
      chan->noise_accumulator = (uint8_t)(
          chan->noise_accumulator +
          rb(f->data, (uint32_t)chan->sample_pointer +
                          (uint32_t)chan->position_in_sample * 6 + 2));
      if ((b & 8) == 0)
        chip->reg[6] = (uint8_t)(chan->noise_accumulator & 31);
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

/* Players.pas:10053-10423, PSC_Get_Registers. MIG-0108: the
 * CheckLoopAndStop-equivalent check now lives in psc_file_make_buffer's
 * tick loop instead of here (see its own comment) - functionally
 * equivalent since nothing else touches global_tick_counter in
 * between. MIG-0112: `chip` is the target AY register file this frame's
 * writes land in - `&f->ay.chip` for standalone/self playback, or an
 * EXTERNAL chip (another player's ay_engine.chip2) when this format is
 * playlist-paired as Turbosound's second voice. */
static void psc_get_registers(psc_file* f, ay_chip* chip) {
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
    if (f->chan_a.note_skip_counter == 0)
      pattern_interpreter(f, chip, &f->chan_a, 0);
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter == 0)
      pattern_interpreter(f, chip, &f->chan_b, 1);
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter == 0)
      pattern_interpreter(f, chip, &f->chan_c, 2);

    f->chan_a.noise_accumulator =
        (uint8_t)(f->chan_a.noise_accumulator + f->noise_base);
    f->chan_b.noise_accumulator =
        (uint8_t)(f->chan_b.noise_accumulator + f->noise_base);
    f->chan_c.noise_accumulator =
        (uint8_t)(f->chan_c.noise_accumulator + f->noise_base);
    f->delay_counter = f->delay;
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

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) + one psc_get_registers
 * call - the reusable "advance one interrupt frame's worth of registers
 * into `chip`" building block MIG-0112's playlist-pairing driver needs
 * (player_step_registers, player.c). Returns false once this format's
 * own natural end is reached (mirrors Real_End[CNum] going true) - the
 * caller (whether psc_file_make_buffer below, standalone, or a pairing
 * driver combining two formats) stops calling this once it returns
 * false, exactly matching MakeBufferTracker's own `Real_End_All :=
 * Real_End_All and Real_End[CNum]` accumulation. */
bool psc_file_step_registers(psc_file* f, ay_chip* chip) {
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
  psc_get_registers(f, chip);
  return true;
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
    /* Players.pas: PSC_Get_Registers's own first statement, `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0108/MIG-0112) - psc_file_step_registers is the shared
     * building block player_step_registers (player.c) also uses when
     * this format is playlist-paired as Turbosound's second voice; here
     * it targets this file's own private chip (standalone use). */
    if (!psc_file_step_registers(f, &f->ay.chip)) break;
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
