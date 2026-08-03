#include "identify/detect_fls.h"

#define FLS_MAX_PAT_E 31 /* Players.pas: FLSMaxPatE = ST1MaxPatE */

/* Players.pas:6339-6340 ValidSamOffset. */
static bool valid_sam_offset(int cur_o, int n_orns) {
  return cur_o >= 12 + n_orns * 2 && (cur_o - n_orns * 2) % 6 == 0;
}

/* Players.pas:6343-6371 ValidSamParams. */
static bool valid_sam_params(const filebuf* f, int ofs) {
  if (!in_bounds(f, (size_t)ofs, 2)) return false;
  int lp = byte_at(f, (size_t)ofs);
  if (lp < 0 || lp > 32) return false;
  int lp_len = byte_at(f, (size_t)ofs + 1);
  if (lp == 0) {
    if (lp_len < 1 || lp_len > 32) return false;
  } else {
    if (lp_len < 1 || lp_len > 33 - lp) return false;
  }
  return true;
}

/* Players.pas:6332-6544 FoundFLS (the current, faster variant), minus the
 * final IntegrityCheck step - see detect_fls.h. ModTypes variant 10
 * (Players.pas:171-176): FLS_PositionsPointer@0(word)
 * FLS_OrnamentsPointer@2(word) FLS_SamplesPointer@4(word)
 * FLS_PatternsPointers[1..N]@6, 6 bytes each (PatternA/B/C: word). */
bool detect_fls_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 52)) return false; /* Players.pas:6378-6384's size floor */

  uint16_t positions_ptr = le16_at(f, 0);
  uint16_t ornaments_ptr = le16_at(f, 2);
  uint16_t samples_ptr = le16_at(f, 4);

  int n_orns = (int)samples_ptr - (int)ornaments_ptr;
  if (n_orns < 0 || n_orns > 15 * 2 || (n_orns & 1)) return false;
  n_orns /= 2;

  int pls_a = positions_ptr;
  int n_sams = pls_a - (int)samples_ptr;
  if (n_sams <= 0 || n_sams > 16 * 4 || (n_sams & 3)) return false;
  n_sams /= 4;

  if (!in_bounds(f, 6, 2)) return false;
  int pat_a = le16_at(f, 6); /* FLS_PatternsPointers[1].PatternA */
  int n_poss = pat_a - pls_a - 1;
  if (n_poss <= 0 || n_poss > 256) return false;

  size_t need = (size_t)(2 + 2 + 2 + 6 + n_poss + 1) +
                (size_t)(2 + 2 + 32) * (size_t)n_sams +
                (size_t)(2 + 32) * (size_t)n_orns + 3;
  if (!in_bounds(f, 0, need - 1)) return false;

  int cur_o = 6;
  int max_o = 6 + n_orns * 2 + n_sams * 4;
  int max_d = (int)f->size - max_o;
  max_o += FLS_MAX_PAT_E * 6;
  int cap = (int)f->size - n_poss - 1 - 3;
  if (max_o > cap) {
    max_o = cap;
    if (max_o & 1) max_o--;
  }

  while (cur_o < max_o) {
    if (!in_bounds(f, (size_t)cur_o, 2)) return false;
    int cur_a = le16_at(f, (size_t)cur_o);
    int dif = cur_a - pls_a;
    if (dif <= 0 || dif > max_d) break;

    if (valid_sam_offset(cur_o, n_orns) && cur_o + 6 < max_o &&
        valid_sam_params(f, cur_o)) {
      if (!in_bounds(f, (size_t)cur_o + 2, 2)) return false;
      int sam_a = le16_at(f, (size_t)cur_o + 2);
      bool orn_ok;
      if (n_orns == 0) {
        orn_ok = true;
      } else {
        if (!in_bounds(f, (size_t)cur_o - 2, 2)) return false;
        orn_ok = sam_a - (int)le16_at(f, (size_t)cur_o - 2) == 32;
      }
      bool next_ok = false;
      if (n_sams == 1) {
        next_ok = pls_a - sam_a == 32 * 3;
      } else if (n_sams > 1 && valid_sam_params(f, cur_o + 4)) {
        if (!in_bounds(f, (size_t)cur_o + 6, 2)) return false;
        next_ok = (int)le16_at(f, (size_t)cur_o + 6) - sam_a == 32 * 3;
      }
      if (orn_ok && next_ok) break;
    }

    if (cur_a < pat_a) return false;
    cur_o += 2;
  }

  if (!valid_sam_offset(cur_o, n_orns)) return false;

  max_o = cur_o + n_sams * 4;
  if (max_o > cap) return false;
  max_d = (int)f->size - max_o;

  for (int i = 0; i < n_sams; i++) {
    if (!valid_sam_params(f, cur_o)) return false;
    if (!in_bounds(f, (size_t)cur_o + 2, 2)) return false;
    int cur_a = le16_at(f, (size_t)cur_o + 2);
    int dif = cur_a - pls_a;
    if (dif <= 0 || dif > max_d) return false;
    cur_o += 4;
  }

  for (int i = 0; i < n_poss; i++) {
    if (!in_bounds(f, (size_t)cur_o, 1)) return false;
    uint8_t v = byte_at(f, (size_t)cur_o);
    if (v < 1 || v > 31) return false;
    cur_o++;
  }

  if (!in_bounds(f, (size_t)cur_o, 1) || byte_at(f, (size_t)cur_o) != 0) return false;

  int j = pat_a - cur_o - 1;
  if (((int)ornaments_ptr - j) & 1) return false;
  if (((int)samples_ptr - j) & 1) return false;
  pls_a -= j;
  if (pls_a & 1) return false;

  if (!in_bounds(f, (size_t)pls_a - 2, 2)) return false;
  size_t length = (size_t)((int)le16_at(f, (size_t)pls_a - 2) + 0x60 - j);
  if ((int)length <= pls_a) return false;
  if (length > f->size) return false;

  d->format = "FLS";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}
