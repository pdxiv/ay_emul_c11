#include "ay_engine/formats/sqt_file.h"

#include <string.h>

/* Players.pas:987-997, SQT_Table. */
static const uint16_t SQT_TABLE[96] = {
    0x0D5D, 0x0C9C, 0x0BE7, 0x0B3C, 0x0A9B, 0x0A02, 0x0973, 0x08EB, 0x086B,
    0x07F2, 0x0780, 0x0714, 0x06AE, 0x064E, 0x05F4, 0x059E, 0x054F, 0x0501,
    0x04B9, 0x0475, 0x0435, 0x03F9, 0x03C0, 0x038A, 0x0357, 0x0327, 0x02FA,
    0x02CF, 0x02A7, 0x0281, 0x025D, 0x023B, 0x021B, 0x01FC, 0x01E0, 0x01C5,
    0x01AC, 0x0194, 0x017D, 0x0168, 0x0153, 0x0140, 0x012E, 0x011D, 0x010D,
    0x00FE, 0x00F0, 0x00E2, 0x00D6, 0x00CA, 0x00BE, 0x00B4, 0x00AA, 0x00A0,
    0x0097, 0x008F, 0x0087, 0x007F, 0x0078, 0x0071, 0x006B, 0x0065, 0x005F,
    0x005A, 0x0055, 0x0050, 0x004C, 0x0047, 0x0043, 0x0040, 0x003C, 0x0039,
    0x0035, 0x0032, 0x0030, 0x002D, 0x002A, 0x0028, 0x0026, 0x0024, 0x0022,
    0x0020, 0x001E, 0x001C, 0x001B, 0x0019, 0x0018, 0x0016, 0x0015, 0x0014,
    0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E};

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

static void wr16(uint8_t* d, uint32_t addr, uint16_t v) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  d[addr] = (uint8_t)(v & 0xFF);
  d[a2] = (uint8_t)(v >> 8);
}

static uint8_t rb(const uint8_t* d, uint32_t addr) { return d[addr & 0xFFFF]; }

/* --- GetTimeSQT (Players.pas:15889-16679), MIG-0101/MIG-0104 ---
 *
 * Per-channel scratch state for the duration precompute. Mirrors three
 * groups of Pascal function-scope locals (f41/j1/j11/f71, f42/j2/j22/f72,
 * f43/j3/j33/f73 - `a1/a2/a3` too) that are declared once at the top of
 * GetTimeSQT and never re-declared inside its loops.
 *
 * IMPORTANT (deliberate, verified - not a bug): `f7x` and `jxx` (j11 /
 * j22 / j33) are read in the "if a1<>0" branch WITHOUT necessarily being
 * (re)set on that exact call. This is safe because `a1/a2/a3` are reset
 * to 0 at the start of every position (see sqt_get_time's outer loop
 * body), which forces the full opcode scan (sqt_time_note_scan, the
 * "else" branch) to run at least once per position before any "a<>0"
 * repeat-branch can read f7x/jxx - and that scan always sets f7x
 * (starts False, only conditionally set True) and, whenever it reaches
 * a note opcode, jxx, before a repeat count can make a1x nonzero. This
 * mirrors the Pascal source exactly: fields below are zero-initialized
 * once before sqt_get_time's outer `while` loop, then left to persist
 * across iterations exactly like the original locals. */
typedef struct {
  bool f4; /* f41/f42/f43: high bit of this channel's position-table
            * pattern-index byte (matches playback's b4ix0). */
  bool f7; /* f71/f72/f73: "a repeat-count note has a pending effect
            * byte to re-apply" carry flag. */
  uint32_t j;  /* j1/j2/j3: this channel's opcode-stream cursor. Can
                * reach exactly 65536 (raw pattern-table word 0xFFFF,
                * +1) before the ">= 65536" guard below rejects it -
                * matches Pascal's own lazy check, done only when the
                * value is about to be used to start a scan. */
  uint32_t jj; /* j11/j22/j33: saved note-opcode start address, used to
                * re-read a trailing effect byte while a note-repeat
                * count is still counting down. */
  int a;       /* a1/a2/a3: note-repeat counter. */
} sqt_time_chan;

