#include "identify/st_convert.h"

#include <string.h>

#include "identify/detect_stf.h"

/* ---- ST1 layout (Players.pas:196-201), same offsets as
 * detect_st_family.c's detect_st1_structural (kept in sync manually -
 * that file is the oracle-cross-checked source of truth for these). ---- */
#define ST1_SMP_OFF(i) ((size_t)((i) - 1) * 130)
#define ST1_POS_OFF(i) (1950 + (size_t)(i) * 2)
#define ST1_POSLEN_OFF 2462
#define ST1_ORN_OFF(i) (2463 + (size_t)(i) * 32)
#define ST1_DEL_OFF 3007
#define ST1_PATLEN_OFF 3008
#define ST1_PAT_BASE 3009
#define ST1_PAT_ROW_SIZE 9 /* 3 channels * 3 bytes (Nt, ESNum, EONum) */
#define ST1_PAT_SIZE 576   /* 64 rows * ST1_PAT_ROW_SIZE */
#define ST1_MAX_PAT_E 31   /* Players.pas: ST1MaxPatE - FoundST1's bound */
#define ST1_MAX_PAT 64     /* Players.pas: ST1MaxPat - ST12STC's own, looser bound */
#define TSTCPAT_SIZE 7      /* Players.pas: TSTCPat = packed record Num:byte; Ofs:array[0..2] of word; end; */

/* Players.pas:1156-1157, st1nts (1-based: index note-1 for note in 1..7). */
static const int ST1_NTS[7] = {9, 11, 0, 2, 4, 5, 7};

static bool in_rng(size_t size, size_t off, size_t len) {
  return len <= size && off <= size - len;
}

/* ---- growable-string emulation for ST12STC's local `pat`/AddPat/Pats ---- */

#define PAT_STR_CAP 1024   /* generous cap per pattern-channel string; real
                             * content (<=64 rows) never gets remotely close */
#define MAX_PAT_STRINGS 128 /* NPatsU (<=32) * 3 channels <= 96, plus margin */

typedef struct {
  uint8_t buf[PAT_STR_CAP];
  size_t len;
} pat_entry;

typedef struct {
  uint8_t buf[PAT_STR_CAP];
  size_t len;
  bool overflow;
} pat_buf;

static void pb_push(pat_buf* pb, uint8_t byte) {
  if (pb->len >= PAT_STR_CAP) {
    pb->overflow = true;
    return;
  }
  pb->buf[pb->len++] = byte;
}

/* Players.pas:1770-1783, AddPat - translated literally, including its
 * unusual-but-correct accumulate-until-match offset logic (see the task
 * comment/migration notes: NOT a bug, just written unusually). Returns -1
 * on overflow (a safety cap Pascal's own unbounded dynamic array doesn't
 * need - see st_convert.h/this file's header comment on safety). */
static int add_pat(pat_entry* pats, int* count, const uint8_t* buf, size_t len) {
  int result = 0;
  for (int i = 0; i < *count; i++) {
    if (pats[i].len != len || memcmp(pats[i].buf, buf, len) != 0) {
      result += (int)pats[i].len;
    } else {
      return result;
    }
  }
  if (*count >= MAX_PAT_STRINGS || len > PAT_STR_CAP) return -1;
  memcpy(pats[*count].buf, buf, len);
  pats[*count].len = len;
  (*count)++;
  return result;
}

/* Players.pas:1789-1803, CalcEmpty. `empty` is the outer ST12STC local
 * updated by side effect (its value after the call, not just the return
 * value, is used by the caller - Inc(j, empty)). Safety: every offset
 * touched here is < ST1_PAT_BASE + n_pats_e*ST1_PAT_SIZE, which the
 * caller has already established is <= size before entering the pattern
 * conversion loop at all (see st1_to_stc's Phase 1/2 split below). */
static int calc_empty(const uint8_t* data, int ir, int j, int c, int pat_len, int* empty,
                       pat_buf* pb) {
  int newempty = 0;
  for (int n = j + 1; n < pat_len; n++) {
    size_t off = (size_t)ST1_PAT_BASE + (size_t)ir * ST1_PAT_SIZE +
                 (size_t)n * ST1_PAT_ROW_SIZE + (size_t)c * 3;
    uint8_t nt = data[off];
    if ((nt & 0xF0) == 0)
      newempty++;
    else
      break;
  }
  if (newempty != *empty) {
    *empty = newempty;
    pb_push(pb, (uint8_t)(161 + newempty));
  }
  return *empty;
}

/* Players.pas:1766-2049, ST12STC. See st_convert.h for the overall
 * caveats (no oracle file exists for this pair) and for why `msize` is
 * re-derived internally here (Phase 1) rather than taken as a caller-
 * supplied parameter the way Pascal's own signature has it: Pascal's
 * caller (FoundST1, via LoadTrackerModule) always passes the exact
 * structurally-derived F_Length, not the raw remaining-file/window size;
 * our C signature only gets the raw window, so Phase 1 below reproduces
 * FoundST1's own F_Length derivation (Players.pas:4922-5001) byte-for-
 * byte before Phase 2 runs the literal ST12STC body against it. Passing
 * the raw window size directly as msize instead (skipping Phase 1) would
 * almost always fail ST12STC's own `(msize-3009) mod 576 <> 0` exact-
 * multiple check for any window with trailing bytes after the module -
 * i.e. it would silently break ST1 detection entirely rather than just
 * skip confirmation, so this re-derivation is required, not optional. */
