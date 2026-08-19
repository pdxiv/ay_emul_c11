#include "identify/detect_pt3.h"

#include <stdio.h>

/* Ay_Emul.fmt: [format+ PT3] match1=0:"ProTracker 3." match1=14:" compilation
 * of " match1=62:" by " (alternative 1) OR match2=0:"Vortex Tracker II 1.0
 * module: " match2=62:" by " (alternative 2) - two independent AND-groups,
 * either one sufficient (OR). ModTypes' PT3 variant (Players.pas:136:
 * `PT3_MusicName: array[0..$62] of char`) is a flat 0-based byte array
 * directly overlaying the file's first 99 bytes, so all these offsets are
 * plain file offsets, not 1-based Pascal string indices.
 * Version digit: PT3_MusicName[13] (Players.pas:2580-2581) - a single
 * ASCII digit '0'..'9' embedded in the "ProTracker 3.<version>" text;
 * default is 6 when absent (Players.pas:2582, not shown above but implied
 * by the `else` branch leaving PLConsts[n].Version at its prior value,
 * which PrepareToPlay initialises to 6). Turbosound marker: byte at offset
 * 98 (Players.pas:2662, `Ord(ZRAM.PT3_MusicName[98])`), only meaningful
 * when version>=7 (Players.pas:2660); a value other than $20 (space)
 * enables dual-AY Turbosound mode. TonTableId: offset 99 (Players.pas:
 * around PT3_TonTableId reads, cross-checked against engine/src/pt3_file.c
 * line 80's `f->data[99]`, already oracle-validated). */
bool detect_pt3(const filebuf* f, detection* d) {
  bool alt1 = str_at(f, 0, "ProTracker 3.") && str_at(f, 14, " compilation of ") &&
              str_at(f, 62, " by ");
  bool alt2 = str_at(f, 0, "Vortex Tracker II 1.0 module: ") && str_at(f, 62, " by ");
  if (!alt1 && !alt2) return false;
  d->format = "PT3";
  d->confidence = "definite";
  d->version = 6;
  if (in_bounds(f, 13, 1)) {
    uint8_t c = byte_at(f, 13);
    if (c >= '0' && c <= '9') d->version = c - '0';
  }
  d->chips = 1;
  d->turbo_sound = 0;
  if (d->version >= 7 && in_bounds(f, 98, 1) && byte_at(f, 98) != 0x20) {
    d->turbo_sound = 1;
    d->chips = 2;
  }
  if (in_bounds(f, 99, 1)) {
    d->extra_key = "tone_table_id";
    snprintf(d->extra_val, sizeof(d->extra_val), "%d", byte_at(f, 99));
  }
  return true;
}

/* Players.pas:5880-5967 FoundPT3(DetectAdr), minus the final
 * IntegrityCheck step. ModTypes variant 6 (Players.pas:136-144):
 * PT3_MusicName@0(99 chars) PT3_TonTableId@99(byte) PT3_Delay@100(byte,
 * unused) PT3_NumberOfPositions@101(byte,unused-for-detection)
 * PT3_LoopPosition@102(byte,unused) PT3_PatternsPointer@103(word)
 * PT3_SamplesPointers[0..31]@105(word*32) PT3_OrnamentsPointers[0..15]
 * @169(word*16) PT3_PositionList@201(byte array). DetectAdr selects
 * whether pointers are relative to PT3_SamplesPointers[0] (loader-linked
 * files) or absolute-from-0 (PT 3.1's empty-but-nonzero sample 0
 * convention) - both variants are tried by the sliding scan, matching
 * Players.pas:6949-6960's two back-to-back FoundPT3(False,...)/
 * FoundPT3(True,...) calls. All locals in the original are `integer`
 * (signed), so no word-wraparound subtlety applies here (see
 * detect_signature_trackers.c's file comment for the general convention
 * used across all these structural ports). */