/* Shared re-read of a note's trailing effect byte, used both by the
 * "if a1<>0 then if f71 then ..." branch (Players.pas ~15908-15928,
 * and the equivalent for channels 2/3) and by the tail of the $80..$BF
 * opcode case inside sqt_time_note_scan (~15975-15995 and channel-2/3
 * equivalents) - all six of these Pascal blocks are the *same* walk
 * ("cptr := jxx; f6x := False; if Index[cptr] in [0..$7f] then ..."),
 * so it is factored out once here rather than duplicated six times.
 * Crucially, every one of those six sites hard-seeds f6x to False right
 * before the walk and never sets it True *within* the walk itself, so
 * the walk's "if f6x then j := cptr + 1" assignments are provably dead
 * in this specific context and are omitted below (they are NOT omitted
 * from sqt_time_note_scan's other, non-shared uses of the same
 * sub-pattern, where f6 can be True).
 *
 * Precedence note (see the porting brief's note 4): `Index[cptr - 1]
 * and 15 - 1` parses as `(Index[cptr-1] and 15) - 1` because Pascal's
 * `and` on ordinals is multiplicative-tier (binds tighter than the
 * additive `-`) - translated below as `(rb(...) & 15) - 1`, matching
 * the plain `Index[cptr-1] - 1` variant's shape ("case selector minus
 * one"), not `rb(...) & 14`.
 *
 * Memory safety: cptr only ever advances forward by a few bytes from
 * `jj`, and `jj` is always a value < 65536 by construction (see
 * sqt_time_chan's `jj` comment) - so `rb()`'s built-in `& 0xFFFF`
 * wraparound is a harmless backstop here, not a silent-corruption
 * risk, exactly like the rest of this function. */
static void sqt_time_rescan_effect(const uint8_t* d, uint32_t jj, bool f4,
                                    uint8_t* b) {
  uint32_t cptr = jj;
  uint8_t v = rb(d, cptr);
  if (v > 0x7F) return;
  cptr++;
  v = rb(d, cptr);
  if (v <= 0x7F) {
    cptr++;
    int sel = (int)rb(d, cptr - 1) - 1;
    if (f4 && (sel == 4 || sel == 5)) {
      uint8_t eff = rb(d, cptr);
      *b = (uint8_t)(sel == 4 ? (eff & 31) : ((*b + eff) & 31));
      if (*b == 0) *b = 32;
    }
  } else { /* $80..$FF */
    if (v & 64) {
      cptr++;
      v = rb(d, cptr);
      if (v & 15) {
        cptr++;
        int sel = ((int)rb(d, cptr - 1) & 15) - 1;
        if (f4 && (sel == 4 || sel == 5)) {
          uint8_t eff = rb(d, cptr);
          *b = (uint8_t)(sel == 4 ? (eff & 31) : ((*b + eff) & 31));
          if (*b == 0) *b = 32;
        }
      }
    }
  }
}

/* Full opcode-byte case dispatch starting at the channel's current
 * cursor `c->j` (Players.pas's "else" branch when a1x = 0, e.g.
 * ~15929-16006 for channel 1 and near-identically for channels 2/3 -
 * see this file's sqt_get_time comment for why all three channels
 * share this one function). Advances `c->j` to the next note's opcode
 * address, records `c->jj` at the byte address of any note opcode
 * encountered, sets `c->f7` when a repeat-count effect byte is
 * requested, and updates `*b` (the shared inter-channel speed/delay
 * byte) on case selector 4/5 exactly like sqt_time_rescan_effect.
 *
 * The Pascal `repeat...until False` this is ported from always executes
 * exactly one iteration in practice: every case arm ends in `break`
 * (0..$5F, $60..$6E, $6F..$7F both sub-branches, $80..$BF, $C0..$FF all
 * reach a `break` after at most a couple of `Incr(cptr)` steps), so it
 * is translated directly as a single dispatch (early `return`s below)
 * rather than as a C loop. */
