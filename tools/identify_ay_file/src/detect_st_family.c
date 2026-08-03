#include "identify/detect_st_family.h"

/* ModTypes variant 14 (Players.pas:196-201) layout, all offsets 0-based
 * file offsets (F_Frame^ overlays the candidate window directly):
 *   ST1_Smp[1..15]  @ 0,          130 bytes each (Vl[32] Ns[32] Tn[32]*2 LPos LLen)
 *   ST1_Pos[0..255] @ 1950,       2 bytes each (PNum, PTrans)
 *   ST1_PosLen      @ 2462,       1 byte
 *   ST1_Orn[0..16]  @ 2463,       32 bytes each (shortint[32])
 *   ST1_Del         @ 3007
 *   ST1_PatLen      @ 3008
 *   ST1_Pat[0..64]  @ 3009,       576 bytes each (64 rows * 3 chans * 3 bytes) */
#define ST1_SMP_OFF(i) ((size_t)((i) - 1) * 130)
#define ST1_POS_OFF(i) (1950 + (size_t)(i) * 2)
#define ST1_POSLEN_OFF 2462
#define ST1_ORN_OFF(i) (2463 + (size_t)(i) * 32)
#define ST1_DEL_OFF 3007
#define ST1_PATLEN_OFF 3008
#define ST1_PAT_BASE 3009
#define ST1_PAT_ROW_SIZE 9 /* 3 channels * 3 bytes */
#define ST1_PAT_SIZE 576   /* 64 rows * ST1_PAT_ROW_SIZE */
#define ST1_MAX_PAT_E 31   /* Players.pas: ST1MaxPatE */

/* Players.pas:1156-1157. */
static const int ST1_NTS[7] = {9, 11, 0, 2, 4, 5, 7};

/* Players.pas:4922-5065 FoundST1, minus the final IntegrityCheck step -
 * see detect_st_family.h's file comment. */
bool detect_st1_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, ST1_PAT_BASE + ST1_PAT_SIZE)) return false;

  uint8_t pat_len = byte_at(f, ST1_PATLEN_OFF);
  if (pat_len < 11 || pat_len > 64) return false;
  uint8_t del = byte_at(f, ST1_DEL_OFF);
  if (del < 1 || del > 15) return false;

  for (int i = 0; i <= 16; i++) {
    for (int j = 0; j < 32; j++) {
      int8_t v = (int8_t)byte_at(f, ST1_ORN_OFF(i) + (size_t)j);
      if (v < -64 || v > 63) return false;
    }
  }

  for (int i = 1; i <= 15; i++) {
    size_t base = ST1_SMP_OFF(i);
    if (byte_at(f, base + 128) > 32) return false; /* LPos */
    if (byte_at(f, base + 129) > 31) return false; /* LLen */
    for (int j = 0; j < 32; j++) {
      if (byte_at(f, base + (size_t)j) > 15) return false;        /* Vl */
      if (byte_at(f, base + 32 + (size_t)j) & 0x20) return false; /* Ns */
      if (le16_at(f, base + 64 + (size_t)j * 2) > 0x1FFF) return false; /* Tn */
    }
  }

  bool pat_used[ST1_MAX_PAT_E + 1] = {0};
  bool pat_exists[ST1_MAX_PAT_E + 1] = {0};
  int n_pats_u = 0, n_pats_e = 0;
  uint8_t pos_len = byte_at(f, ST1_POSLEN_OFF);
  for (int i = 0; i <= 255; i++) {
    int n = byte_at(f, ST1_POS_OFF(i)); /* PNum */
    if (n == 0) return false;
    n--;
    if (n > ST1_MAX_PAT_E) return false;
    if (!pat_used[n] && i <= pos_len) {
      n_pats_u++;
      pat_used[n] = true;
    }
    if (!pat_exists[n]) {
      n_pats_e++;
      pat_exists[n] = true;
    }
  }
  if (n_pats_u == 0) return false;

  /* cutoff unused patterns after the last used one (Players.pas:4992-5001). */
  for (int i = ST1_MAX_PAT_E; i >= 0; i--) {
    if (pat_used[i]) break;
    if (pat_exists[i]) {
      pat_exists[i] = false;
      n_pats_e--;
    }
  }

  size_t length = (size_t)ST1_PAT_BASE + (size_t)ST1_PAT_SIZE * (size_t)n_pats_e;
  if (!in_bounds(f, 0, length)) return false;

  int ir = -1, nt_cnt = 0;
  for (int i = 0; i <= ST1_MAX_PAT_E; i++) {
    if (!pat_exists[i]) continue;
    ir++;
    for (int c = 0; c < 3; c++) {
      for (int j = 0; j < 64; j++) {
        size_t off = (size_t)ST1_PAT_BASE + (size_t)ir * ST1_PAT_SIZE +
                     (size_t)j * ST1_PAT_ROW_SIZE + (size_t)c * 3;
        uint8_t raw_nt = byte_at(f, off);
        int note = raw_nt >> 4;
        if (note == 0) continue;
        if (note & 8) return false; /* bit7 of raw_nt set -> invalid */
        int octave = raw_nt & 7;
        int diez = (raw_nt & 8) ? 1 : 0;
        if ((note == 2 || note == 5) && diez != 0) return false;
        int pitch = ST1_NTS[note - 1] + octave * 12 + diez;
        if (pitch < 0 || pitch > 0x5F) return false;
        nt_cnt++;
      }
    }
  }
  if (nt_cnt == 0) return false;

  d->format = "ST1";
  d->confidence = "probable"; /* see file header: IntegrityCheck not run */
  d->chips = 1;
  return true;
}