bool detect_pt3_structural(const filebuf* f, bool detect_adr, detection* d) {
  long r = (long)f->size - 1;
  if (r < 202) return false;

  long adr;
  if (detect_adr) {
    if (!in_bounds(f, 105, 2)) return false;
    adr = le16_at(f, 105);
  } else {
    adr = 0;
  }

  if (!in_bounds(f, 103, 2)) return false;
  long j4 = (long)le16_at(f, 103) - adr;
  if (j4 < 202 || j4 > r) return false;
  if (!in_bounds(f, (size_t)j4 - 1, 1) || byte_at(f, (size_t)j4 - 1) != 255) return false;

  if (!in_bounds(f, 169, 2)) return false;
  long j = (long)le16_at(f, 169) - adr;
  if (j < 202 || j + 2 > r) return false;

  /* move(Index[j], j, 3): reinterpret the 3 bytes at offset j as a
   * 24-bit little-endian value (the top byte of Pascal's 4-byte integer
   * is left as whatever it already was - always 0 here, since j was just
   * assigned a small non-negative pointer difference). */
  if (!in_bounds(f, (size_t)j, 3)) return false;
  long j3 = (long)byte_at(f, (size_t)j) | (long)byte_at(f, (size_t)j + 1) << 8 |
            (long)byte_at(f, (size_t)j + 2) << 16;
  if (j3 != 256) return false;

  long j5 = 0, j1 = 0;
  for (;;) {
    if (j5 >= 256) break;
    if (j5 + 201 > r) return false;
    if (!in_bounds(f, 201 + (size_t)j5, 1)) return false;
    long j2 = byte_at(f, 201 + (size_t)j5);
    if (j2 == 255) break;
    if (j2 % 3 != 0) return false;
    if (j1 < j2) j1 = j2;
    j5++;
  }

  long jmin = 65535;
  for (long j2 = 0; j2 <= j1 + 2; j2++) {
    if (j4 + j2 * 2 >= r) return false;
    if (!in_bounds(f, (size_t)(j4 + j2 * 2), 2)) return false;
    long j6 = (long)le16_at(f, (size_t)(j4 + j2 * 2)) - adr;
    if (j6 < 202 || j6 > r) return false;
    if (jmin > j6) jmin = j6;
  }

  long jd = jmin - j4;
  if (jd <= 0) return false;
  if (jd % 6 != 0) return false;
  if (jd / 6 != j1 / 3 + 1) return false;

  long jo = 15;
  while (jo > 0) {
    if (!in_bounds(f, 169 + (size_t)jo * 2, 2)) return false;
    if ((long)le16_at(f, 169 + (size_t)jo * 2) != adr) break;
    jo--;
  }
  if (!in_bounds(f, 169 + (size_t)jo * 2, 2)) return false;
  long jorn = (long)le16_at(f, 169 + (size_t)jo * 2) - adr;
  if (jorn < 202 || jorn + 2 > r) return false;

  if (!in_bounds(f, (size_t)jorn + 1, 1)) return false;
  long length = jorn + (long)byte_at(f, (size_t)jorn + 1) + 2;
  if (length > r + 1) return false;

  d->format = "PT3";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}

bool scan_whole_file_for_pt3(const filebuf* f, detection* d) {
  size_t pos = find_bytes(f, 0, (const uint8_t*)"ProTracker 3.", 13);
  if (pos != (size_t)-1 && str_at(f, pos + 14, " compilation of ") &&
      str_at(f, pos + 62, " by ")) {
    d->format = "PT3";
    d->confidence = "probable";
    d->chips = 1;
    return true;
  }
  pos = find_bytes(f, 0, (const uint8_t*)"Vortex Tracker II 1.0 module: ", 31);
  if (pos != (size_t)-1 && str_at(f, pos + 62, " by ")) {
    d->format = "PT3";
    d->confidence = "probable";
    d->chips = 1;
    return true;
  }
  return false;
}