static void sqt_time_note_scan(const uint8_t* d, sqt_time_chan* c,
                                uint8_t* b) {
  uint32_t cptr = c->j;
  bool f6 = true;
  c->f7 = false;
  uint8_t op = rb(d, cptr);

  if (op <= 0x5F) {
    c->jj = cptr;
    cptr++;
    uint8_t v = rb(d, cptr);
    if (v <= 0x7F) {
      cptr++;
      if (f6) {
        c->j = cptr + 1;
        f6 = false;
      }
      int sel = (int)rb(d, cptr - 1) - 1;
      if (c->f4 && (sel == 4 || sel == 5)) {
        uint8_t eff = rb(d, cptr);
        *b = (uint8_t)(sel == 4 ? (eff & 31) : ((*b + eff) & 31));
        if (*b == 0) *b = 32;
      }
    } else { /* $80..$FF */
      if (v & 64) {
        cptr++;
        v = rb(d, cptr);
        if (v & 15) {
          cptr++;
          if (f6) {
            c->j = cptr + 1;
            f6 = false;
          }
          int sel = ((int)rb(d, cptr - 1) & 15) - 1;
          if (c->f4 && (sel == 4 || sel == 5)) {
            uint8_t eff = rb(d, cptr);
            *b = (uint8_t)(sel == 4 ? (eff & 31) : ((*b + eff) & 31));
            if (*b == 0) *b = 32;
          }
        }
      }
    }
    cptr++;
    if (f6) c->j = cptr;
    return;
  }

  if (op <= 0x6E) {
    cptr++;
    if (f6) c->j = cptr + 1;
    int sel = (int)rb(d, cptr - 1) - 0x60 - 1;
    if (c->f4 && (sel == 4 || sel == 5)) {
      uint8_t eff = rb(d, cptr);
      *b = (uint8_t)(sel == 4 ? (eff & 31) : ((*b + eff) & 31));
      if (*b == 0) *b = 32;
    }
    return;
  }

  if (op <= 0x7F) {
    if (op != 0x6F) {
      cptr++;
      if (f6) c->j = cptr + 1;
      int sel = (int)rb(d, cptr - 1) - 0x6F - 1;
      if (c->f4 && (sel == 4 || sel == 5)) {
        uint8_t eff = rb(d, cptr);
        *b = (uint8_t)(sel == 4 ? (eff & 31) : ((*b + eff) & 31));
        if (*b == 0) *b = 32;
      }
    } else {
      c->j = cptr + 1;
    }
    return;
  }

  if (op <= 0xBF) {
    c->j = cptr + 1;
    /* Channel 1's Pascal source spells this `Index[cptr] in
     * [$a0..$bf]`; channels 2/3 spell the same condition
     * `not (Index[cptr] in [$80..$9f])`. Both mean "op >= 0xA0" here,
     * since op is already known to be in $80..$BF at this point -
     * verified equivalent, not assumed, before sharing this function
     * across all three channels. */
    if (op >= 0xA0) {
      c->a = op & 15;
      if ((op & 16) == 0) return; /* break: no effect-byte re-read */
      if (c->a != 0) c->f7 = true;
    }
    sqt_time_rescan_effect(d, c->jj, c->f4, b);
    return;
  }

  /* $C0..$FF */
  c->j = cptr + 1;
  c->jj = cptr;
}

/* One "for i := 1 to Index[j1-1] do" loop iteration for a single
 * channel: either the "if a1<>0" repeat-count branch (Dec(a1); if f71
 * then re-scan the trailing effect byte) or, when not repeating, the
 * full opcode scan - with the same `if j1 >= 65536 then
 * RaiseBadFileStructure` guard the Pascal source has immediately before
 * entering that scan. Returns false to signal "bail out, duration =
 * 0", mirroring RaiseBadFileStructure. */
static bool sqt_time_process_note(const uint8_t* d, sqt_time_chan* c,
                                   uint8_t* b) {
  if (c->a != 0) {
    c->a--;
    if (c->f7) sqt_time_rescan_effect(d, c->jj, c->f4, b);
    return true;
  }
  if (c->j >= 65536) return false; /* Players.pas: RaiseBadFileStructure */
  sqt_time_note_scan(d, c, b);
  return true;
}

/* Players.pas:15889-16679, GetTimeSQT. Walks the position/pattern data
 * (not the CPU) to compute a real duration in player ticks (Tm) and a
 * loop-point tick (Lp), matching the real Pascal IntegrityCheck/GUI
 * duration precompute. Tm = 0 means the file is structurally broken
 * enough that Pascal itself would reject it (RaiseBadFileStructure).
 *
 * Safety: the C `data` buffer is `[65536]` (indices 0..65535), one byte
 * smaller than Pascal's `array[0..65536] of byte` (65537 bytes, which
 * tolerates reading exactly at offset 65536 harmlessly) - so offset
 * 65536 is treated as invalid here too (bailing to Tm=0), even at the
 * handful of spots the Pascal source itself doesn't explicitly guard.
 * All byte/word reads go through rb()/rd16(), which additionally mask
 * addresses to 0..65535 as a memory-safety backstop, so no read in this
 * function (or the helpers above) can ever go out of bounds regardless
 * of how malformed the input is. */