bool st1_to_stc(const uint8_t* data, size_t size, uint8_t out[65536], size_t* out_size) {
  /* ---- Phase 1: re-derive F_Length (Players.pas:4922-5001) ---- */
  if (!in_rng(size, 0, ST1_PAT_BASE + ST1_PAT_SIZE)) return false;

  uint8_t pat_len = data[ST1_PATLEN_OFF];
  if (pat_len < 1 || pat_len > 64) return false; /* ST12STC's own [1..64] check */
  uint8_t del = data[ST1_DEL_OFF];

  bool pat_used[ST1_MAX_PAT_E + 1] = {0};
  bool pat_exists[ST1_MAX_PAT_E + 1] = {0};
  int n_pats_u = 0, n_pats_e = 0;
  uint8_t pos_len = data[ST1_POSLEN_OFF];
  for (int i = 0; i <= 255; i++) {
    if (!in_rng(size, ST1_POS_OFF(i), 2)) return false;
    int pn = data[ST1_POS_OFF(i)]; /* PNum */
    if (pn == 0) return false;
    pn--;
    if (pn > ST1_MAX_PAT_E) return false;
    if (!pat_used[pn] && i <= pos_len) {
      n_pats_u++;
      pat_used[pn] = true;
    }
    if (!pat_exists[pn]) {
      n_pats_e++;
      pat_exists[pn] = true;
    }
  }
  if (n_pats_u == 0) return false;

  for (int i = ST1_MAX_PAT_E; i >= 0; i--) {
    if (pat_used[i]) break;
    if (pat_exists[i]) {
      pat_exists[i] = false;
      n_pats_e--;
    }
  }

  size_t msize = (size_t)ST1_PAT_BASE + (size_t)ST1_PAT_SIZE * (size_t)n_pats_e;
  if (!in_rng(size, 0, msize)) return false;

  /* ---- Phase 2: literal ST12STC body (Players.pas:1766-2049) ----
   * NPats = (msize-3009)/576 - 1 = n_pats_e - 1 exactly, by construction
   * of msize above - so ST12STC's own `if NPats > ST1MaxPat` check can
   * never trip (n_pats_e <= ST1_MAX_PAT_E+1 = 32), kept only as a literal
   * defense-in-depth mirror of the Pascal check. Likewise, ST12STC's own
   * PatUsed/PatExists position-table scan (which uses the looser
   * ST1MaxPat=64 bound, Players.pas:1844-1868) is mathematically
   * identical to Phase 1's scan above for every candidate that reached
   * here (all PNum-1 values are already proven <= ST1_MAX_PAT_E=31), so
   * it is not literally re-run - pat_used/pat_exists are reused directly
   * into the wider [0..ST1_MAX_PAT] arrays CPats indexing needs. */
  int n_pats = n_pats_e - 1;
  if (n_pats > ST1_MAX_PAT) return false;

  bool pu[ST1_MAX_PAT + 1] = {0};
  bool pe[ST1_MAX_PAT + 1] = {0};
  for (int i = 0; i <= ST1_MAX_PAT_E; i++) {
    pu[i] = pat_used[i];
    pe[i] = pat_exists[i];
  }

  bool smp_used[16] = {0};   /* index 1..15 used */
  bool orn_used[16] = {0};   /* index 0..15 used */
  orn_used[0] = true;

  typedef struct {
    uint8_t num;
    uint16_t ofs[3];
  } cpat_t;
  cpat_t cpats[ST1_MAX_PAT + 1];
  for (int i = 0; i <= ST1_MAX_PAT; i++) {
    cpats[i].num = (uint8_t)(i + 1);
    cpats[i].ofs[0] = cpats[i].ofs[1] = cpats[i].ofs[2] = 0;
  }

  pat_entry pats[MAX_PAT_STRINGS];
  int pat_count = 0;

  int ir = -1;
  for (int i = 0; i <= ST1_MAX_PAT; i++) {
    if (!pe[i]) continue;
    ir++;
    if (!pu[i]) continue;
    for (int c = 0; c < 3; c++) {
      pat_buf pb;
      pb.len = 0;
      pb.overflow = false;
      int empty = -1;
      int sam = -1, orn = -1, et = -1, ep = -1;
      int j = 0;
      while (j < pat_len) {
        size_t celloff = (size_t)ST1_PAT_BASE + (size_t)ir * ST1_PAT_SIZE +
                          (size_t)j * ST1_PAT_ROW_SIZE + (size_t)c * 3;
        uint8_t nt = data[celloff];
        uint8_t esnum = data[celloff + 1];
        uint8_t eonum = data[celloff + 2];
        int note = nt >> 4;
        if (note == 0) {
          j += calc_empty(data, ir, j, c, pat_len, &empty, &pb);
          pb_push(&pb, 0x81);
        } else {
          calc_empty(data, ir, j, c, pat_len, &empty, &pb); /* side effect on `empty` only */
          int esn = esnum >> 4;
          if (esn >= 1 && esn <= 15 && esn != sam) {
            sam = esn;
            pb_push(&pb, (uint8_t)(0x60 + esn));
            smp_used[esn] = true;
          }
          int et_cand = esnum & 15;
          if (et_cand >= 7 && et_cand <= 14) {
            if (et != et_cand || ep != eonum) {
              orn = -1;
              et = et_cand;
              ep = eonum;
              pb_push(&pb, (uint8_t)(0x80 + et_cand));
              pb_push(&pb, (uint8_t)ep);
            }
          } else if (et_cand == 1 || et_cand == 15) {
            int o = (et_cand == 1) ? 0 : (eonum & 15);
            if (o != orn) {
              et = -1;
              ep = -1;
              orn = o;
              if (et_cand == 1 && o == 0)
                pb_push(&pb, 0x82);
              else
                pb_push(&pb, (uint8_t)(0x70 + o));
              orn_used[o] = true;
            }
          }
          if ((note & 8) == 0) {
            int octave = nt & 7;
            int diez = (nt & 8) ? 1 : 0;
            if ((note == 2 || note == 5) && diez != 0) return false;
            int note2 = ST1_NTS[note - 1] + octave * 12 + diez;
            if (note2 < 0 || note2 > 0x5F) return false;
            pb_push(&pb, (uint8_t)note2);
          } else {
            pb_push(&pb, 0x80);
          }
          j += empty;
        }
        j++;
      }
      if (pb.overflow) return false;
      pb_push(&pb, 0xFF);
      if (pb.overflow) return false;
      int ofs = add_pat(pats, &pat_count, pb.buf, pb.len);
      if (ofs < 0) return false;
      cpats[i].ofs[c] = (uint16_t)ofs;
    }
  }

  /* ---- assemble the STC-layout output buffer (mc in Pascal) ---- */
  uint8_t mc[65536];
  memset(mc, 0, sizeof(mc));
  mc[0] = del; /* ST_Delay */
  memcpy(mc + 7, "SONG BY ST COMPILE", 18); /* ST_Name */

  size_t n = 27;

  for (int i = 1; i <= 15; i++) {
    if (!smp_used[i]) continue;
    if (n + 1 + 32 * 3 + 2 > 65536) return false;
    mc[n++] = (uint8_t)i;
    size_t smp_base = ST1_SMP_OFF(i);
    for (int j = 0; j < 32; j++) {
      uint8_t vl = data[smp_base + (size_t)j];
      uint8_t ns = data[smp_base + 32 + (size_t)j];
      uint16_t tn = (uint16_t)(data[smp_base + 64 + (size_t)j * 2] |
                                (data[smp_base + 64 + (size_t)j * 2 + 1] << 8));
      mc[n++] = (uint8_t)((vl & 15) | ((tn & 0xF00) >> 4));
      mc[n++] = (uint8_t)((ns & 0xDF) | ((tn & 0x1000) >> 7));
      mc[n++] = (uint8_t)(tn & 0xFF);
    }
    mc[n++] = data[smp_base + 128]; /* LPos */
    mc[n++] = data[smp_base + 129]; /* LLen */
  }

  size_t positions_pointer = n;
  if (n + 1 > 65536) return false;
  mc[n++] = pos_len;
  size_t pos_bytes = ((size_t)pos_len + 1) * 2;
  if (!in_rng(size, ST1_POS_OFF(0), pos_bytes)) return false;
  if (n + pos_bytes > 65536) return false;
  memcpy(mc + n, data + ST1_POS_OFF(0), pos_bytes);
  n += pos_bytes;

  size_t ornaments_pointer = n;
  for (int i = 0; i <= 15; i++) {
    if (!orn_used[i]) continue;
    if (n + 1 + 32 > 65536) return false;
    mc[n++] = (uint8_t)i;
    memcpy(mc + n, data + ST1_ORN_OFF(i), 32);
    n += 32;
  }

  size_t patterns_pointer = n;
  size_t c_base = n + (size_t)n_pats_u * TSTCPAT_SIZE + 1;
  for (int i = 0; i <= ST1_MAX_PAT; i++) {
    if (!pu[i]) continue;
    for (int ch = 0; ch < 3; ch++) {
      uint32_t newofs = (uint32_t)cpats[i].ofs[ch] + (uint32_t)c_base;
      if (newofs > 0xFFFF) return false;
      cpats[i].ofs[ch] = (uint16_t)newofs;
    }
    if (n + TSTCPAT_SIZE > 65536) return false;
    mc[n++] = cpats[i].num;
    mc[n++] = (uint8_t)(cpats[i].ofs[0] & 0xFF);
    mc[n++] = (uint8_t)(cpats[i].ofs[0] >> 8);
    mc[n++] = (uint8_t)(cpats[i].ofs[1] & 0xFF);
    mc[n++] = (uint8_t)(cpats[i].ofs[1] >> 8);
    mc[n++] = (uint8_t)(cpats[i].ofs[2] & 0xFF);
    mc[n++] = (uint8_t)(cpats[i].ofs[2] >> 8);
  }
  if (n + 1 > 65536) return false;
  mc[n++] = 255;

  for (int i = 0; i < pat_count; i++) {
    if (n + pats[i].len > 65536) return false;
    memcpy(mc + n, pats[i].buf, pats[i].len);
    n += pats[i].len;
  }

  mc[1] = (uint8_t)(positions_pointer & 0xFF);
  mc[2] = (uint8_t)(positions_pointer >> 8);
  mc[3] = (uint8_t)(ornaments_pointer & 0xFF);
  mc[4] = (uint8_t)(ornaments_pointer >> 8);
  mc[5] = (uint8_t)(patterns_pointer & 0xFF);
  mc[6] = (uint8_t)(patterns_pointer >> 8);
  mc[25] = (uint8_t)(n & 0xFF);
  mc[26] = (uint8_t)(n >> 8);

  memcpy(out, mc, n);
  *out_size = n;
  return true;
}

