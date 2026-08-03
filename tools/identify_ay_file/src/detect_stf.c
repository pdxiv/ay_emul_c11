/* Depacker + structural checks for STF. See detect_stf.h.
 *
 * Equivalence note: Players.pas's FoundSTF calls STFDepackBytes
 * incrementally with a growing target size (`posu`), validating each
 * structural field as soon as it has enough freshly-depacked bytes. Since
 * STFDepackBytes is a pure resumable state machine (pos1/pos2 carried
 * between calls) that produces byte-for-byte identical output whether
 * called once for N bytes or many times for growing sub-targets, this file
 * instead depacks the WHOLE candidate window in a single pass up to
 * STF_MAX_SIZE (or until the stream's own end-of-data marker fires, or an
 * error occurs) and then re-runs every checkpoint Pascal's incremental
 * calls would have made, each guarded by "were at least this many bytes
 * successfully produced" - which is exactly the failure Pascal's own
 * per-checkpoint depack call would have hit had the stream ended or erred
 * before that checkpoint's target. */
#include "identify/detect_stf.h"

#include <string.h>

#define STF_PAT_SIZE 576          /* Players.pas: STF_PatSize = 9*64 */
#define STF_PRE_PATS_SIZE 0xBF9   /* Players.pas: STF_PrePatsSize */
#define STF_MAX_SIZE (STF_PRE_PATS_SIZE + STF_PAT_SIZE * 32)

/* Depacked-buffer field offsets, ModTypes variant 15 (Players.pas:
 * 202-210): Samples[1..15]@0 (130B each: Vl[32] NTENs[32] Tn[32]*2 LpB LpS)
 * Positions[0..255]@1950 (2B each) PosLen@2462 Ornaments[0..16]@2463
 * (32B each: LpB LpE Vals[30]) Delay@3007 PatLens[1..31]@3008 LoopPos@3039
 * Title[1..25]@3040 Patterns[1..31]@3065(=STF_PRE_PATS_SIZE). */
#define STF_SMP_OFF(i) ((size_t)((i) - 1) * 130)
#define STF_POS_OFF(i) (1950 + (size_t)(i) * 2)
#define STF_POSLEN_OFF 2462
#define STF_ORN_OFF(i) (2463 + (size_t)(i) * 32)
#define STF_DELAY_OFF 3007
#define STF_PATLEN_OFF(i) (3008 + (size_t)((i) - 1))
#define STF_LOOPPOS_OFF 3039
#define STF_TITLE_OFF(i) (3040 + (size_t)((i) - 1))

typedef struct {
  const uint8_t* buf1; /* compressed input */
  size_t psize;
  size_t pos1;
  uint8_t buf2[STF_MAX_SIZE]; /* depacked output */
  size_t pos2;
  bool ended;
  bool ok;
} stf_depack_state;

static int stf_get_byte(stf_depack_state* s) {
  if (s->pos1 >= s->psize) return -1;
  return s->buf1[s->pos1++];
}

static bool stf_fill(stf_depack_state* s, int c, uint8_t b) {
  if (s->pos2 + (size_t)c > STF_MAX_SIZE) return false;
  memset(s->buf2 + s->pos2, b, (size_t)c);
  s->pos2 += (size_t)c;
  return true;
}

/* Players.pas:1222-1240 MoveBufBytes - LZ77-style back-reference copy,
 * done byte-by-byte (not memmove) since source and destination ranges may
 * overlap when replicating a short repeating pattern. */
static bool stf_move_back(stf_depack_state* s, int c, uint8_t ofs_h) {
  if (s->pos2 + (size_t)c > STF_MAX_SIZE) return false;
  int lo = stf_get_byte(s);
  if (lo < 0) return false;
  long from = (long)s->pos2 - ((long)ofs_h << 8) - lo;
  if (from < 0) return false;
  for (int k = 0; k < c; k++) {
    s->buf2[s->pos2] = s->buf2[(size_t)from];
    s->pos2++;
    from++;
  }
  return true;
}

static bool stf_move_literal(stf_depack_state* s, int c) {
  if (s->pos2 + (size_t)c > STF_MAX_SIZE) return false;
  if (s->pos1 + (size_t)c > s->psize) return false;
  memcpy(s->buf2 + s->pos2, s->buf1 + s->pos1, (size_t)c);
  s->pos2 += (size_t)c;
  s->pos1 += (size_t)c;
  return true;
}

/* Players.pas:1196-1329 STFDepackBytes, decoded in one continuous pass up
 * to STF_MAX_SIZE instead of Pascal's incremental per-checkpoint calls -
 * see this file's top comment for why that is equivalent. */