static void sqt_get_time(const sqt_file* f, int64_t* out_tm,
                          int64_t* out_lp) {
  const uint8_t* d = f->data;
  int64_t tm = 0, lp = 0;
  uint32_t pptr = f->positions_pointer; /* SQT_PositionsPointer */
  /* Zero-initialized once, before the outer loop starts, then left to
   * persist across positions exactly like GetTimeSQT's own function-
   * scope locals - see sqt_time_chan's comment. */
  sqt_time_chan c1 = {0}, c2 = {0}, c3 = {0};
  long iterations = 0; /* defensive cap beyond the literal Pascal, like
                        * this project's other GetTimeXXX ports. */

  for (;;) {
    uint8_t pos_op;
    uint16_t pat;
    uint8_t note_count, b;
    int i;

    if (++iterations > (1 << 20)) goto fail;
    if (pptr >= 65536) goto fail; /* offset-65536 guard, see comment above */

    pos_op = rb(d, pptr);
    if (pos_op == 0) break; /* while Index[pptr] <> 0 */

    if (pptr == f->loop_pointer) lp = tm; /* SQT_LoopPointer */

    /* Channel 1. */
    c1.f4 = (pos_op & 0x80) != 0;
    pat = rd16(d, (uint32_t)f->patterns_pointer +
                       (uint32_t)(uint8_t)(pos_op * 2)); /* note 3: byte()
                                                          * truncates the
                                                          * *2 to 8 bits
                                                          * before adding
                                                          * the base. */
    c1.j = (uint32_t)pat + 1;
    pptr += 2;
    if (pptr >= 65536) goto fail; /* Players.pas: RaiseBadFileStructure */

    /* Channel 2. */
    pos_op = rb(d, pptr);
    c2.f4 = (pos_op & 0x80) != 0;
    pat = rd16(d, (uint32_t)f->patterns_pointer +
                       (uint32_t)(uint8_t)(pos_op * 2));
    c2.j = (uint32_t)pat + 1;
    pptr += 2;
    if (pptr >= 65536) goto fail;

    /* Channel 3. */
    pos_op = rb(d, pptr);
    c3.f4 = (pos_op & 0x80) != 0;
    pat = rd16(d, (uint32_t)f->patterns_pointer +
                       (uint32_t)(uint8_t)(pos_op * 2));
    c3.j = (uint32_t)pat + 1;
    pptr += 2;
    if (pptr >= 65536) goto fail;

    b = rb(d, pptr);
    pptr++;

    /* `for i := 1 to Index[j1-1] do` - bound read once. j1-1 (the raw,
     * pre-Incr pattern-table word) is always a plain uint16_t address
     * (c1.j >= 1 by construction), so no extra bounds check is needed
     * beyond the ones already taken above. */
    note_count = rb(d, c1.j - 1);

    for (i = 0; i < (int)note_count; i++) {
      if (!sqt_time_process_note(d, &c1, &b)) goto fail;
      if (!sqt_time_process_note(d, &c2, &b)) goto fail;
      if (!sqt_time_process_note(d, &c3, &b)) goto fail;
      tm += b;
      if (++iterations > (1 << 20)) goto fail;
    }
  }

  *out_tm = tm;
  *out_lp = lp;
  return;

fail:
  *out_tm = 0;
  *out_lp = 0;
}

/* ModTypes variant 11 (Players.pas:177-178): SQT_Size@0 (word, unused)
 * SQT_SamplesPointer@2 SQT_OrnamentsPointer@4 SQT_PatternsPointer@6
 * SQT_PositionsPointer@8 SQT_LoopPointer@10 (all word). */
