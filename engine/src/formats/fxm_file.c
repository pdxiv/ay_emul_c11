#include "ay_engine/formats/fxm_file.h"

#include <string.h>

#include "ay_engine/util/trace_log.h"

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

/* Players.pas:14422-14950, GetTimeFXM - forward-declared here so
 * fxm_file_load (below) can call it; defined further down, after the
 * real playback interpreter it deliberately parallels (fxm_get_time
 * walks the same per-channel bytecode VM opcodes as
 * pattern_interpreter()/get_registers() above, but only to accumulate a
 * tick count and detect the song's loop point - it never touches
 * note/ton/volume/amplitude or any AY register). */
static void fxm_get_time(const fxm_file* f, uint16_t address, int64_t* out_tm,
                          int64_t* out_lp);

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

  f->do_loop = false;
  f->real_end_all = false;
  fxm_get_time(f, address, &f->global_tick_max, &f->loop_tick); /* MIG-0104 */

  return FXM_FILE_OK;
}

/* Players.pas:11702-11713, RealGetRegisters. */
static void real_get_registers(fxm_file* f, ay_chip* chip, fxm_channel* chan) {
  chip->reg[6] = (uint8_t)(f->noise_base & 31);
  chan->b2e = false;
  if (chan->ton != 0)
    chan->amplitude = (uint8_t)(chan->volume & 15);
  else
    chan->amplitude = 0;
}

/* Players.pas:11715-11789, GetRegisters. */
static void get_registers(fxm_file* f, ay_chip* chip, fxm_channel* chan) {
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
  real_get_registers(f, chip, chan);
}

/* Players.pas:11791-11949, PatternInterpreter. Unlike every other format
 * ported so far, there is no common trailing address increment - every
 * branch below is fully self-contained (matches the Pascal literally:
 * `repeat case ... end until False`, no statement between `end` and
 * `until`), and the only way out of the loop is the note branch's
 * `return` (Pascal `exit`). */
static void pattern_interpreter(fxm_file* f, ay_chip* chip, fxm_channel* chan) {
  chan->note_skip_counter--;
  if (chan->note_skip_counter != 0) {
    get_registers(f, chip, chan);
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
        real_get_registers(f, chip, chan);
      } else {
        get_registers(f, chip, chan);
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
      /* Players.pas:11866-11868 (`Dec(Stek[i-2]); if Stek[i-2] and 255<>0
       * then ... else ...`) indexes Stek[i-2]/Stek[i-1] with no bounds
       * check either - a loop-decrement opcode with no matching loop-
       * start (0x82) pushed onto the stack first. Well-formed FXM pattern
       * data never emits 0x83 without a prior balancing 0x82, so this is
       * believed unreachable in practice (matching the original's own
       * unguarded behavior), but C has no array-bounds trap to fall back
       * on the way Pascal's range-checked builds would - i<2 here would
       * be an out-of-bounds stek[] access (undefined behavior, not just
       * "wrong output"), so it's guarded defensively: skip the loop-
       * control effect entirely and just advance past the opcode, same
       * as this opcode's own "loop exhausted" branch below. */
      if (i < 2) {
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      } else {
        chan->stek[i - 2] = (uint16_t)(chan->stek[i - 2] - 1);
        if ((chan->stek[i - 2] & 255) != 0) {
          chan->address_in_pattern = chan->stek[i - 1];
        } else {
          chan->stek_len -= 2;
          chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
        }
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
      /* Players.pas:11908-11909 (`Address_In_Pattern := Stek[i-1];
       * Dec(StekLen)`) - a "return" with an empty call stack, same
       * unguarded-in-both-languages shape as 0x83 above. Guarded here to
       * avoid an out-of-bounds stek[] access; the only defined-but-inert
       * choice available is to leave address_in_pattern where it is
       * rather than invent a jump target that isn't on the stack -
       * believed unreachable for well-formed pattern data (every real
       * corpus file's data has never been observed hitting this).
       * Advances address_in_pattern past the opcode byte regardless (not
       * a true no-op) so this can never stall pattern_interpreter's own
       * unbounded opcode loop into spinning on the same byte forever. */
      if (i < 1) {
        chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
      } else {
        chan->address_in_pattern = chan->stek[i - 1];
        chan->stek_len = i - 1;
      }
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
      /* Players.pas:11949 (`Transposit := Stek[i-1]; Dec(StekLen)`) - same
       * unguarded-stack-underflow shape as 0x83/0x89 above, guarded here
       * for the same reason (avoid undefined out-of-bounds stek[]
       * access); leaves chan->transposit unchanged when the stack is
       * already empty rather than reading garbage. */
      if (i >= 1) {
        chan->transposit = (int8_t)chan->stek[i - 1];
        chan->stek_len = i - 1;
      }
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    } else {
      chan->address_in_pattern = (uint16_t)(chan->address_in_pattern + 1);
    }
  }
}