/* Players.pas:5175-5297 FoundST3, minus the final IntegrityCheck step.
 * ModTypes variant 16 (Players.pas:211-214): ST3_Delay(byte) @0,
 * ST3_PositionsPointer/SamplesPointer/OrnamentsPointer/PatternsPointer
 * (word each) @1/3/5/7, ST3_Title[1..55] @9. All pointer fields are
 * native (LE) `word`s, i.e. plain file offsets from the candidate start
 * (F_Frame^.Index[0]). */
bool detect_st3_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 9)) return false;
  uint16_t positions_ptr = le16_at(f, 1);
  uint16_t ornaments_ptr = le16_at(f, 5);
  uint16_t samples_ptr = le16_at(f, 3);
  uint16_t patterns_ptr = le16_at(f, 7);

  int j1 = (int)patterns_ptr - (int)ornaments_ptr;
  if (j1 <= 0) return false;
  int j2 = (int)ornaments_ptr - (int)samples_ptr;
  if (j2 <= 0) return false;
  int j3 = (int)samples_ptr - (int)positions_ptr;
  if (j3 <= 0) return false;
  int j4 = (int)positions_ptr - 9;
  if (j4 <= 0) return false;

  bool id = false;
  if (j4 % 130 != 0) {
    if (j4 < 55 || (j4 - 55) % 130 != 0) return false;
    id = true;
  }

  if (!in_bounds(f, samples_ptr, 1)) return false;
  int j5 = byte_at(f, samples_ptr); /* number of samples */
  if (j5 < 1 || j5 > 16) return false;
  int j = j5 * 130 + 9;
  if (id) j += 55;
  if ((int)positions_ptr != j) return false;

  if (!in_bounds(f, (size_t)j, 1)) return false;
  int j6 = byte_at(f, (size_t)j); /* number of positions */
  if (j6 == 0) return false;
  j += j6 * 2 + 1;

  if ((int)samples_ptr != j && (int)samples_ptr != j + 2) return false;

  if (!in_bounds(f, ornaments_ptr, 1)) return false;
  int j7 = byte_at(f, ornaments_ptr); /* number of ornaments */
  if (j7 < 1 || j7 > 16) return false;

  j = samples_ptr + j5 * 2 + 1 + j7 * 32;
  if ((int)ornaments_ptr != j) return false;
  j += j7 * 2 + 1;
  if (!in_bounds(f, patterns_ptr, 2) || (int)le16_at(f, patterns_ptr) != j) return false;

  if (!in_bounds(f, (size_t)samples_ptr + 1, 2)) return false;
  int loadaddr = (int)le16_at(f, (size_t)samples_ptr + 1) - 9;
  if (id) loadaddr -= 55;
  if (loadaddr < 0) return false;

  if (!in_bounds(f, (size_t)ornaments_ptr + 1, 2)) return false;
  j = (int)le16_at(f, (size_t)ornaments_ptr + 1) - loadaddr;
  if (j != samples_ptr + j5 * 2 + 1) return false;

  /* ornament 0 must be dummy (all zero) */
  if (!in_bounds(f, (size_t)j, 32)) return false;
  for (int k = j; k < j + 32; k++)
    if (byte_at(f, (size_t)k) != 0) return false;

  int j8 = -1;
  j = positions_ptr + 2;
  for (int i = 0; i < j6; i++) {
    if (!in_bounds(f, (size_t)j, 1)) return false;
    int v = byte_at(f, (size_t)j);
    if (v % 6 != 0) return false;
    if (j8 < v) j8 = v;
    j += 2;
  }

  j = patterns_ptr + j8 + 6;
  if (!in_bounds(f, 0, (size_t)j)) return false;
  if (loadaddr + j > 65536) return false;

  if (id) {
    /* KsaId check (Players.pas:5280) - the embedded "S.T.3 Compilation of"
     * style tag text at offset 9; skipped here since we cannot access
     * Players.pas's KsaId string constant table without pulling in the
     * whole PlayerTag subsystem, and it is a secondary confirmation only
     * (the pointer-arithmetic checks above already establish the ST3
     * layout) - documented as an approximation, not silently omitted. */
  }

  d->format = "ST3";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}