sqt_file_status sqt_file_load(sqt_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  (void)sample_rate;

  memset(f, 0, sizeof(*f));

  if (size < 12) return SQT_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);
  int mlen = (int)size;

  uint16_t samples_ptr_raw = rd16(f->data, 2);
  uint16_t patterns_ptr_raw = rd16(f->data, 6);
  uint16_t positions_ptr_raw = rd16(f->data, 8);

  /* Players.pas:2393-2423: heuristic base-address detection + bulk word
   * relocation - see this file's header comment for the mechanics. */
  int base = (int)samples_ptr_raw - 10;
  if (base < 0) return SQT_FILE_ERR_BAD_HEADER;
  int i1 = 0;
  int i2 = (int)positions_ptr_raw - base;
  if (i2 < 0) return SQT_FILE_ERR_BAD_HEADER;
  while (rb(f->data, (uint32_t)i2) != 0) {
    int v;
    if (i2 > mlen - 8) return SQT_FILE_ERR_BAD_HEADER;
    v = rb(f->data, (uint32_t)i2) & 0x7F;
    if (i1 < v) i1 = v;
    i2 += 2;
    v = rb(f->data, (uint32_t)i2) & 0x7F;
    if (i1 < v) i1 = v;
    i2 += 2;
    v = rb(f->data, (uint32_t)i2) & 0x7F;
    if (i1 < v) i1 = v;
    i2 += 3;
  }
  int words_to_fix = ((int)patterns_ptr_raw - base + i1 * 2) / 2;
  if (words_to_fix < 1 || words_to_fix >= (65536 - 2) / 2)
    return SQT_FILE_ERR_BAD_HEADER;

  {
    uint32_t woff = 2;
    int j;
    for (j = 0; j < words_to_fix; j++) {
      wr16(f->data, woff, (uint16_t)(rd16(f->data, woff) - base));
      woff += 2;
    }
  }

  f->samples_pointer = rd16(f->data, 2);
  f->ornaments_pointer = rd16(f->data, 4);
  f->patterns_pointer = rd16(f->data, 6);
  f->positions_pointer = rd16(f->data, 8);
  f->loop_pointer = rd16(f->data, 10);

  ay_engine_init(&f->ay);
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * SQT_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3809-3842, InitTrackerModule's FT.SQT branch. */
  f->delay_counter = 1;
  f->delay = 1;
  f->lines_counter = 1;
  f->positions_pointer = rd16(f->data, 8); /* SQT_PositionsPointer, post-relocation */

  f->global_tick_counter = 0;

  f->global_tick_max = 0;
  f->loop_tick = 0;
  f->do_loop = false;
  f->real_end_all = false;
  sqt_get_time(f, &f->global_tick_max, &f->loop_tick); /* MIG-0104 */

  return SQT_FILE_OK;
}

/* Players.pas:10434-10495, Call_LC1D1. */
static void call_lc1d1(sqt_file* f, ay_chip* chip, sqt_channel* chan,
                        uint16_t* ptr, uint8_t a) {
  (*ptr)++;
  if (chan->b6ix0) {
    chan->address_in_pattern = (uint16_t)(*ptr + 1);
    chan->b6ix0 = false;
  }
  uint8_t case_val = (uint8_t)(a - 1);
  if (case_val == 0) {
    if (chan->b4ix0) chan->volume = (uint8_t)(rb(f->data, *ptr) & 15);
  } else if (case_val == 1) {
    if (chan->b4ix0)
      chan->volume = (uint8_t)((chan->volume + rb(f->data, *ptr)) & 15);
  } else if (case_val == 2) {
    if (chan->b4ix0) {
      uint8_t v = rb(f->data, *ptr);
      f->chan_a.volume = v;
      f->chan_b.volume = v;
      f->chan_c.volume = v;
    }
  } else if (case_val == 3) {
    if (chan->b4ix0) {
      uint8_t v = rb(f->data, *ptr);
      f->chan_a.volume = (uint8_t)((f->chan_a.volume + v) & 15);
      f->chan_b.volume = (uint8_t)((f->chan_b.volume + v) & 15);
      f->chan_c.volume = (uint8_t)((f->chan_c.volume + v) & 15);
    }
  } else if (case_val == 4) {
    if (chan->b4ix0) {
      uint8_t v = (uint8_t)(rb(f->data, *ptr) & 31);
      if (v == 0) v = 32;
      f->delay_counter = v;
      f->delay = v;
    }
  } else if (case_val == 5) {
    if (chan->b4ix0) {
      uint8_t v = (uint8_t)((f->delay_counter + rb(f->data, *ptr)) & 31);
      if (v == 0) v = 32;
      f->delay_counter = v;
      f->delay = v;
    }
  } else if (case_val == 6) {
    chan->current_ton_sliding = 0;
    chan->gliss = true;
    chan->ton_slide_step = (int16_t)(-(int)rb(f->data, *ptr));
  } else if (case_val == 7) {
    chan->current_ton_sliding = 0;
    chan->gliss = true;
    chan->ton_slide_step = rb(f->data, *ptr);
  } else {
    chan->envelope_enabled = true;
    ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(case_val & 15));
    chip->reg[11] = rb(f->data, *ptr);
  }
}

/* Players.pas:10497-10512, Call_LC2A8. */
static void call_lc2a8(sqt_file* f, sqt_channel* chan, uint8_t a) {
  chan->envelope_enabled = false;
  chan->ornament_enabled = false;
  chan->gliss = false;
  chan->enabled = true;
  chan->sample_pointer = rd16(f->data, (uint32_t)a * 2 + f->samples_pointer);
  chan->point_in_sample = (uint16_t)(chan->sample_pointer + 2);
  chan->sample_tik_counter = 32;
  chan->mix_noise = true;
  chan->mix_ton = true;
}