/* Players.pas:11700-11972, FXM_Get_Registers. MIG-0108: the
 * CheckLoopAndStop-equivalent check now lives in fxm_file_make_buffer's
 * tick loop instead of here (see its own comment) - functionally
 * equivalent since nothing else touches global_tick_counter in
 * between. MIG-0112: `chip` is the target AY register file this frame's
 * writes land in - see stc_file.c's stc_get_registers for the full
 * rationale. */
static void fxm_get_registers(fxm_file* f, ay_chip* chip) {
  pattern_interpreter(f, chip, &f->chan_a);
  pattern_interpreter(f, chip, &f->chan_b);
  pattern_interpreter(f, chip, &f->chan_c);

  uint16_t ton_a = (uint16_t)(f->chan_a.ton & 0xFFF);
  uint16_t ton_b = (uint16_t)(f->chan_b.ton & 0xFFF);
  uint16_t ton_c = (uint16_t)(f->chan_c.ton & 0xFFF);
  chip->reg[0] = (uint8_t)(ton_a & 0xFF);
  chip->reg[1] = (uint8_t)(ton_a >> 8);
  chip->reg[2] = (uint8_t)(ton_b & 0xFF);
  chip->reg[3] = (uint8_t)(ton_b >> 8);
  chip->reg[4] = (uint8_t)(ton_c & 0xFF);
  chip->reg[5] = (uint8_t)(ton_c >> 8);

  ay_chip_set_ay_register_fast(chip, 8, f->chan_a.amplitude);
  ay_chip_set_ay_register_fast(chip, 9, f->chan_b.amplitude);
  ay_chip_set_ay_register_fast(chip, 10, f->chan_c.amplitude);

  uint8_t mixer = (uint8_t)((f->chan_a.fxm_mixer | (f->chan_b.fxm_mixer << 1) |
                             (f->chan_c.fxm_mixer << 2)) &
                            0x3F);
  ay_chip_set_ay_register_fast(chip, 7, mixer);

  trace_log_ay(f->global_tick_counter, "fxm_a", ton_a, f->chan_a.amplitude);
  trace_log_ay(f->global_tick_counter, "fxm_b", ton_b, f->chan_b.amplitude);
  trace_log_ay(f->global_tick_counter, "fxm_c", ton_c, f->chan_c.amplitude);
  trace_log_ay(f->global_tick_counter, "fxm_mixer", mixer, 0);

  f->global_tick_counter++;
}

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) + one fxm_get_registers
 * call - see stc_file.c's stc_file_step_registers for the full
 * rationale. */
bool fxm_file_step_registers(fxm_file* f, ay_chip* chip) {
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
  fxm_get_registers(f, chip);
  return true;
}

