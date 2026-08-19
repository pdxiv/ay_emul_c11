#include "ay_engine/formats/psm_file.h"

#include <string.h>

/* Players.pas:1014-1022, PSM_Table - a distinct table from PT3/PT1/GTR's
 * PT3NoteTable_ST and from FLS/STC/STP's ST_Table. */
static const uint16_t PSM_TABLE[96] = {
    0x0D3D, 0x0C7F, 0x0BCB, 0x0B22, 0x0A82, 0x09EB, 0x095D, 0x08D6, 0x0857,
    0x07DF, 0x076E, 0x0703, 0x069F, 0x063F, 0x05E6, 0x0591, 0x0541, 0x04F6,
    0x04AE, 0x046B, 0x042C, 0x03F0, 0x03B7, 0x0382, 0x034F, 0x0320, 0x02F3,
    0x02C8, 0x02A1, 0x027B, 0x0257, 0x0236, 0x0216, 0x01F8, 0x01DC, 0x01C1,
    0x01A8, 0x0190, 0x0179, 0x0164, 0x0150, 0x013D, 0x012C, 0x011B, 0x010B,
    0x00FC, 0x00EE, 0x00E0, 0x00D4, 0x00C8, 0x00BD, 0x00B2, 0x00A8, 0x009F,
    0x0096, 0x008D, 0x0085, 0x007E, 0x0077, 0x0070, 0x006A, 0x0064, 0x005E,
    0x0059, 0x0054, 0x004F, 0x004B, 0x0047, 0x0043, 0x003F, 0x003B, 0x0038,
    0x0035, 0x0032, 0x002F, 0x002D, 0x002A, 0x0028, 0x0025, 0x0023, 0x0021,
    0x001F, 0x001E, 0x001C, 0x001A, 0x0019, 0x0018, 0x0016, 0x0015, 0x0014,
    0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E};

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* Copies a fixed-width, space-padded (not NUL-terminated) field, then
 * trims leading/trailing bytes <= ' ' - see gtr_file.c's identical
 * helper for the full rationale (duplicated per this project's
 * per-file convention). */
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

/* Players.pas:16819-16881, GetTimePSM - walks the position/pattern data
 * opcode-only (no audio synthesis) to compute the song's total duration
 * and loop-point tick, in the same style as pt3_file.c's pt3_get_time
 * (MIG-0101). Bails to tm=0/lp=0 on: any of Pascal's own three
 * RaiseBadFileStructure triggers (the position-list-terminator sentinel
 * missing at Index[PSM_PositionsPointer], the decoded loop-index `l`
 * landing past the terminator, or the `j >= 65535`/`ra >= 65536`
 * pattern-jump-table bounds checks - all ported as literally as
 * possible, with the same effective thresholds since Pascal's checks
 * here already leave enough headroom for the immediately-following
 * word reads to stay within this port's 65536-byte data buffer, one
 * byte smaller than Pascal's oversized Index[0..65536] array - see the
 * CRITICAL SAFETY NOTE in this port's task ledger, MIG-0103); any
 * OTHER bounds violation Pascal's own checks don't cover (e.g. the
 * unbounded position-list-terminator scan, or the plain `Index[PSM_
 * PatternsPointer + d*7]` pattern-delay read, which Pascal never
 * bounds-checks at all, trusting its oversized buffer); or a
 * pathologically long walk (an added iteration cap - several of
 * Pascal's own `repeat...until False` loops here have no cap of their
 * own). */