/* ---- ST3 (Players.pas:2051-2216, ST32STC) ---- */

static bool rng_ok(size_t size, int64_t off, int64_t len) {
  if (off < 0 || len < 0) return false;
  if ((uint64_t)off > (uint64_t)size) return false;
  return (uint64_t)len <= (uint64_t)size - (uint64_t)off;
}

static uint16_t rd16(const uint8_t* data, int64_t off) {
  return (uint16_t)(data[off] | (data[off + 1] << 8));
}

/* Players.pas:2051-2216, ST32STC. Unlike ST12STC, every one of ST32STC's
 * own `msize` checks is a plain upper-bound safety check (`if X > msize
 * then exit(False)`), never an exact-multiple/equality constraint - so,
 * unlike st1_to_stc, no F_Length pre-derivation phase is needed here:
 * using the real available window `size` directly as `msize` is both
 * safe (every read stays bounds-checked against the real buffer) and, if
 * anything, more permissive/accurate than Pascal's own conservative
 * pre-computed F_Length (which is itself just a conservative estimate
 * computed by FoundST3 before allocating the module - see this file's
 * header comment in st_convert.h). */
bool st3_to_stc(const uint8_t* data, size_t size, uint8_t out[65536], size_t* out_size) {
  if (size < 9) return false;
  int64_t msize = (int64_t)size;

  uint8_t mc[65536];
  memset(mc, 0, sizeof(mc));
  mc[0] = data[0]; /* ST3_Delay == ST_Delay: same on-disk byte, offset 0 */
  memcpy(mc + 7, "SONG BY ST COMPILE", 18);
  size_t n = 27;

  uint16_t positions_pointer_in = rd16(data, 1);
  uint16_t samples_pointer_in = rd16(data, 3);
  uint16_t ornaments_pointer_in = rd16(data, 5);
  uint16_t patterns_pointer_in = rd16(data, 7);

  int64_t i = (int64_t)positions_pointer_in - 9;
  if (i <= 0) return false;

  bool id = false;
  if (i % 130 != 0) {
    if (i < 55 || (i - 55) % 130 != 0) return false;
    id = true;
  }

  int64_t ptr = samples_pointer_in;
  if (ptr >= msize - 3) return false;
  if (!rng_ok(size, ptr, 1)) return false;
  int64_t num = data[ptr];
  ptr++;
  if (ptr + num * 2 > msize) return false;

  if (!rng_ok(size, ptr, 2)) return false;
  int64_t loadaddr = (int64_t)rd16(data, ptr) - 9;
  if (id) loadaddr -= 55;
  if (loadaddr < 0 || loadaddr + msize > 65536) return false;

  for (int64_t ii = 0; ii < num; ii++) {
    if (n + 1 > 65536) return false;
    mc[n++] = (uint8_t)ii;
    if (!rng_ok(size, ptr, 2)) return false;
    int64_t ptr2 = (int64_t)rd16(data, ptr) - loadaddr;
    ptr += 2;
    if (!rng_ok(size, ptr2, 2 + 32 * 4)) return false;

    int lpbeg = data[ptr2];
    ptr2++;
    int lplen = data[ptr2] - lpbeg;
    ptr2++;
    if (n + 32 * 3 + 2 > 65536) return false;
    for (int j = 0; j < 32; j++) {
      int16_t tn = (int16_t)rd16(data, ptr2);
      ptr2 += 2;
      uint8_t en = data[ptr2];
      ptr2++;
      uint8_t ns_raw = data[ptr2];
      ptr2++;
      uint8_t ns = (uint8_t)((en & 0x80) | ((en & 0x10) << 2) | (ns_raw & 0x1F));
      if (tn > 0)
        ns |= 0x20;
      else
        tn = (int16_t)(-tn);
      uint16_t utn = (uint16_t)tn;
      mc[n++] = (uint8_t)((en & 15) | (uint8_t)((uint8_t)(utn >> 8) << 4));
      mc[n++] = ns;
      mc[n++] = (uint8_t)(utn & 0xFF);
    }
    mc[n++] = (uint8_t)lpbeg;
    mc[n++] = (uint8_t)lplen;
  }

  size_t positions_pointer_out = n;
  ptr = positions_pointer_in;
  if (ptr >= msize - 3) return false;
  if (!rng_ok(size, ptr, 1)) return false;
  int64_t num_pos = (int64_t)data[ptr] - 1;
  ptr++;
  if (ptr + num_pos * 2 > msize) return false;
  if (n + 1 > 65536) return false;
  mc[n++] = (uint8_t)(uint8_t)num_pos;
  int64_t maxpat = -1;
  for (int64_t ii = 0; ii <= num_pos; ii++) {
    if (!rng_ok(size, ptr, 2)) return false;
    if (data[ptr + 1] % 6 != 0) return false;
    int64_t patn = data[ptr + 1] / 6;
    if (patn > maxpat) maxpat = patn;
    if (n + 2 > 65536) return false;
    mc[n] = (uint8_t)patn;
    mc[n + 1] = data[ptr];
    n += 2;
    ptr += 2;
  }
  if (maxpat < 0) return false;

  size_t ornaments_pointer_out = n;
  ptr = ornaments_pointer_in;
  if (ptr >= msize - 3) return false;
  if (!rng_ok(size, ptr, 1)) return false;
  int64_t num_orn = data[ptr];
  ptr++;
  if (ptr + num_orn * 2 > msize) return false;
  for (int64_t ii = 0; ii < num_orn; ii++) {
    if (n + 1 + 32 > 65536) return false;
    mc[n++] = (uint8_t)ii;
    if (!rng_ok(size, ptr, 2)) return false;
    int64_t ptr2 = (int64_t)rd16(data, ptr) - loadaddr;
    ptr += 2;
    if (!rng_ok(size, ptr2, 32)) return false;
    memcpy(mc + n, data + ptr2, 32);
    n += 32;
  }

  int64_t patsptr = ptr;

  size_t patterns_pointer_out = n;
  ptr = patterns_pointer_in;
  if (ptr + maxpat * 6 + 6 > msize) return false;

  int64_t patsdif = -patsptr + (int64_t)patterns_pointer_out + maxpat * 7 + 8;
  for (int64_t ii = 0; ii <= maxpat; ii++) {
    if (n + 1 + 6 > 65536) return false;
    mc[n++] = (uint8_t)ii;
    for (int j = 0; j < 3; j++) {
      if (!rng_ok(size, ptr, 2)) return false;
      int64_t neww = (int64_t)rd16(data, ptr) + patsdif;
      mc[n] = (uint8_t)(neww & 0xFF);
      mc[n + 1] = (uint8_t)((neww >> 8) & 0xFF);
      n += 2;
      ptr += 2;
    }
  }
  if (n + 1 > 65536) return false;
  mc[n++] = 255;

  i = (int64_t)patterns_pointer_in - patsptr - 1;
  if (i <= 0 || patsptr + i > msize) return false;
  if (patsptr + i < msize && data[patsptr + i] == 255) i++;
  if (!rng_ok(size, patsptr, i)) return false;
  if (n + (size_t)i > 65536) return false;
  memcpy(mc + n, data + patsptr, (size_t)i);
  n += (size_t)i;

  mc[1] = (uint8_t)(positions_pointer_out & 0xFF);
  mc[2] = (uint8_t)(positions_pointer_out >> 8);
  mc[3] = (uint8_t)(ornaments_pointer_out & 0xFF);
  mc[4] = (uint8_t)(ornaments_pointer_out >> 8);
  mc[5] = (uint8_t)(patterns_pointer_out & 0xFF);
  mc[6] = (uint8_t)(patterns_pointer_out >> 8);
  mc[25] = (uint8_t)(n & 0xFF);
  mc[26] = (uint8_t)(n >> 8);

  memcpy(out, mc, n);
  *out_size = n;
  return true;
}