/* Players.pas:14422-14950, GetTimeFXM (duration precompute, MIG-0104 -
 * SQT/FXM were the two formats deliberately deferred by that entry's
 * first pass; this is the FXM half of that follow-up debt). Unlike
 * every other already-ported GetTimeXXX, FXM's per-channel walk is a
 * bytecode VM with call/return and loop-counter opcodes (same VM
 * pattern_interpreter() above implements for real playback), so both
 * GetTimeFXM's outer procedure and its nested FXM_Loop_Found function
 * are transcribed here as three-channel opcode walks, sharing one
 * fxm_time_channel_step() helper for the (structurally identical
 * modulo two differences noted below) per-channel opcode dispatch that
 * Players.pas itself writes out three times (once per channel) in each
 * of the two Pascal routines.
 *
 * The two differences between the outer procedure's per-channel walk
 * and FXM_Loop_Found's are: (1) the outer procedure records a
 * "candidate loop-recurrence state" (j11/j22/j33) on every $80 jump and
 * $83 loop-taken event - passed here as the `candidate` out-parameter,
 * NULL from FXM_Loop_Found (whose j11/j22/j33 are read-only search
 * targets instead); (2) FXM_Loop_Found's $82 opcode does an inline
 * three-way equality check against those targets mid-walk (checked via
 * `check_match`/`t1..t3` here, using the live j1/j2/j3 pointers so the
 * comparison always sees each channel's current cursor regardless of
 * which channel is being stepped) - the outer procedure has no such
 * check at all.
 *
 * Players.pas's Stek-equivalent call/loop stacks here (`fxms1/2/3`) are
 * unbounded dynamic arrays in the original; per this file's own
 * existing convention for the SAME opcodes in pattern_interpreter()
 * above (fxm_channel.stek/stek_len, FXM_STEK_MAX), fixed-size local
 * arrays are used instead - a deliberate, documented bound already
 * established in this file, not a new one invented for this port. */
typedef enum {
  FXM_TIME_STEP_CONTINUE,
  FXM_TIME_STEP_MATCH, /* FXM_Loop_Found's $82 mid-walk equality hit */
  FXM_TIME_STEP_ERROR, /* bounds violation - Pascal's RaiseBadFileStructure */
} fxm_time_step_result;

static fxm_time_step_result fxm_time_channel_step(
    const uint8_t* d, uint32_t* j, int* a, bool* f7, bool* f6,
    uint16_t* stack, int* stack_len, uint16_t* candidate, bool check_match,
    const uint32_t* j1p, const uint32_t* j2p, const uint32_t* j3p,
    uint16_t t1, uint16_t t2, uint16_t t3) {
  (*a)--;
  if (*a != 0) return FXM_TIME_STEP_CONTINUE;
  *f7 = false;
  *f6 = false;
  for (;;) {
    if (*j >= 65536) return FXM_TIME_STEP_ERROR;
    uint8_t op = d[*j];
    if (op <= 0x7F || op >= 0x8F) {
      (*j)++;
      if (*j >= 65536) return FXM_TIME_STEP_ERROR;
      *a = d[*j];
      (*j)++;
      break;
    } else if (op == 0x80) {
      if (*j >= 65536 - 2) return FXM_TIME_STEP_ERROR;
      *j = rd16(d, *j + 1);
      if (candidate) *candidate = (uint16_t)*j;
      *f7 = true;
    } else if (op == 0x81) {
      if (*j >= 65536 - 3) return FXM_TIME_STEP_ERROR;
      if (*stack_len >= FXM_STEK_MAX) return FXM_TIME_STEP_ERROR;
      stack[(*stack_len)++] = (uint16_t)(*j + 3);
      *j = rd16(d, *j + 1);
    } else if (op == 0x82) {
      if (check_match && *j1p == t1 && *j2p == t2 && *j3p == t3)
        return FXM_TIME_STEP_MATCH;
      if (*stack_len + 2 > FXM_STEK_MAX) return FXM_TIME_STEP_ERROR;
      (*j)++;
      if (*j >= 65536) return FXM_TIME_STEP_ERROR;
      stack[*stack_len] = d[*j];
      (*j)++;
      stack[*stack_len + 1] = (uint16_t)*j;
      *stack_len += 2;
    } else if (op == 0x83) {
      if (*stack_len < 2) return FXM_TIME_STEP_ERROR;
      stack[*stack_len - 2] = (uint16_t)(stack[*stack_len - 2] - 1);
      if ((stack[*stack_len - 2] & 255) != 0) {
        *j = stack[*stack_len - 1];
        if (candidate) {
          if (*j < 2) return FXM_TIME_STEP_ERROR;
          *candidate = (uint16_t)(*j - 2);
        }
        *f6 = true;
      } else {
        *stack_len -= 2;
        (*j)++;
      }
    } else if (op == 0x84 || op == 0x85 || op == 0x88 || op == 0x8D ||
               op == 0x8E) {
      *j += 2;
    } else if (op == 0x86 || op == 0x87 || op == 0x8C) {
      *j += 3;
    } else if (op == 0x89) {
      if (*stack_len < 1) return FXM_TIME_STEP_ERROR;
      *j = stack[*stack_len - 1];
      *stack_len -= 1;
    } else if (op == 0x8A || op == 0x8B) {
      (*j)++;
    }
    if (*j >= 65536) return FXM_TIME_STEP_ERROR;
  }
  return FXM_TIME_STEP_CONTINUE;
}