static void stf_depack_all(stf_depack_state* s) {
  s->ok = true;
  s->ended = false;
  while (s->pos2 < STF_MAX_SIZE) {
    int a = stf_get_byte(s);
    if (a < 0) {
      s->ok = false;
      return;
    }
    bool step_ok;
    if (a & 1) {
      if (a & 2) {
        if (a & 4) {
          step_ok = stf_fill(s, 2, (uint8_t)((a >> 3) - 1));
        } else {
          /* end-of-data marker (Players.pas:1275-1282). */
          step_ok = stf_fill(s, 1, s->buf1[0]);
          if (step_ok) s->ended = true;
          s->ok = step_ok;
          return;
        }
      } else {
        step_ok = stf_move_back(s, (a >> 5) + 3, (uint8_t)((a >> 2) & 7));
      }
    } else {
      if (a & 2) {
        if (a & 4) {
          step_ok = stf_move_literal(s, (a >> 3) + 1);
        } else {
          int b = stf_get_byte(s);
          if (b < 0) {
            s->ok = false;
            return;
          }
          step_ok = stf_move_back(s, b + 3, (uint8_t)(a >> 3));
        }
      } else {
        int b = stf_get_byte(s);
        if (b < 0) {
          s->ok = false;
          return;
        }
        if (a & 4) {
          step_ok = stf_fill(s, (a >> 3) + 3, (uint8_t)b);
        } else {
          int c = ((a & 0xF8) << 5) + b + 3;
          int b2 = stf_get_byte(s);
          if (b2 < 0) {
            s->ok = false;
            return;
          }
          step_ok = stf_fill(s, c, (uint8_t)b2);
        }
      }
    }
    if (!step_ok) {
      s->ok = false;
      return;
    }
  }
}

/* Players.pas: STF_AllowedChars. */
static bool stf_allowed_char(uint8_t c) {
  if (c >= 0x20 && c <= 0x5A) return true;
  if (c >= 0x5E && c <= 0x7A) return true;
  if (c == 0xAC || c == 0xC3) return true;
  if (c >= 0xC5 && c <= 0xC9) return true;
  if (c >= 0xCB && c <= 0xCD) return true;
  if (c == 0xE2) return true;
  return false;
}

/* Players.pas:5408-5553 FoundSTF, minus the final IntegrityCheck step. */
bool detect_stf_structural(const filebuf* f, detection* d) {
  if (f->size < 5) return false;
  if (!in_bounds(f, 0, 2)) return false;
  if ((le16_at(f, 0) & 0xFFF0) == 0) return false; /* Players.pas:5420 */

  stf_depack_state s;
  s.buf1 = f->data;
  s.psize = f->size;
  s.pos1 = 1;
  s.pos2 = 0;
  stf_depack_all(&s);
  if (!s.ok) return false;
  size_t produced = s.pos2;

#define NEED(off) do { if (produced < (off)) return false; } while (0)

  for (int i = 1; i <= 15; i++) {
    size_t base = STF_SMP_OFF(i);
    NEED(base + 32);
    for (int j = 0; j < 32; j++)
      if (s.buf2[base + (size_t)j] > 15) return false; /* Vl */
    NEED(base + 130);
    uint8_t lp_b = s.buf2[base + 128];
    if (lp_b > 32) return false;
    uint8_t lp_s = s.buf2[base + 129];
    if (lp_b > 0 && (int)lp_b + lp_s > 32) return false;
    for (int j = 0; j < 32; j++) {
      uint16_t tn = (uint16_t)(s.buf2[base + 64 + (size_t)j * 2] |
                                (uint16_t)s.buf2[base + 64 + (size_t)j * 2 + 1] << 8);
      if (tn & 0xE000) return false;
    }
  }

  for (int i = 0; i <= 255; i++) {
    NEED(STF_POS_OFF(i) + 2);
    uint8_t pat = s.buf2[STF_POS_OFF(i)];
    if (pat < 1 || pat > 31) return false;
  }

  NEED(STF_ORN_OFF(0) + 32);
  NEED(STF_POSLEN_OFF + 1); /* PosLen, part of the same "1+32" checkpoint */
  {
    size_t base = STF_ORN_OFF(0);
    if (s.buf2[base] != 0) return false; /* LpB */
    uint8_t lp_e = s.buf2[base + 1];
    if (lp_e != 0 && lp_e != 29) return false;
    for (int j = 0; j < 30; j++)
      if (s.buf2[base + 2 + (size_t)j] != 0) return false;
  }

  for (int i = 1; i <= 15; i++) {
    size_t base = STF_ORN_OFF(i);
    NEED(base + 32);
    uint8_t lp_b = s.buf2[base];
    uint8_t lp_e = s.buf2[base + 1];
    if (lp_e > 29 || lp_b > lp_e) return false;
    for (int j = 0; j < 30; j++) {
      int8_t v = (int8_t)s.buf2[base + 2 + (size_t)j];
      if (v < -64 || v > 63) return false;
    }
  }

  NEED(STF_DELAY_OFF + 1); /* ornament[16] (unchecked) + Delay */
  uint8_t delay = s.buf2[STF_DELAY_OFF];
  if (delay < 3 || delay > 15) return false;

  NEED(STF_PATLEN_OFF(31) + 1);
  for (int i = 1; i <= 31; i++) {
    uint8_t v = s.buf2[STF_PATLEN_OFF(i)];
    if (v < 5 || v > 64) return false;
  }
  uint8_t pos_len = s.buf2[STF_POSLEN_OFF];
  uint8_t loop_pos = s.buf2[STF_LOOPPOS_OFF];
  if (loop_pos > pos_len) return false;

  NEED(STF_TITLE_OFF(25) + 1);
  for (int i = 1; i <= 25; i++)
    if (!stf_allowed_char(s.buf2[STF_TITLE_OFF(i)])) return false;

  /* Players.pas:5532-5533: depacking must run to completion (the stream's
   * own end-of-data marker must fire) - see this file's top comment. */
  if (!s.ended) return false;

  d->format = "STF";
  d->confidence = "probable";
  d->chips = 1;
  return true;

#undef NEED
}