/* --- STF2STP (Players.pas:1346-1764) --------------------------------- */

/* Depacked-buffer field offsets - same table as detect_stf.c's own
 * (duplicated per this project's per-file convention; see that file's
 * top comment for the full field-layout derivation). ModTypes variant
 * 15 (Players.pas:202-210). */
#define STF_SMP_OFF(i) ((size_t)((i)-1) * 130)
#define STF_POS_OFF(i) (1950 + (size_t)(i)*2)
#define STF_POSLEN_OFF 2462
#define STF_ORN_OFF(i) (2463 + (size_t)(i)*32)
#define STF_DELAY_OFF 3007
#define STF_PATLEN_OFF(i) (3008 + (size_t)((i)-1))
#define STF_LOOPPOS_OFF 3039
#define STF_TITLE_OFF(i) (3040 + (size_t)((i)-1))
/* Players.pas: STF_MinSize = STF_PrePatsSize + STF_PatSize. */
#define STF_MIN_SIZE (STF_PRE_PATS_SIZE + STF_PAT_SIZE)

/* Players.pas:1156-1157 - same table as detect_st_family.c's ST1_NTS
 * (duplicated per this project's per-file convention). */
static const int STF_NTS[7] = {9, 11, 0, 2, 4, 5, 7};

/* Working buffer size: Players.pas ModTypes.Index is array[0..65536] of
 * byte, i.e. 65537 bytes - matched here so every offset the Pascal
 * source uses (including the inclusive top index 65536) stays in
 * bounds. */