/* Players.pas:14424-14669, the FXM_Loop_Found nested function: replays
 * the three channels from the song's start (`address`), looking for the
 * exact state (j11,j22,j33) - the outer walk's latest jump/loop-taken
 * targets - to recur. Returns the tick offset (from replay start) it
 * recurred at via *out_lp, or false if never found before the outer
 * procedure's own COND1 heuristic (see fxm_get_time) recurs again first
 * (matching Pascal's `until (...)` bound on FXM_Loop_Found's own replay
 * loop - the replay isn't unbounded, but it has no *numeric* tick cap of
 * its own the way the outer procedure's `if tm > 180000` does, so
 * `guard` below is this port's own backstop against a state-machine
 * cycle that never satisfies COND1, matching this task's "add your own
 * outer iteration cap" guidance). On any bounds violation
 * (RaiseBadFileStructure in the original), *out_error is set true so
 * the caller aborts the ENTIRE computation to Tm=0/Lp=0, exactly as an
 * uncaught Pascal exception raised from within this nested function
 * would propagate out of GetTimeFXM as a whole - not merely make this
 * one call return false. */
static bool fxm_loop_found(const fxm_file* f, uint16_t address, uint16_t j11,
                            uint16_t j22, uint16_t j33, int64_t* out_lp,
                            bool* out_error) {
  const uint8_t* d = f->data;
  *out_error = false;

  uint32_t j1 = rd16(d, address);
  uint32_t j2 = rd16(d, (uint32_t)address + 2);
  uint32_t j3 = rd16(d, (uint32_t)address + 4);
  int a1 = 1, a2 = 1, a3 = 1;
  bool f71 = false, f72 = false, f73 = false;
  bool f61 = false, f62 = false, f63 = false;
  uint16_t stack1[FXM_STEK_MAX], stack2[FXM_STEK_MAX], stack3[FXM_STEK_MAX];
  int stack1_len = 0, stack2_len = 0, stack3_len = 0;
  int64_t tr = 0;
  int64_t guard = 0;

  for (;;) {
    if (j1 == j11 && j2 == j22 && j3 == j33) {
      *out_lp = tr;
      return true;
    }

    fxm_time_step_result r1 =
        fxm_time_channel_step(d, &j1, &a1, &f71, &f61, stack1, &stack1_len,
                               NULL, true, &j1, &j2, &j3, j11, j22, j33);
    if (r1 == FXM_TIME_STEP_ERROR) {
      *out_error = true;
      return false;
    }
    if (r1 == FXM_TIME_STEP_MATCH) {
      *out_lp = tr;
      return true;
    }

    fxm_time_step_result r2 =
        fxm_time_channel_step(d, &j2, &a2, &f72, &f62, stack2, &stack2_len,
                               NULL, true, &j1, &j2, &j3, j11, j22, j33);
    if (r2 == FXM_TIME_STEP_ERROR) {
      *out_error = true;
      return false;
    }
    if (r2 == FXM_TIME_STEP_MATCH) {
      *out_lp = tr;
      return true;
    }

    fxm_time_step_result r3 =
        fxm_time_channel_step(d, &j3, &a3, &f73, &f63, stack3, &stack3_len,
                               NULL, true, &j1, &j2, &j3, j11, j22, j33);
    if (r3 == FXM_TIME_STEP_ERROR) {
      *out_error = true;
      return false;
    }
    if (r3 == FXM_TIME_STEP_MATCH) {
      *out_lp = tr;
      return true;
    }

    tr++;
    if (++guard > (1 << 20)) return false;

    bool cond1 = (f71 && (f72 || f62) && (f73 || f63)) ||
                 ((f71 || f61) && f72 && (f73 || f63)) ||
                 ((f71 || f61) && (f72 || f62) && f73);
    if (cond1) break;
  }
  return false;
}