/* Players.pas:10514-10524, Call_LC2D9. */
static void call_lc2d9(sqt_file* f, sqt_channel* chan, uint8_t a) {
  chan->ornament_pointer = rd16(f->data, (uint32_t)a * 2 + f->ornaments_pointer);
  chan->point_in_ornament = (uint16_t)(chan->ornament_pointer + 2);
  chan->ornament_tik_counter = 32;
  chan->ornament_enabled = true;
}

/* Players.pas:10526-10548, Call_LC283. */
static void call_lc283(sqt_file* f, ay_chip* chip, sqt_channel* chan,
                        uint16_t* ptr) {
  uint8_t op = rb(f->data, *ptr);
  if (op <= 0x7F) {
    call_lc1d1(f, chip, chan, ptr, op);
  } else {
    if (((op >> 1) & 31) != 0) call_lc2a8(f, chan, (uint8_t)((op >> 1) & 31));
    if (op & 64) {
      int temp = rb(f->data, (uint32_t)*ptr + 1) >> 4;
      if (op & 1) temp |= 16;
      if (temp != 0) call_lc2d9(f, chan, (uint8_t)temp);
      (*ptr)++;
      if (rb(f->data, *ptr) & 15)
        call_lc1d1(f, chip, chan, ptr, (uint8_t)(rb(f->data, *ptr) & 15));
    }
  }
  (*ptr)++;
}

/* Players.pas:10550-10567, Call_LC191. */
static void call_lc191(sqt_file* f, ay_chip* chip, sqt_channel* chan) {
  uint16_t ptr = chan->ix27;
  chan->b6ix0 = false;
  uint8_t op = rb(f->data, ptr);
  if (op <= 0x7F) {
    ptr++;
    call_lc283(f, chip, chan, &ptr);
  } else {
    call_lc2a8(f, chan, (uint8_t)(op & 31));
  }
}

/* Players.pas:10429-10638, PatternInterpreter. Uses real C `break`
 * statements mirroring Pascal's own `break` 1:1 (unlike this project's
 * other tracker ports, which flatten into if/elseif chains with a quit
 * flag) because this format's break placement is genuinely irregular -
 * one sub-branch inside the $80-$BF opcode conditionally breaks BEFORE
 * reaching code that every other path in that same opcode falls through
 * to (Call_LC191), so a flattened quit-flag translation would risk
 * silently losing that early-exit. */
static void pattern_interpreter(sqt_file* f, ay_chip* chip, sqt_channel* chan) {
  if (chan->ix21 != 0) {
    chan->ix21--;
    if (chan->b7ix0) call_lc191(f, chip, chan);
    return;
  }

  uint16_t ptr = chan->address_in_pattern;
  chan->b6ix0 = true;
  chan->b7ix0 = false;

  for (;;) {
    uint8_t op = rb(f->data, ptr);
    if (op <= 0x5F) {
      chan->note = op;
      chan->ix27 = ptr;
      ptr = (uint16_t)(ptr + 1);
      call_lc283(f, chip, chan, &ptr);
      if (chan->b6ix0) chan->address_in_pattern = ptr;
      break;
    } else if (op <= 0x6E) {
      call_lc1d1(f, chip, chan, &ptr, (uint8_t)(op - 0x60));
      break;
    } else if (op <= 0x7F) {
      chan->mix_noise = false;
      chan->mix_ton = false;
      chan->enabled = false;
      if (op != 0x6F) {
        call_lc1d1(f, chip, chan, &ptr, (uint8_t)(op - 0x6F));
      } else {
        chan->address_in_pattern = (uint16_t)(ptr + 1);
      }
      break;
    } else if (op <= 0xBF) {
      chan->address_in_pattern = (uint16_t)(ptr + 1);
      if (op <= 0x9F) {
        if ((op & 16) == 0)
          chan->note = (uint8_t)(chan->note + (op & 15));
        else
          chan->note = (uint8_t)(chan->note - (op & 15));
      } else {
        chan->ix21 = (uint8_t)(op & 15);
        if ((op & 16) == 0) break;
        if (chan->ix21 != 0) chan->b7ix0 = true;
      }
      call_lc191(f, chip, chan);
      break;
    } else { /* $C0..$FF */
      chan->address_in_pattern = (uint16_t)(ptr + 1);
      chan->ix27 = ptr;
      call_lc2a8(f, chan, (uint8_t)(op & 31));
      break;
    }
  }
}