#define STF_WORK_SIZE 65537

/* --- Pats: dedup'd pattern-opcode-stream table (Players.pas nested
 * function AddPat, Players.pas:1349-1360). Up to 31 patterns * 3
 * channels = 93 distinct streams can ever be added. */
#define STF_PAT_STR_CAP 800 /* see st_convert.c derivation: 64 rows * 9B/row + 1 */
#define STF_PATS_MAX 96

typedef struct {
  uint8_t data[STF_PAT_STR_CAP];
  size_t len;
} stf_pat_str;

typedef struct {
  stf_pat_str pats[STF_PATS_MAX];
  int count;
} stf_pat_list;

/* Players.pas:1349-1360 AddPat - returns the offset (within the eventual
 * concatenation of all distinct pattern strings) at which `pat` starts,
 * appending it to the table first if it is not already present. */
static int stf_add_pat(stf_pat_list* pl, const uint8_t* buf, size_t len) {
  size_t result = 0;
  for (int i = 0; i < pl->count; i++) {
    if (pl->pats[i].len != len || memcmp(pl->pats[i].data, buf, len) != 0) {
      result += pl->pats[i].len;
    } else {
      return (int)result;
    }
  }
  if (pl->count >= STF_PATS_MAX || len > STF_PAT_STR_CAP) return -1;
  memcpy(pl->pats[pl->count].data, buf, len);
  pl->pats[pl->count].len = len;
  pl->count++;
  return (int)result;
}

/* A single pattern-channel opcode stream being built (Pascal's local
 * `pat` string variable). */
typedef struct {
  uint8_t data[STF_PAT_STR_CAP];
  size_t len;
} stf_out_stream;

static bool stf_append(stf_out_stream* s, uint8_t b) {
  if (s->len >= STF_PAT_STR_CAP) return false;
  s->data[s->len++] = b;
  return true;
}