/* Players.pas:14691-14950, GetTimeFXM's outer procedure body: the real
 * per-tick walk that accumulates Tm (one increment per iteration, same
 * "global tick" unit fxm_get_registers()'s global_tick_counter uses -
 * one call to fxm_get_registers() is one PatternInterpreter invocation
 * per channel, exactly what one iteration of this loop simulates
 * without touching AY state). Tm is capped at 180000 (Pascal's own
 * `if tm > 180000 then begin tm := 15001; break; end`) - `guard` below
 * is redundant with that existing Pascal-native cap for this loop
 * specifically, but kept anyway per this task's "blanket insurance"
 * guidance, and IS load-bearing for fxm_loop_found()'s own replay loop
 * (see its comment). On any bounds violation - here or propagated up
 * from fxm_loop_found() - out_tm/out_lp are set to 0, matching this
 * project's established convention for RaiseBadFileStructure in a
 * duration-precompute context (see pt3_get_time's own comment): the
 * file already loaded successfully by the time this runs, so a
 * duration-precompute failure degrades to "no known duration" rather
 * than failing the whole load. */
static void fxm_get_time(const fxm_file* f, uint16_t address, int64_t* out_tm,
                          int64_t* out_lp) {
  const uint8_t* d = f->data;
  int64_t tm = 0, lp = 0;

  if (address > 65536 - 6) {
    *out_tm = 0;
    *out_lp = 0;
    return;
  }

  uint32_t j1 = rd16(d, address);
  uint32_t j2 = rd16(d, (uint32_t)address + 2);
  uint32_t j3 = rd16(d, (uint32_t)address + 4);
  int a1 = 1, a2 = 1, a3 = 1;
  bool f71 = false, f72 = false, f73 = false;
  bool f61 = false, f62 = false, f63 = false;
  uint16_t stack1[FXM_STEK_MAX], stack2[FXM_STEK_MAX], stack3[FXM_STEK_MAX];
  int stack1_len = 0, stack2_len = 0, stack3_len = 0;
  uint16_t j11 = 0, j22 = 0, j33 = 0;
  int64_t guard = 0;

  for (;;) {
    fxm_time_step_result r1 =
        fxm_time_channel_step(d, &j1, &a1, &f71, &f61, stack1, &stack1_len,
                               &j11, false, &j1, &j2, &j3, 0, 0, 0);
    if (r1 == FXM_TIME_STEP_ERROR) {
      *out_tm = 0;
      *out_lp = 0;
      return;
    }

    fxm_time_step_result r2 =
        fxm_time_channel_step(d, &j2, &a2, &f72, &f62, stack2, &stack2_len,
                               &j22, false, &j1, &j2, &j3, 0, 0, 0);
    if (r2 == FXM_TIME_STEP_ERROR) {
      *out_tm = 0;
      *out_lp = 0;
      return;
    }

    fxm_time_step_result r3 =
        fxm_time_channel_step(d, &j3, &a3, &f73, &f63, stack3, &stack3_len,
                               &j33, false, &j1, &j2, &j3, 0, 0, 0);
    if (r3 == FXM_TIME_STEP_ERROR) {
      *out_tm = 0;
      *out_lp = 0;
      return;
    }

    tm++;
    if (tm > 180000) {
      tm = 15001;
      break;
    }
    if (++guard > (1 << 20)) {
      *out_tm = 0;
      *out_lp = 0;
      return;
    }

    bool cond1 = (f71 && (f72 || f62) && (f73 || f63)) ||
                 ((f71 || f61) && f72 && (f73 || f63)) ||
                 ((f71 || f61) && (f72 || f62) && f73);
    if (cond1) {
      bool err = false;
      bool found = fxm_loop_found(f, address, j11, j22, j33, &lp, &err);
      if (err) {
        *out_tm = 0;
        *out_lp = 0;
        return;
      }
      if (found) break;
    }
  }

  tm--;
  *out_tm = tm;
  *out_lp = lp;
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
    /* Players.pas: FXM_Get_Registers's own first statement, `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0108/MIG-0112) - fxm_file_step_registers is the shared
     * building block player_step_registers (player.c) also uses when
     * this format is playlist-paired as Turbosound's second voice; here
     * it targets this file's own private chip (standalone use). Force_Loop
     * (the original's TS-pair "continue playback of the shorter module"
     * case) doesn't apply - TSMode isn't ported (MIG-0007), so only
     * Do_Loop matters. */
    if (!fxm_file_step_registers(f, &f->ay.chip)) break;
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