/* Players.pas:10640-10721, GetRegisters. */
static void get_registers(sqt_file* f, ay_chip* chip, sqt_channel* chan,
                           uint8_t* temp_mixer) {
  *temp_mixer = (uint8_t)(*temp_mixer << 1);
  if (!chan->enabled) {
    chan->amplitude = 0;
    return;
  }

  uint8_t b0 = rb(f->data, chan->point_in_sample);
  chan->amplitude = (uint8_t)(b0 & 15);
  if (chan->amplitude != 0) {
    chan->amplitude = (uint8_t)(chan->amplitude - chan->volume);
    if ((int8_t)chan->amplitude < 0) chan->amplitude = 0;
  } else if (chan->envelope_enabled) {
    chan->amplitude = 16;
  }

  uint8_t b1 = rb(f->data, (uint32_t)chan->point_in_sample + 1);
  if (b1 & 32) {
    *temp_mixer |= 8;
    chip->reg[6] = (uint8_t)((b0 & 0xF0) >> 3);
    if ((int8_t)b1 < 0) chip->reg[6]++;
  }
  if (b1 & 64) *temp_mixer |= 1;

  uint8_t j = chan->note;
  if (chan->ornament_enabled) {
    j = (uint8_t)(j + rb(f->data, chan->point_in_ornament));
    chan->ornament_tik_counter--;
    if (chan->ornament_tik_counter == 0) {
      if (rb(f->data, chan->ornament_pointer) != 32) {
        chan->ornament_tik_counter =
            (int8_t)rb(f->data, (uint32_t)chan->ornament_pointer + 1);
        chan->point_in_ornament = (uint16_t)(
            chan->ornament_pointer + 2 + rb(f->data, chan->ornament_pointer));
      } else {
        chan->ornament_tik_counter =
            (int8_t)rb(f->data, (uint32_t)chan->sample_pointer + 1);
        chan->point_in_ornament = (uint16_t)(
            chan->ornament_pointer + 2 + rb(f->data, chan->sample_pointer));
      }
    } else {
      chan->point_in_ornament = (uint16_t)(chan->point_in_ornament + 1);
    }
  }
  j = (uint8_t)(j + chan->transposit);
  if (j > 0x5F) j = 0x5F;

  int ton;
  int delta = ((int)(b1 & 15) << 8) + rb(f->data, (uint32_t)chan->point_in_sample + 2);
  if ((b1 & 16) == 0)
    ton = SQT_TABLE[j] - delta;
  else
    ton = SQT_TABLE[j] + delta;

  chan->sample_tik_counter--;
  if (chan->sample_tik_counter == 0) {
    chan->sample_tik_counter = (int8_t)rb(f->data, (uint32_t)chan->sample_pointer + 1);
    if (rb(f->data, chan->sample_pointer) == 32) {
      chan->enabled = false;
      chan->ornament_enabled = false;
    }
    chan->point_in_sample = (uint16_t)(
        chan->sample_pointer + 2 + (uint32_t)rb(f->data, chan->sample_pointer) * 3);
  } else {
    chan->point_in_sample = (uint16_t)(chan->point_in_sample + 3);
  }

  if (chan->gliss) {
    ton += chan->current_ton_sliding;
    chan->current_ton_sliding = (int16_t)(chan->current_ton_sliding + chan->ton_slide_step);
  }
  chan->ton = (uint16_t)(ton & 0xFFF);
}

/* Players.pas:10425-10843, SQT_Get_Registers. MIG-0108: the
 * CheckLoopAndStop-equivalent check now lives in sqt_file_make_buffer's
 * tick loop instead of here (see its own comment) - functionally
 * equivalent since nothing else touches global_tick_counter in
 * between. Channel processing order is C, B, A (like PSM, unlike every
 * other format ported so far, which goes A, B, C). */