/* Players.pas:1362-1379 InsertUnusedPatterns nested function - a
 * structural pre-processing step run BEFORE the main conversion: it
 * expands the depacked buffer's gapless (only-present-patterns-stored)
 * pattern region back out into the standard "pattern index i lives at
 * STF_PrePatsSize+(i-1)*STF_PatSize" layout the rest of STF2STP (and
 * every offset macro above) assumes. `mc` is 65537 bytes of scratch,
 * distinct from its later reuse as the STP-layout output buffer in
 * stf_to_stp. Mutates *msize (Players.pas mutates its own `msize`
 * var-parameter the same way). */
static bool stf_insert_unused_patterns(uint8_t m[STF_WORK_SIZE],
                                        uint8_t mc[STF_WORK_SIZE], int* msize) {
  bool pats_used[32];
  memset(pats_used, 0, sizeof(pats_used));

  int pofs = *msize - (int)STF_PRE_PATS_SIZE;
  for (int i = 0; i <= 255; i++) {
    uint8_t b = m[STF_POS_OFF(i)];
    if (b < 1 || b > 31) return false;
    if (!pats_used[b]) {
      pats_used[b] = true;
      pofs -= STF_PAT_SIZE;
      if (pofs < 0) return false;
    }
  }

  if (*msize < (int)STF_PRE_PATS_SIZE) return false; /* defensive; already guaranteed */
  size_t copy_len = (size_t)(*msize - (int)STF_PRE_PATS_SIZE);
  if (copy_len > STF_WORK_SIZE) return false; /* defensive */
  memcpy(mc, m + STF_PRE_PATS_SIZE, copy_len);

  pofs = 0;
  *msize = (int)STF_PRE_PATS_SIZE;
  for (int i = 1; i <= 31; i++) {
    if (pats_used[i]) {
      if ((size_t)pofs + STF_PAT_SIZE > copy_len) return false; /* defensive, see .c comment */
      memcpy(m + *msize, mc + pofs, STF_PAT_SIZE);
      pofs += STF_PAT_SIZE;
    }
    *msize += STF_PAT_SIZE;
  }
  return true;
}

/* Players.pas:1382-1397 CalcEmpty nested function - counts how many
 * further rows (from j+1) in channel c of pattern i have no note, no
 * slide/envelope/ornament command, and no volume; appends a "skip N
 * empty rows" marker byte ($80+N) to `out` iff N differs from the
 * caller's running `*empty` state. Returns the new N either way. */
static int stf_calc_empty(const uint8_t m[STF_WORK_SIZE], int i, int j, int c,
                           int patlen, int* empty, stf_out_stream* out,
                           bool* ok) {
  int newempty = 0;
  for (int n = j + 1; n < patlen; n++) {
    size_t base = STF_PRE_PATS_SIZE + (size_t)(i - 1) * STF_PAT_SIZE +
                  (size_t)n * 9 + (size_t)c * 3;
    uint8_t nt = m[base];
    uint8_t smcmd = m[base + 1];
    uint8_t param = m[base + 2];
    if ((nt & 0xF0) != 0 || (smcmd & 0xF) != 0 || (param & 0xF0) != 0) break;
    newempty++;
  }
  if (newempty != *empty) {
    *empty = newempty;
    if (!stf_append(out, (uint8_t)(0x80 + newempty))) *ok = false;
  }
  return newempty;
}

/* Players.pas:98 TSTPPat = packed record Ofs: array[0..2] of word; end; */
typedef struct {
  int ofs[3];
} stf_cpat;

/* Players.pas:1346-1764 STF2STP main body, operating on the depacked,
 * gap-expanded buffer `m` (STF_WORK_SIZE bytes, already validated by
 * stf_insert_unused_patterns) and producing STP-layout bytes into
 * `mc` (reused as scratch above, now the final output buffer). On
 * success, *out_size holds the number of valid bytes written to mc. */
