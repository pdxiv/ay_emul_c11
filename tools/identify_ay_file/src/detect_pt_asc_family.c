#include "identify/detect_pt_asc_family.h"

/* ModTypes variant 2 (Players.pas:116-119): Delay@0 LoopingPosition@1
 * PatternsPointers@2(word) SamplesPointers@4(word) OrnamentsPointers@6
 * (word) Number_Of_Positions@8 Positions[]@9. Players.pas:5299-5352
 * FoundASC1, minus the final IntegrityCheck step (see detect_st_family.h). */
bool detect_asc1_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 9)) return false;
  uint16_t patterns_ptr = le16_at(f, 2);
  uint16_t samples_ptr = le16_at(f, 4);
  uint16_t ornaments_ptr = le16_at(f, 6);
  uint8_t num_positions = byte_at(f, 8);

  int j = (int)patterns_ptr - num_positions;
  if (j != 9 && j != 72) return false;
  if (!in_bounds(f, 9, num_positions)) return false;
  if (!in_bounds(f, samples_ptr, 2) || !in_bounds(f, ornaments_ptr, 2) ||
      !in_bounds(f, patterns_ptr, 2))
    return false;

  if (le16_at(f, samples_ptr) != 0x40) return false;
  if (le16_at(f, ornaments_ptr) != 0x40) return false;

  int j3 = 0;
  for (int i = 0; i < num_positions; i++) {
    uint8_t v = byte_at(f, 9 + (size_t)i);
    if (j3 < v) j3 = v;
  }
  if ((int)le16_at(f, patterns_ptr) != (j3 + 1) * 6) return false;

  if (!in_bounds(f, (size_t)ornaments_ptr + 0x40 - 2, 2)) return false;
  int p = le16_at(f, (size_t)ornaments_ptr + 0x40 - 2) + ornaments_ptr;
  while (p < (int)f->size && p < 65535 && in_bounds(f, (size_t)p, 1) &&
         (byte_at(f, (size_t)p) & 0x40) == 0)
    p += 2;
  if (p > 65534) return false;
  if (p >= (int)f->size) return false;

  d->format = "ASC";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}

/* ModTypes variant 3 (Players.pas:120-123): Delay@0 PatternsPointers@1
 * (word) SamplesPointers@3(word) OrnamentsPointers@5(word)
 * Number_Of_Positions@7 Positions[]@8. Players.pas:5354-5406 FoundASC0. */
bool detect_asc0_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 8)) return false;
  uint16_t patterns_ptr = le16_at(f, 1);
  uint16_t samples_ptr = le16_at(f, 3);
  uint16_t ornaments_ptr = le16_at(f, 5);
  uint8_t num_positions = byte_at(f, 7);

  int j = (int)patterns_ptr - num_positions;
  if (j != 8 && j != 71) return false;
  if (!in_bounds(f, 8, num_positions)) return false;
  if (!in_bounds(f, samples_ptr, 2) || !in_bounds(f, ornaments_ptr, 2) ||
      !in_bounds(f, patterns_ptr, 2))
    return false;

  if (le16_at(f, samples_ptr) != 0x40) return false;
  if (le16_at(f, ornaments_ptr) != 0x40) return false;

  int j3 = 0;
  for (int i = 0; i < num_positions; i++) {
    uint8_t v = byte_at(f, 8 + (size_t)i);
    if (j3 < v) j3 = v;
  }
  if ((int)le16_at(f, patterns_ptr) != (j3 + 1) * 6) return false;

  if (!in_bounds(f, (size_t)ornaments_ptr + 0x40 - 2, 2)) return false;
  int p = le16_at(f, (size_t)ornaments_ptr + 0x40 - 2) + ornaments_ptr;
  while (p < (int)f->size && p < 65535 && in_bounds(f, (size_t)p, 1) &&
         (byte_at(f, (size_t)p) & 0x40) == 0)
    p += 2;
  if (p > 65534) return false;
  if (p >= (int)f->size) return false;

  d->format = "ASC0";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}