static void psm_get_time(const psm_file* f, int64_t* out_tm,
                          int64_t* out_lp) {
  const uint8_t* dat = f->data;
  int64_t tm = 0, lp = 0;
  uint32_t p, l, j, ra = 0;
  uint8_t d, b, a, rc;
  long iterations;

  p = f->positions_pointer;
  if (dat[p] == 255) goto fail; /* RaiseBadFileStructure */
  l = p;

  iterations = 0;
  for (;;) {
    if (++iterations > (1 << 20)) goto fail;
    p += 2;
    if (p >= 65536) goto fail;
    if (dat[p] == 255) break;
  }
  p += 1;
  if (p >= 65536) goto fail;
  d = dat[p];
  if (d != 255) {
    l = f->positions_pointer + d;
    if ((int64_t)l > (int64_t)p - 3) goto fail; /* RaiseBadFileStructure */
  }

  p = f->positions_pointer;
  iterations = 0;
  for (;;) {
    if (++iterations > (1 << 20)) goto fail;
    if (p == l) lp = tm;
    d = dat[p];
    if (d == 255) break;

    j = f->patterns_pointer + (uint32_t)d * 7 + 5;
    if (j >= 65535) goto fail; /* RaiseBadFileStructure */
    j = rd16(dat, j);
    {
      uint32_t pat_hdr = f->patterns_pointer + (uint32_t)d * 7;
      if (pat_hdr >= 65536) goto fail;
      d = dat[pat_hdr];
    }

    a = 1;
    rc = 0;
    for (;;) {
      if (++iterations > (1 << 20)) goto fail;
      if (rc != 0) {
        rc--;
        if (rc == 0) j = ra;
      }
      if (j >= 65536) goto fail;
      b = dat[j];
      if (b <= 0x60 || b == 0x90 || (b >= 0xFC && b <= 0xFE)) {
        tm += (int64_t)d * a;
      } else if (b >= 0xB1 && b <= 0xB7) {
        j++;
      } else if (b >= 0xB8 && b <= 0xF8) {
        a = (uint8_t)(b - 0xB7);
      } else if (b == 0xF9) {
        ra = j + 3;
        if (ra >= 65536) goto fail; /* RaiseBadFileStructure */
        rc = dat[j + 2];
        j = rd16(dat, j) - 1;
      } else if (b == 0xFF) {
        break;
      }
      j++;
    }

    p += 2;
    if (p >= 65536) goto fail;
  }

  *out_tm = tm;
  *out_lp = lp;
  return;
fail:
  *out_tm = 0;
  *out_lp = 0;
}

/* ModTypes variant 13 (Players.pas:191-195): PSM_PositionsPointer@0
 * (word) PSM_SamplesPointer@2 (word) PSM_OrnamentsPointer@4 (word)
 * PSM_PatternsPointer@6 (word) PSM_Remark@8. */
psm_file_status psm_file_load(psm_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;

  memset(f, 0, sizeof(*f));

  if (size < 8) return PSM_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  f->positions_pointer = rd16(f->data, 0);
  f->samples_pointer = rd16(f->data, 2);
  f->ornaments_pointer = rd16(f->data, 4);
  f->patterns_pointer = rd16(f->data, 6);

  /* Players.pas: "else if FType = FT.PSM" (7532-7550). */
  if (f->positions_pointer > 8) {
    size_t remark_len = (size_t)f->positions_pointer - 8;
    if (8 + remark_len <= size) {
      const uint8_t* remark = f->data + 8;
      bool has_prefix =
          remark_len >= 5 && memcmp(remark, "psm1", 4) == 0 && remark[4] == 0;
      bool is_exactly_sentinel = has_prefix && remark_len == 5;
      if (!is_exactly_sentinel) {
        if (!has_prefix || remark_len <= 5) {
          copy_fixed_field(f->data, size, 8, remark_len, f->title,
                            sizeof(f->title));
        } else {
          copy_fixed_field(f->data, size, 8 + 5, remark_len - 5, f->title,
                            sizeof(f->title));
        }
      }
    }
  }

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * PSM_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3845-3871, InitTrackerModule's FT.PSM branch. */
  {
    uint8_t b = rb(f->data, f->positions_pointer);
    f->transposition = (int8_t)(rb(f->data, (uint32_t)f->positions_pointer + 1) + 48);
    f->delay = rb(f->data, f->patterns_pointer + (uint32_t)b * 7);
    f->chan_a.address_in_pattern =
        rd16(f->data, f->patterns_pointer + (uint32_t)b * 7 + 1);
    f->chan_b.address_in_pattern =
        rd16(f->data, f->patterns_pointer + (uint32_t)b * 7 + 3);
    f->chan_c.address_in_pattern =
        rd16(f->data, f->patterns_pointer + (uint32_t)b * 7 + 5);
  }
  f->chan_a.note_skip_counter = 1;
  f->chan_b.note_skip_counter = 1;
  f->chan_c.note_skip_counter = 1;
  f->chan_a.note = -128;
  f->chan_b.note = -128;
  f->chan_c.note = -128;
  f->delay_counter = 1;
  f->current_position = 0;
  f->finished = false;

  f->global_tick_counter = 0;

  f->do_loop = false;
  f->real_end_all = false;
  psm_get_time(f, &f->global_tick_max, &f->loop_tick); /* MIG-0101 */

  return PSM_FILE_OK;
}