static bool stf_to_stp_convert(uint8_t m[STF_WORK_SIZE], uint8_t mc[STF_WORK_SIZE],
                                int msize, size_t* out_size) {
  if (msize < STF_MIN_SIZE) return false;

  bool pats_used[32];
  bool sams_used[16];
  bool orns_used[16];
  memset(pats_used, 0, sizeof(pats_used));
  memset(sams_used, 0, sizeof(sams_used));
  memset(orns_used, 0, sizeof(orns_used));
  orns_used[0] = true;

  int pats_used_max = 0;
  int pos_len = m[STF_POSLEN_OFF];
  for (int i = 0; i <= pos_len; i++) {
    uint8_t b = m[STF_POS_OFF(i)];
    if (b < 1 || b > 31) return false; /* defensive: already true post-InsertUnusedPatterns */
    uint8_t patlen = m[STF_PATLEN_OFF(b)];
    if (patlen < 1 || patlen > 64) return false;
    if (b > pats_used_max) pats_used_max = b;
    pats_used[b] = true;
  }

  if (msize < STF_MIN_SIZE + STF_PAT_SIZE * (pats_used_max - 1)) return false;

  static stf_pat_list pat_list_storage; /* too big for comfortable stack reuse alongside m/mc */
  stf_pat_list* pat_list = &pat_list_storage;
  memset(pat_list, 0, sizeof(*pat_list));
  static stf_cpat cpats[32];
  memset(cpats, 0, sizeof(cpats));

  for (int patn = 1; patn <= 31; patn++) {
    if (!pats_used[patn]) continue;
    int patlen = m[STF_PATLEN_OFF(patn)];
    for (int chn = 0; chn < 3; chn++) {
      stf_out_stream out;
      out.len = 0;
      bool append_ok = true;

      int empty = -1;
      int sam = -1;
      int env = 0;
      int orn = -1;
      int sld = 0;
      int vol = -1;
      int lin = 0;

      while (lin < patlen) {
        size_t base = STF_PRE_PATS_SIZE + (size_t)(patn - 1) * STF_PAT_SIZE +
                      (size_t)lin * 9 + (size_t)chn * 3;
        uint8_t nt = m[base];
        uint8_t smcmd = m[base + 1];
        uint8_t param = m[base + 2];

        int sample = smcmd >> 4;
        int ornament = -1;
        int slide_t = 0;
        int slide_d = 0;
        int env_t = 0;
        int env_p = 0;
        int volume = 0;
        int blow = smcmd & 15;

        if (blow == 1) {
          slide_t = 1;
          slide_d = param;
        } else if (blow == 2) {
          slide_t = -1;
          slide_d = -(int)param;
        } else if (blow >= 8 && blow <= 14) {
          env_t = blow;
          env_p = param;
          env = 1;
        } else {
          volume = param >> 4;
          if (blow == 15) {
            ornament = param & 15;
            orns_used[ornament] = true;
          } else if (blow != 0) {
            ornament = 0;
          }
        }

        int note;
        if (nt >= 0xF0 && nt <= 0xF7) {
          note = -2; /* R-- */
        } else {
          note = nt >> 4;
          if (note == 0) {
            note = -1; /* no note */
          } else if (note >= 1 && note <= 7) {
            int diez = (nt & 8) != 0 ? 1 : 0;
            if ((note == 2 || note == 5) && diez != 0) return false;
            int octave = nt & 7;
            note = STF_NTS[note - 1] + octave * 12 + diez;
          } else {
            return false;
          }
        }

        if (sample > 0 && sam != sample && note >= 0) {
          sams_used[sample] = true;
          append_ok &= stf_append(&out, (uint8_t)(0x60 + sample));
          sam = sample;
        }
        if (ornament >= 0 && (orn != ornament || sld != 0 || env != 0)) {
          append_ok &= stf_append(&out, (uint8_t)(0x70 + ornament));
          orn = ornament;
          sld = 0;
          env = 0;
        }
        if (env_t > 0) {
          append_ok &= stf_append(&out, (uint8_t)(0xC0 + env_t));
          append_ok &= stf_append(&out, (uint8_t)env_p);
        }
        if (slide_t != 0) {
          append_ok &= stf_append(&out, 0xF0);
          append_ok &= stf_append(&out, (uint8_t)slide_d);
          sld = slide_t;
        }
        if (note >= 0 && env_t > 0) volume = 15;
        if (volume > 0 && vol != volume) {
          append_ok &= stf_append(&out, (uint8_t)(0x100 - volume));
          vol = volume;
        }

        int adv = stf_calc_empty(m, patn, lin, chn, patlen, &empty, &out, &append_ok) + 1;
        lin += adv;

        if (note == -2) {
          append_ok &= stf_append(&out, 0xD0);
        } else if (note == -1) {
          append_ok &= stf_append(&out, 0xE0);
        } else {
          append_ok &= stf_append(&out, (uint8_t)(note + 1));
        }

        if (!append_ok) return false;
      }
      if (!stf_append(&out, 0)) return false;

      int ofs = stf_add_pat(pat_list, out.data, out.len);
      if (ofs < 0) return false;
      cpats[patn].ofs[chn] = ofs;
    }
  }

  static const char STP_ID[] = "KSA SOFTWARE COMPILATION OF "; /* 28 chars */
  memset(mc, 0, STF_WORK_SIZE);
  mc[0] = m[STF_DELAY_OFF];
  int curofs = 10;

  bool title_nonempty = false;
  for (int i = 0; i < 25; i++)
    if (m[STF_TITLE_OFF(1) + (size_t)i] > 0x20) {
      title_nonempty = true;
      break;
    }
  if (title_nonempty) {
    if (curofs + 28 + 25 > 65536) return false;
    memcpy(mc + curofs, STP_ID, 28);
    curofs += 28;
    memcpy(mc + curofs, m + STF_TITLE_OFF(1), 25);
    curofs += 25;
  }

  int patsofs = curofs;
  for (int i = 0; i < pat_list->count; i++) {
    size_t l = pat_list->pats[i].len;
    if ((size_t)curofs + l > 65536) return false;
    memcpy(mc + curofs, pat_list->pats[i].data, l);
    curofs += (int)l;
  }

  int ornofs = curofs;
  for (int i = 0; i <= 15; i++) {
    if (!orns_used[i]) continue;
    size_t base = STF_ORN_OFF(i);
    uint8_t lpb = m[base];
    uint8_t lpe = m[base + 1];
    if (lpe > 29 || lpb > lpe || curofs + (int)lpe > 65536 - 3) return false;
    mc[curofs++] = lpb;
    mc[curofs++] = (uint8_t)(lpe + 1);
    for (int lin = 0; lin <= lpe; lin++) {
      int8_t sv = (int8_t)m[base + 2 + (size_t)lin];
      if (sv < -64 || sv > 63) return false;
      mc[curofs++] = (uint8_t)sv;
    }
  }

  int samofs = curofs;
  for (int i = 1; i <= 15; i++) {
    if (!sams_used[i]) continue;
    size_t base = STF_SMP_OFF(i);
    uint8_t lpb = m[base + 128];
    int lin_count;
    if (lpb == 0) {
      lin_count = 32;
    } else {
      if (lpb > 32) return false;
      uint8_t lps = m[base + 129];
      lin_count = lpb + lps;
      if (lin_count > 32) return false;
    }
    if (curofs + lin_count * 0x80 > 65536 - 2) return false; /* verbatim from Players.pas */
    mc[curofs++] = (uint8_t)(lpb - 1);
    mc[curofs++] = (uint8_t)lin_count;
    for (int lin = 0; lin < lin_count; lin++) {
      uint8_t vl = m[base + (size_t)lin];
      if (vl > 15) return false;
      uint8_t ntens = m[base + 32 + (size_t)lin];
      uint8_t b = vl;
      if ((int8_t)ntens < 0) b |= 0x80;
      if (ntens & 0x40) b |= 0x10;
      mc[curofs++] = b;
      uint8_t b2 = (uint8_t)(((ntens & 0x20) != 0 ? 1 : 0) + ((ntens & 0x1f) << 1));
      mc[curofs++] = b2;
      uint16_t tn = (uint16_t)(m[base + 64 + (size_t)lin * 2] |
                                ((uint16_t)m[base + 64 + (size_t)lin * 2 + 1] << 8));
      if (tn & 0xE000) return false;
      int w = tn & 0xFFF;
      if (!(tn & 0x1000)) w = -w;
      uint16_t uw = (uint16_t)(int16_t)w;
      mc[curofs++] = (uint8_t)(uw & 0xFF);
      mc[curofs++] = (uint8_t)((uw >> 8) & 0xFF);
    }
  }

  if (curofs + 2 * (pos_len + 2) > 65536) return false;
  int stp_positions_pointer = curofs;
  mc[curofs++] = (uint8_t)(pos_len + 1);
  mc[curofs++] = m[STF_LOOPPOS_OFF];
  for (int i = 0; i <= pos_len; i++) {
    uint8_t pat_b = m[STF_POS_OFF(i)];
    mc[curofs++] = (uint8_t)((pat_b - 1) * 6);
    mc[curofs++] = m[STF_POS_OFF(i) + 1];
  }

  if (curofs + 6 * pats_used_max > 65536) return false;
  int stp_patterns_pointer = curofs;
  for (int patn = 1; patn <= pats_used_max; patn++) {
    for (int chn = 0; chn < 3; chn++) cpats[patn].ofs[chn] += patsofs;
    for (int chn = 0; chn < 3; chn++) {
      mc[curofs] = (uint8_t)(cpats[patn].ofs[chn] & 0xFF);
      mc[curofs + 1] = (uint8_t)((cpats[patn].ofs[chn] >> 8) & 0xFF);
      curofs += 2;
    }
  }

  if (curofs > 65536 - 2 * 16) return false;
  int stp_ornaments_pointer = curofs;
  int orn_running = ornofs;
  for (int i = 0; i <= 15; i++) {
    mc[curofs] = (uint8_t)(orn_running & 0xFF);
    mc[curofs + 1] = (uint8_t)((orn_running >> 8) & 0xFF);
    if (orns_used[i]) {
      uint8_t lpe = m[STF_ORN_OFF(i) + 1];
      orn_running += 2 + lpe + 1;
    }
    curofs += 2;
  }

  if (curofs > 65536 - 2 * 15) return false;
  int stp_samples_pointer = curofs;
  int sam_running = samofs;
  for (int i = 1; i <= 15; i++) {
    mc[curofs] = (uint8_t)(sam_running & 0xFF);
    mc[curofs + 1] = (uint8_t)((sam_running >> 8) & 0xFF);
    if (sams_used[i]) {
      uint8_t lpb = m[STF_SMP_OFF(i) + 128];
      int lin_count = (lpb == 0) ? 32 : (lpb + m[STF_SMP_OFF(i) + 129]);
      sam_running += 2 + lin_count * 4;
    }
    curofs += 2;
  }

  mc[9] = (uint8_t)(pats_used_max * 3 + 16 + 15);
  mc[1] = (uint8_t)(stp_positions_pointer & 0xFF);
  mc[2] = (uint8_t)((stp_positions_pointer >> 8) & 0xFF);
  mc[3] = (uint8_t)(stp_patterns_pointer & 0xFF);
  mc[4] = (uint8_t)((stp_patterns_pointer >> 8) & 0xFF);
  mc[5] = (uint8_t)(stp_ornaments_pointer & 0xFF);
  mc[6] = (uint8_t)((stp_ornaments_pointer >> 8) & 0xFF);
  mc[7] = (uint8_t)(stp_samples_pointer & 0xFF);
  mc[8] = (uint8_t)((stp_samples_pointer >> 8) & 0xFF);

  *out_size = (size_t)curofs;
  return true;
}

bool stf_to_stp(const uint8_t* data, size_t size, uint8_t out[65536], size_t* out_size) {
  stf_depack_state s;
  s.buf1 = data;
  s.psize = size;
  s.pos1 = 1;
  s.pos2 = 0;
  stf_depack_all(&s);
  if (!s.ok) return false;

  static uint8_t m[STF_WORK_SIZE];
  static uint8_t mc[STF_WORK_SIZE];
  memset(m, 0, STF_WORK_SIZE);

  size_t produced = s.pos2;
  if (produced > STF_MAX_SIZE) produced = STF_MAX_SIZE; /* defensive; shouldn't happen */
  memcpy(m, s.buf2, produced);
  int msize = (int)produced;

  if (msize < STF_MIN_SIZE) return false;
  if (!stf_insert_unused_patterns(m, mc, &msize)) return false;

  size_t final_size;
  if (!stf_to_stp_convert(m, mc, msize, &final_size)) return false;
  if (final_size > 65536) return false; /* defensive */

  memcpy(out, mc, final_size);
  *out_size = final_size;
  return true;
}