/* ModTypes variant 5 (Players.pas:128-135): Delay@0 NumberOfPositions@1
 * LoopPosition@2 SamplesPointers[0..31]@3(word*32=64B)
 * OrnamentsPointers[0..15]@67(word*16=32B) PatternsPointer@99(word)
 * MusicName[0..29]@101 PositionList[]@131. Players.pas:5785-5878 FoundPT2. */
bool detect_pt2_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 132)) return false;
  uint16_t patterns_ptr = le16_at(f, 99);
  if (patterns_ptr == 0) return false;
  if (patterns_ptr >= f->size) return false;
  if (byte_at(f, (size_t)patterns_ptr - 1) != 255) return false;

  uint16_t samp0 = le16_at(f, 3); /* SamplesPointers[0] */
  uint16_t orn0 = le16_at(f, 67); /* OrnamentsPointers[0] */
  if ((int)orn0 - (int)samp0 + 2 > (int)f->size - 1) return false;
  if ((int)orn0 - (int)samp0 < 0) return false;

  if (!in_bounds(f, (size_t)orn0 - samp0, 3)) return false;
  uint32_t tag = byte_at(f, (size_t)orn0 - samp0) |
                 (uint32_t)byte_at(f, (size_t)orn0 - samp0 + 1) << 8 |
                 (uint32_t)byte_at(f, (size_t)orn0 - samp0 + 2) << 16;
  if (tag != 1) return false;

  int j = (int)le16_at(f, patterns_ptr) - samp0;
  if (j < 0 || j > (int)f->size - 1) return false;
  j -= patterns_ptr;
  if (j <= 0 || j % 6 != 2) return false;

  int j1 = 0, j2 = 0;
  while (j2 < 256 && j2 <= (int)f->size - 1 - 131) {
    uint8_t v = byte_at(f, 131 + (size_t)j2);
    if (v >= 128) break;
    if (j1 < v) j1 = v;
    j2++;
  }
  if (j / 6 != j1 + 1) return false;

  int jj = 15;
  while (jj > 0 && le16_at(f, 67 + (size_t)jj * 2) == samp0) jj--;
  int j4 = (int)le16_at(f, 67 + (size_t)jj * 2) - samp0;
  if (j4 < 0 || j4 + 2 > (int)f->size - 1) return false;
  size_t length = (size_t)j4 + byte_at(f, (size_t)j4) + 2;
  if (length > 65536) return false;
  if (length > f->size) return false;

  for (int k = 0; k < 32; k++) {
    int v = (int)le16_at(f, 3 + (size_t)k * 2) - samp0;
    if (v < 0 || v + 2 > (int)f->size - 1) return false;
  }
  for (int k = 0; k < 16; k++) {
    int v = (int)le16_at(f, 67 + (size_t)k * 2) - samp0;
    if (v < 0 || v + 2 > (int)f->size - 1) return false;
  }
  for (int k = 0; k <= j1 * 3 + 2; k++) {
    size_t off = (size_t)patterns_ptr + (size_t)k * 2;
    if (off >= f->size) return false;
    int v = (int)le16_at(f, off) - samp0;
    if (v < 0 || v > (int)f->size - 1) return false;
  }

  d->format = "PT2";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}

/* ModTypes variant 9 (Players.pas:163-170): Delay@0 NumberOfPositions@1
 * LoopPosition@2 SamplesPointers[0..15]@3(word*16=32B)
 * OrnamentsPointers[0..15]@35(word*16=32B) PatternsPointer@67(word)
 * MusicName[0..29]@69 PositionList[]@99. Players.pas:6262-6330 FoundPT1. */