/* Players.pas:11976-12094, PatternInterpreter. */
static void pattern_interpreter(psm_file* f, ay_chip* chip, psm_channel* chan) {
  uint16_t pat_addr = chan->address_in_pattern;
  bool quit = false;

  if (chan->ret_cnt != 0) {
    chan->ret_cnt--;
    if (chan->ret_cnt == 0) pat_addr = chan->ret_address;
  }

  do {
    uint8_t op = rb(f->data, pat_addr);
    if (op <= 0x5F) {
      if (chan->note < 0)
        chan->note = (int8_t)(f->transposition - op);
      else
        chan->note = (int8_t)(chan->note - op);
      if (chan->note < 0) chan->note = (int8_t)(chan->note + 96);
      chan->vol_cnt = chan->vol;
      chan->smp_tick = 0;
      chan->div_shift = 0;
      chan->loop_cnt = 1;
      if (chan->orn_tick < 0)
        chan->orn_tick = (int8_t)((uint8_t)chan->orn_tick & 0xE0);
      else
        chan->orn_tick = (int8_t)((uint8_t)chan->orn_tick & 0xC0);
      if (((uint8_t)chan->orn_tick & 0x40) != 0 && chan->orn >= 33) {
        uint16_t env;
        if (chan->env_type >= 0xB1) {
          ay_chip_set_ay_register_fast(chip, 13,
                                        (uint8_t)(chan->env_type - 0xB1 + 8));
          if (chan->env_div >= 0xF1)
            env = (uint16_t)((chan->env_div & 15) << 8);
          else
            env = chan->env_div;
          chip->reg[11] = (uint8_t)(env & 0xFF);
          chip->reg[12] = (uint8_t)(env >> 8);
          chan->orn_tick = (int8_t)((uint8_t)chan->orn_tick | 0x40);
        } else {
          uint8_t b = (uint8_t)(chan->env_type - 0xA1);
          ay_chip_set_ay_register_fast(chip, 13,
                                        (uint8_t)(((b & 3) << 1) | 8));
          b = (uint8_t)((b & 12) * 3 + (uint8_t)chan->note);
          if (b >= 48) {
            b = (uint8_t)(b - 48);
            if (b >= 48) b = (uint8_t)(b - 48);
          }
          env = PSM_TABLE[b + 48];
          chip->reg[11] = (uint8_t)(env & 0xFF);
          chip->reg[12] = (uint8_t)(env >> 8);
        }
      }
      quit = true;
    } else if (op == 0x60) {
      chan->smp_tick = (int8_t)((uint8_t)chan->smp_tick | 128);
      quit = true;
    } else if (op >= 0x61 && op <= 0x6F) {
      chan->samp = (uint8_t)(op - 0x61);
    } else if (op >= 0x70 && op <= 0x8F) {
      chan->orn = (uint8_t)(op - 0x70);
      chan->orn_tick = 0;
    } else if (op == 0x90) {
      quit = true;
    } else if (op >= 0x91 && op <= 0x9F) {
      chan->vol = (uint8_t)(op - 0x90);
    } else if (op == 0xA0) {
      chan->orn_tick = (int8_t)op;
    } else if (op >= 0xA1 && op <= 0xB0) {
      chan->orn = 33;
      chan->env_type = op;
      chan->orn_tick = (int8_t)((uint8_t)chan->orn_tick | 0x40);
    } else if (op >= 0xB1 && op <= 0xB7) {
      uint16_t env;
      chan->env_type = op;
      pat_addr = (uint16_t)(pat_addr + 1);
      chan->env_div = rb(f->data, pat_addr);
      ay_chip_set_ay_register_fast(chip, 13,
                                    (uint8_t)(chan->env_type - 0xB1 + 8));
      if (chan->env_div >= 0xF1)
        env = (uint16_t)((chan->env_div & 15) << 8);
      else
        env = chan->env_div;
      chip->reg[11] = (uint8_t)(env & 0xFF);
      chip->reg[12] = (uint8_t)(env >> 8);
      chan->orn_tick = (int8_t)((uint8_t)chan->orn_tick | 0x40);
    } else if (op >= 0xB8 && op <= 0xF8) {
      chan->number_of_notes_to_skip = (uint8_t)(op - 0xB7);
    } else if (op == 0xF9) {
      chan->ret_address = (uint16_t)(pat_addr + 4);
      chan->ret_cnt = rb(f->data, (uint32_t)(pat_addr + 3));
      pat_addr = (uint16_t)(rd16(f->data, (uint32_t)pat_addr + 1) - 1);
    } else if (op >= 0xFA && op <= 0xFB) {
      chan->orn = (uint8_t)(op - 0xFA + 32);
    } else {
      quit = true;
    }
    pat_addr = (uint16_t)(pat_addr + 1);
  } while (!quit);

  chan->address_in_pattern = pat_addr;
  chan->note_skip_counter = chan->number_of_notes_to_skip;
}