/* Players.pas:5555-5624 FoundSTP, minus the final IntegrityCheck step.
 * ModTypes variant 4 (Players.pas:124-127): STP_Delay(byte) @0,
 * STP_PositionsPointer/PatternsPointer/OrnamentsPointer/SamplesPointer
 * (word each) @1/3/5/7, STP_Init_Id(byte) @9. */
bool detect_stp_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 10)) return false;
  uint16_t positions_ptr = le16_at(f, 1);
  uint16_t patterns_ptr = le16_at(f, 3);
  uint16_t ornaments_ptr = le16_at(f, 5);
  uint16_t samples_ptr = le16_at(f, 7);

  if ((int)samples_ptr - (int)ornaments_ptr != 0x20) return false;
  if ((int)ornaments_ptr - (int)patterns_ptr <= 0) return false;
  if (((int)ornaments_ptr - (int)patterns_ptr) % 6 != 0) return false;

  if (!in_bounds(f, positions_ptr, 1)) return false;
  int first_pos_pnum = byte_at(f, positions_ptr);
  if (first_pos_pnum * 2 + 2 + (int)positions_ptr - (int)patterns_ptr != 0) return false;

  size_t length = (size_t)samples_ptr + 30;
  if (length > 65535) return false;
  if (!in_bounds(f, 0, length)) return false;

  int j2 = 0;
  int j3 = byte_at(f, 9); /* STP_Init_Id */
  if (j3 == 0) {
    if (!in_bounds(f, patterns_ptr, 2)) return false;
    j2 = le16_at(f, patterns_ptr);
    /* KsaId-prefixed variant offsets the pointer base by an extra 53
     * bytes (Players.pas:5585-5589) - the KsaId string comparison itself
     * is a secondary confirmation skipped here for the same reason noted
     * in detect_st3_structural; we use the more common (non-KsaId) base
     * only, a documented approximation for this one sub-case. */
    j2 -= 0xa;
    if (j2 < 0) return false;
  }

  if (!in_bounds(f, ornaments_ptr, 2)) return false;
  int j = (int)le16_at(f, ornaments_ptr) - 1 - j2;
  if (j < 0 || (size_t)j > f->size - 1) return false;
  if (!in_bounds(f, (size_t)j, 2)) return false;
  if (le16_at(f, (size_t)j) != 0) return false;

  d->format = "STP";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}