bool detect_pt1_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 0x66)) return false;
  uint16_t patterns_ptr = le16_at(f, 67);
  if (patterns_ptr >= f->size) return false;

  int j = 0, j1 = 65535;
  for (int i = 0; i < 16; i++) {
    uint16_t samp = le16_at(f, 3 + (size_t)i * 2);
    uint16_t orn = le16_at(f, 35 + (size_t)i * 2);
    if (j < samp) j = samp;
    if (orn != 0 && j1 > orn) j1 = orn;
  }
  if (j1 < 0x67) return false;
  if (j < 0x67) return false;
  if (j > 65534) return false;
  if ((size_t)j > f->size) return false;
  if (!in_bounds(f, (size_t)j, 1)) return false;
  if (j + byte_at(f, (size_t)j) * 3 + 2 != j1) return false;

  j = 0;
  for (int i = 0; i < 16; i++) {
    uint16_t orn = le16_at(f, 35 + (size_t)i * 2);
    if (j < orn) j = orn;
  }
  if (j < 0x67) return false;

  size_t length = (size_t)j + 64;
  if (length > f->size) {
    length = f->size;
    if (length <= (size_t)j) return false;
  }
  if (length > 65536) return false;

  size_t k = 0x63;
  while (k <= patterns_ptr && in_bounds(f, k, 1) && byte_at(f, k) != 255) k++;
  if (k + 1 != patterns_ptr) return false;

  d->format = "PT1";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}

/* ModTypes variant 11 (Players.pas:177-178): SQT_Size,SQT_SamplesPointer,
 * SQT_OrnamentsPointer,SQT_PatternsPointer,SQT_PositionsPointer,
 * SQT_LoopPointer, all `word`, @0/2/4/6/8/10. Players.pas:6182-6260
 * FoundSQT. */
bool detect_sqt_structural(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 17)) return false;
  uint16_t samples_ptr = le16_at(f, 2);
  uint16_t ornaments_ptr = le16_at(f, 4);
  uint16_t patterns_ptr = le16_at(f, 6);
  uint16_t positions_ptr = le16_at(f, 8);
  uint16_t loop_ptr = le16_at(f, 10);

  if (samples_ptr < 10) return false;
  if (ornaments_ptr <= samples_ptr + 1) return false;
  if (patterns_ptr < ornaments_ptr) return false;
  if (positions_ptr <= patterns_ptr) return false;
  if (loop_ptr < positions_ptr) return false;

  int base = samples_ptr - 10;
  if ((int)loop_ptr - base > (int)f->size - 1 - 12) return false;

  int j1 = (int)positions_ptr - base;
  if (!in_bounds(f, (size_t)j1, 1) || byte_at(f, (size_t)j1) == 0) return false;
  int j2 = 0;
  do {
    if (j1 + 11 >= (int)f->size - 1) return false;
    for (int step = 0; step < 2; step++) {
      if (!in_bounds(f, (size_t)j1, 1)) return false;
      int v = byte_at(f, (size_t)j1) & 0x7f;
      if (j2 < v) j2 = v;
      j1 += 2;
    }
    if (!in_bounds(f, (size_t)j1, 1)) return false;
    int v = byte_at(f, (size_t)j1) & 0x7f;
    if (j2 < v) j2 = v;
    j1 += 3;
    if (!in_bounds(f, (size_t)j1, 1)) return false;
  } while (byte_at(f, (size_t)j1) != 0);

  if (!in_bounds(f, (size_t)samples_ptr - base + 2, 2)) return false;
  int check = (int)le16_at(f, (size_t)samples_ptr - base + 2) - patterns_ptr - 2;
  if (check != j2 * 2) return false;

  size_t length = (size_t)j1 + 7;
  if (!in_bounds(f, 12, 2)) return false;
  size_t off = 12;
  int j2b = le16_at(f, off);
  int orn_words = ((int)ornaments_ptr - (int)samples_ptr) / 2;
  for (int i = 0; i < orn_words; i++) {
    off += 2;
    if (!in_bounds(f, off, 2)) return false;
    int j3 = le16_at(f, off);
    if (j3 - j2b != 0x62) return false;
    j2b = j3;
  }
  int pat_words = ((int)patterns_ptr - (int)ornaments_ptr) / 2;
  for (int i = 0; i < pat_words; i++) {
    off += 2;
    if (!in_bounds(f, off, 2)) return false;
    int j3 = le16_at(f, off);
    if (j3 - j2b != 0x22) return false;
    j2b = j3;
  }

  if (!in_bounds(f, 0, length)) return false;

  d->format = "SQT";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}