/* Players.pas:12099-12190, ChangeRegisters. */
static void change_registers(psm_file* f, ay_chip* chip, psm_channel* chan,
                              uint8_t* temp_mixer) {
  uint8_t b, b1, b2;
  uint16_t w, wo, ws;

  b = (uint8_t)(chan->note & 127);
  b2 = (uint8_t)chan->orn_tick;
  wo = rd16(f->data, f->ornaments_pointer + (uint32_t)chan->orn * 2);
  if (((uint8_t)chan->orn_tick & 0x60) == 0)
    b = (uint8_t)(b + rb(f->data, (uint32_t)wo + 2 + b2));
  if ((int8_t)b < 0)
    b = 0;
  else if (b > 95)
    b = 95;
  chan->ton = PSM_TABLE[b];

  b2 = (uint8_t)((uint8_t)chan->smp_tick * 3);
  ws = rd16(f->data, f->samples_pointer + (uint32_t)chan->samp * 2);
  b = rb(f->data, (uint32_t)ws + 2 + b2);
  b1 = rb(f->data, (uint32_t)ws + 2 + b2 + 1);
  b2 = rb(f->data, (uint32_t)ws + 2 + b2 + 2);

  w = (uint16_t)(((uint16_t)(b1 & 7) << 8) + b2);
  if (b1 & 4) w = (uint16_t)(w | 0xF800);

  chan->div_shift = (uint16_t)(chan->div_shift + w);
  chan->ton = (uint16_t)(chan->ton + chan->div_shift);
  if ((int16_t)chan->ton < 0)
    chan->ton = 0;
  else if (chan->ton >= 4096)
    chan->ton = 4095;

  chan->amplitude = (uint8_t)(b & 15);
  if ((uint8_t)chan->orn_tick & 0x40)
    chan->amplitude = (uint8_t)(chan->amplitude | 16);
  chan->amplitude = (uint8_t)(chan->amplitude + chan->vol_cnt - 15);
  if ((int8_t)chan->amplitude < 0 || (int8_t)chan->smp_tick < 0)
    chan->amplitude = 0;

  *temp_mixer = (uint8_t)(((b >> 1) & 0x48) | *temp_mixer);
  if ((int8_t)chan->smp_tick < 0) *temp_mixer = (uint8_t)(*temp_mixer | 0x40);

  if ((int8_t)b >= 0 && chan->amplitude != 0)
    chip->reg[6] = (uint8_t)(b1 >> 3);

  b = (uint8_t)(((uint8_t)chan->smp_tick & 31) + 1);
  b1 = rb(f->data, ws);
  b2 = rb(f->data, (uint32_t)ws + 1);
  if (b > (b1 & 31)) {
    if ((b2 & 0xE0) == 0) {
      chan->smp_tick = (int8_t)((uint8_t)chan->smp_tick | 128);
    } else {
      b = (uint8_t)(b2 & 31);
      chan->loop_cnt--;
      if (chan->loop_cnt == 0) {
        chan->loop_cnt = (uint8_t)(b2 >> 5);
        if ((b1 & 0x20) == 0)
          chan->vol_cnt = (uint8_t)(chan->vol_cnt + (b1 >> 6));
        else
          chan->vol_cnt = (uint8_t)(chan->vol_cnt - (b1 >> 6) - 1);
        if ((int8_t)chan->vol_cnt < 0)
          chan->vol_cnt = 0;
        else if (chan->vol_cnt > 15)
          chan->vol_cnt = 15;
      }
    }
  }
  chan->smp_tick =
      (int8_t)(((b ^ (uint8_t)chan->smp_tick) & 31) ^ (uint8_t)chan->smp_tick);

  b = (uint8_t)(((uint8_t)chan->orn_tick & 31) + 1);
  b1 = rb(f->data, wo);
  b2 = rb(f->data, (uint32_t)wo + 1);
  if (b > b1) {
    if ((int8_t)b2 < 0)
      b = b2;
    else
      chan->orn_tick = (int8_t)((uint8_t)chan->orn_tick | 0x20);
  }
  chan->orn_tick =
      (int8_t)(((b ^ (uint8_t)chan->orn_tick) & 31) ^ (uint8_t)chan->orn_tick);

  *temp_mixer >>= 1;
}