static void sqt_get_registers(sqt_file* f, ay_chip* chip) {
  uint8_t temp_mixer;

  f->delay_counter--;
  if (f->delay_counter == 0) {
    f->delay_counter = f->delay;
    f->lines_counter--;
    if (f->lines_counter == 0) {
      if (rb(f->data, f->positions_pointer) == 0)
        f->positions_pointer = f->loop_pointer;

      f->chan_c.b4ix0 = ((int8_t)rb(f->data, f->positions_pointer) < 0);
      {
        uint8_t pat = (uint8_t)(rb(f->data, f->positions_pointer) * 2);
        f->chan_c.address_in_pattern = rd16(f->data, f->patterns_pointer + pat);
      }
      f->lines_counter = rb(f->data, f->chan_c.address_in_pattern);
      f->chan_c.address_in_pattern = (uint16_t)(f->chan_c.address_in_pattern + 1);
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_c.volume = (uint8_t)(rb(f->data, f->positions_pointer) & 15);
      {
        uint8_t hi = (uint8_t)(rb(f->data, f->positions_pointer) >> 4);
        if (hi < 9)
          f->chan_c.transposit = (int8_t)hi;
        else
          f->chan_c.transposit = (int8_t)(-(int)(hi - 9) - 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_c.ix21 = 0;

      if (rb(f->data, f->positions_pointer) == 0)
        f->positions_pointer = f->loop_pointer;
      f->chan_b.b4ix0 = ((int8_t)rb(f->data, f->positions_pointer) < 0);
      {
        uint8_t pat = (uint8_t)(rb(f->data, f->positions_pointer) * 2);
        f->chan_b.address_in_pattern =
            (uint16_t)(rd16(f->data, f->patterns_pointer + pat) + 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_b.volume = (uint8_t)(rb(f->data, f->positions_pointer) & 15);
      {
        uint8_t hi = (uint8_t)(rb(f->data, f->positions_pointer) >> 4);
        if (hi < 9)
          f->chan_b.transposit = (int8_t)hi;
        else
          f->chan_b.transposit = (int8_t)(-(int)(hi - 9) - 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_b.ix21 = 0;

      if (rb(f->data, f->positions_pointer) == 0)
        f->positions_pointer = f->loop_pointer;
      f->chan_a.b4ix0 = ((int8_t)rb(f->data, f->positions_pointer) < 0);
      {
        uint8_t pat = (uint8_t)(rb(f->data, f->positions_pointer) * 2);
        f->chan_a.address_in_pattern =
            (uint16_t)(rd16(f->data, f->patterns_pointer + pat) + 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_a.volume = (uint8_t)(rb(f->data, f->positions_pointer) & 15);
      {
        uint8_t hi = (uint8_t)(rb(f->data, f->positions_pointer) >> 4);
        if (hi < 9)
          f->chan_a.transposit = (int8_t)hi;
        else
          f->chan_a.transposit = (int8_t)(-(int)(hi - 9) - 1);
      }
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
      f->chan_a.ix21 = 0;

      f->delay = rb(f->data, f->positions_pointer);
      f->delay_counter = f->delay;
      f->positions_pointer = (uint16_t)(f->positions_pointer + 1);
    }
    pattern_interpreter(f, chip, &f->chan_c);
    pattern_interpreter(f, chip, &f->chan_b);
    pattern_interpreter(f, chip, &f->chan_a);
  }

  temp_mixer = 0;
  get_registers(f, chip, &f->chan_c, &temp_mixer);
  get_registers(f, chip, &f->chan_b, &temp_mixer);
  get_registers(f, chip, &f->chan_a, &temp_mixer);
  temp_mixer = (uint8_t)((-(int)(temp_mixer + 1)) & 0x3F);

  if (!f->chan_a.mix_noise) temp_mixer = (uint8_t)(temp_mixer | 8);
  if (!f->chan_a.mix_ton) temp_mixer = (uint8_t)(temp_mixer | 1);
  if (!f->chan_b.mix_noise) temp_mixer = (uint8_t)(temp_mixer | 16);
  if (!f->chan_b.mix_ton) temp_mixer = (uint8_t)(temp_mixer | 2);
  if (!f->chan_c.mix_noise) temp_mixer = (uint8_t)(temp_mixer | 32);
  if (!f->chan_c.mix_ton) temp_mixer = (uint8_t)(temp_mixer | 4);

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

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) + one sqt_get_registers
 * call - the reusable "advance one interrupt frame's worth of registers
 * into `chip`" building block player_step_registers (player.c, MIG-0112)
 * needs for playlist-level Turbosound pairing. */
bool sqt_file_step_registers(sqt_file* f, ay_chip* chip) {
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
  sqt_get_registers(f, chip);
  return true;
}

int sqt_file_make_buffer(sqt_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(SQT_FILE_AY_FREQ_DEF /
                    (SQT_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
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
    /* Players.pas: SQT_Get_Registers's own first statement, `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0108/MIG-0112) - sqt_file_step_registers is the shared
     * building block player_step_registers (player.c) also uses when
     * this format is playlist-paired as Turbosound's second voice; here
     * it targets this file's own private chip (standalone use). */
    if (!sqt_file_step_registers(f, &f->ay.chip)) break;
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