/* Players.pas:11974-12281, PSM_Get_Registers. MIG-0108: the
 * CheckLoopAndStop-equivalent check now lives in psm_file_make_buffer's
 * tick loop instead of here (see its own comment) - functionally
 * equivalent since nothing else touches global_tick_counter in between,
 * and real Pascal itself calls CheckLoopAndStop BEFORE incrementing
 * Global_Tick_Counter and BEFORE the separate `if Finished then exit`
 * check below (Players.pas:12213-12222) - i.e. CheckLoopAndStop and
 * Finished are two genuinely independent mechanisms, not one
 * superseding the other; see this file's own `finished` field comment
 * and player.c's player_real_end_all for how both are now exposed.
 * Channel processing order is C, B, A (unlike every other format ported
 * so far, which goes A, B, C) - PSM_C alone drives the shared position
 * advance. MIG-0112: `chip` is the target AY register file this frame's
 * writes land in - see stc_file.c's stc_get_registers for the full
 * rationale. */
static void psm_get_registers(psm_file* f, ay_chip* chip) {
  uint8_t temp_mixer;

  f->global_tick_counter++;

  ay_chip_set_ay_register_fast(chip, 8, 0);
  ay_chip_set_ay_register_fast(chip, 9, 0);
  ay_chip_set_ay_register_fast(chip, 10, 0);

  if (f->finished) return;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->chan_c.note_skip_counter--;
    if (f->chan_c.note_skip_counter == 0) {
      if (rb(f->data, f->chan_c.address_in_pattern) == 255) {
        f->current_position++;
        uint8_t b =
            rb(f->data, f->positions_pointer + (uint32_t)f->current_position * 2);
        if (b == 255) {
          b = rb(f->data, (uint32_t)f->positions_pointer +
                               (uint32_t)f->current_position * 2 + 1);
          if (b == 255) {
            f->finished = true;
            return;
          }
          f->current_position = b;
          b = rb(f->data, f->positions_pointer + (uint32_t)b * 2);
        }
        f->transposition =
            (int8_t)(rb(f->data, (uint32_t)f->positions_pointer +
                                      (uint32_t)f->current_position * 2 + 1) +
                     48);
        f->delay = rb(f->data, f->patterns_pointer + (uint32_t)b * 7);
        f->chan_a.address_in_pattern =
            rd16(f->data, f->patterns_pointer + (uint32_t)b * 7 + 1);
        f->chan_b.address_in_pattern =
            rd16(f->data, f->patterns_pointer + (uint32_t)b * 7 + 3);
        f->chan_c.address_in_pattern =
            rd16(f->data, f->patterns_pointer + (uint32_t)b * 7 + 5);
        f->chan_a.ret_cnt = 0;
        f->chan_b.ret_cnt = 0;
        f->chan_c.ret_cnt = 0;
        f->chan_a.note_skip_counter = 1;
        f->chan_b.note_skip_counter = 1;
        f->chan_a.note = (int8_t)((uint8_t)f->chan_a.note | 128);
        f->chan_b.note = (int8_t)((uint8_t)f->chan_b.note | 128);
        f->chan_c.note = (int8_t)((uint8_t)f->chan_c.note | 128);
      }
      pattern_interpreter(f, chip, &f->chan_c);
    }
    f->chan_b.note_skip_counter--;
    if (f->chan_b.note_skip_counter == 0) pattern_interpreter(f, chip, &f->chan_b);
    f->chan_a.note_skip_counter--;
    if (f->chan_a.note_skip_counter == 0) pattern_interpreter(f, chip, &f->chan_a);
    f->delay_counter = f->delay;
  }

  temp_mixer = 0;
  change_registers(f, chip, &f->chan_a, &temp_mixer);
  change_registers(f, chip, &f->chan_b, &temp_mixer);
  change_registers(f, chip, &f->chan_c, &temp_mixer);

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
}

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) + one psm_get_registers
 * call - see stc_file.c's stc_file_step_registers for the full
 * rationale. */
bool psm_file_step_registers(psm_file* f, ay_chip* chip) {
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
  psm_get_registers(f, chip);
  return true;
}

int psm_file_make_buffer(psm_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(PSM_FILE_AY_FREQ_DEF /
                    (PSM_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    /* Players.pas: PSM_Get_Registers's own first statement, `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0108/MIG-0112) - psm_file_step_registers is the shared
     * building block player_step_registers (player.c) also uses when
     * this format is playlist-paired as Turbosound's second voice; here
     * it targets this file's own private chip (standalone use). Force_Loop
     * (the original's TS-pair "continue playback of the shorter module"
     * case) doesn't apply - TSMode isn't ported (MIG-0007), so only
     * Do_Loop matters. */
    if (!psm_file_step_registers(f, &f->ay.chip)) break;
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
