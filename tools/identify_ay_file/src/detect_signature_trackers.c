#include "identify/detect_signature_trackers.h"

#include <string.h>

/* Ay_Emul.fmt: [format+ STC] match1..match10=7:<string> (ten alternative
 * single-offset signatures, all anchored at offset 7 - the compiled
 * module's embedded compiler-name string). Reported as one OR'd list
 * here since Ay_Emul.fmt itself treats them as independent alternatives
 * (each matchN is its own single-pair AND-group). */
static const char* const STC_SIGS[] = {
    "SONG BY ST COMPILE", "SONG BY MB COMPILE", "SONG BY ST-COMPILE",
    "SOUND TRACKER v1.1", "S.T.FULL EDITION  ", "S.T.FULL EDITION ",
    "SOUND TRACKER v1.3", " STU SONG COMPILER", "(C) KLAV \"S_SONIC\"",
    "ZX81 Compiler A.Re"};
#define STC_SIGS_LEN (sizeof(STC_SIGS) / sizeof(STC_SIGS[0]))

bool detect_stc(const filebuf* f, detection* d) {
  for (size_t i = 0; i < STC_SIGS_LEN; i++) {
    if (str_at(f, 7, STC_SIGS[i])) {
      d->format = "STC";
      d->confidence = "definite";
      d->chips = 1;
      return true;
    }
  }
  return false;
}

/* Ay_Emul.fmt: [format+ PSC] match1=0:"PSC V1.0" match1=9:" COMPILATION OF "
 * match1=45:" BY " - three pairs under the SAME level (match1 repeated),
 * i.e. one AND-group, all three must hold. */
bool detect_psc(const filebuf* f, detection* d) {
  if (str_at(f, 0, "PSC V1.0") && str_at(f, 9, " COMPILATION OF ") &&
      str_at(f, 45, " BY ")) {
    d->format = "PSC";
    d->confidence = "definite";
    d->chips = 1;
    return true;
  }
  return false;
}

/* Ay_Emul.fmt: [format+ FTC] match1=0:"Module: " match1=50:";Fast Tracker
 * v1.00" (one AND-group, two pairs). */
bool detect_ftc(const filebuf* f, detection* d) {
  if (str_at(f, 0, "Module: ") && str_at(f, 50, ";Fast Tracker v1.00")) {
    d->format = "FTC";
    d->confidence = "definite";
    d->chips = 1;
    return true;
  }
  return false;
}

/* Ay_Emul.fmt: [format+ GTR] match=1:GTR#10; ("GTR"+newline at offset 1). */
bool detect_gtr(const filebuf* f, detection* d) {
  if (str_at(f, 1, "GTR\n")) {
    d->format = "GTR";
    d->confidence = "definite";
    d->chips = 1;
    return true;
  }
  return false;
}

/* Ay_Emul.fmt: [format+ FXM] match=0:FXSM. */
bool detect_fxm(const filebuf* f, detection* d) {
  if (str_at(f, 0, "FXSM")) {
    d->format = "FXM";
    d->confidence = "definite";
    d->chips = 1;
    return true;
  }
  return false;
}

/* Ay_Emul.fmt: [format+ PSM] match=8:psm1 (offset 8). */
bool detect_psm(const filebuf* f, detection* d) {
  if (str_at(f, 8, "psm1")) {
    d->format = "PSM";
    d->confidence = "definite";
    d->chips = 1;
    return true;
  }
  return false;
}

/* Best-effort Tier C fallback for the 4 tracker formats Players.pas's real
 * Module_Detector actually calls out of this file's six (STC, PSC, FTC,
 * GTR - see dispatch.c's top comment): FXM and PSM are deliberately
 * excluded here even though they have Ay_Emul.fmt byte signatures, because
 * Module_Detector's real if/elseif chain (Players.pas:6901-7002) never
 * calls FoundFXM/FoundPSM - confirmed by reading that function in full.
 * FXM/PSM remain reachable via Tier A (a recognised .fxm/.psm extension)
 * exactly as before; only the extensionless/unrecognised-extension
 * fallback is affected by this exclusion.
 * Unlike Players.pas's real Module_Detector, which slides a window over
 * the whole file re-running full structural checks at every offset, this
 * only re-tests the same fixed-relative-offset signatures used above,
 * anchored at every possible file position - equivalent to a plain
 * multi-anchor substring search. This reproduces Ay_Emul.fmt's signatures
 * faithfully but is NOT equivalent to Module_Detector's fuller structural
 * validation, so results from this path are reported with
 * confidence=probable. */
bool scan_whole_file_for_signature_trackers(const filebuf* f, detection* d) {
  for (size_t i = 0; i < STC_SIGS_LEN; i++) {
    size_t pos = find_bytes(f, 0, (const uint8_t*)STC_SIGS[i], strlen(STC_SIGS[i]));
    if (pos != (size_t)-1) {
      d->format = "STC";
      d->confidence = "probable";
      d->chips = 1;
      return true;
    }
  }
  size_t pos = find_bytes(f, 0, (const uint8_t*)"PSC V1.0", 8);
  if (pos != (size_t)-1 && str_at(f, pos + 9, " COMPILATION OF ") &&
      str_at(f, pos + 45, " BY ")) {
    d->format = "PSC";
    d->confidence = "probable";
    d->chips = 1;
    return true;
  }
  pos = find_bytes(f, 0, (const uint8_t*)"Module: ", 8);
  if (pos != (size_t)-1 && str_at(f, pos + 50, ";Fast Tracker v1.00")) {
    d->format = "FTC";
    d->confidence = "probable";
    d->chips = 1;
    return true;
  }
  pos = find_bytes(f, 0, (const uint8_t*)"GTR\n", 4);
  if (pos != (size_t)-1) {
    d->format = "GTR";
    d->confidence = "probable";
    d->chips = 1;
    return true;
  }
  return false;
}

/* ---- Real Module_Detector structural ports (Players.pas:4841-5173,
 * 6090-6180) - used by the true byte-by-byte sliding scan in dispatch.c
 * instead of the Ay_Emul.fmt-signature approximation above. See
 * dispatch.c's top comment: Ay_Emul.fmt's match= fields (implemented by
 * detect_stc/detect_psc/detect_ftc/detect_gtr and
 * scan_whole_file_for_signature_trackers above) turn out to be read ONLY
 * by filetypes.pas's desktop shared-mime-info XML writer, never by
 * AddFile/Module_Detector - so they were never actually equivalent to
 * what Module_Detector checks. These functions are direct ports of the
 * genuine FoundSTC/FoundFTC/FoundGTR pointer/table validation instead.
 *
 * Convention used throughout: Pascal's Readen1 (bytes remaining from the
 * current candidate offset, decremented once per Module_Detector slide
 * step before the FoundXXX call) is modelled as `(long)f->size - 1` -
 * cross-checked against detect_st_family.c's FoundST1 port, whose
 * "Readen1 уменьшено на 1" ("Readen1 reduced by 1") comment confirms this
 * mapping against its own in_bounds() floor check. Struct fields declared
 * `word` in Pascal that are assigned back into a `word`-typed local
 * (e.g. FoundGTR's `w`, `w2`, `adr`) keep 16-bit wraparound (uint16_t);
 * fields compared or subtracted in a bare boolean expression, or assigned
 * into an `integer` local, widen to a signed type with no wraparound -
 * matching FPC's own promotion rules and this codebase's existing
 * precedent (e.g. detect_st_family.c's ST3/STP pointer diffs). */

/* Players.pas:5067-5173 FoundSTC, minus the final IntegrityCheck step (see
 * detect_st_family.h's file comment) and the final Id-branch StcId/KsaId
 * CompareMem confirmation (same documented approximation as
 * detect_st_family.c's ST3/STP Id branches - the PlayerTag string table
 * isn't pulled in for this secondary confirmation only).
 * ModTypes variant 1 (Players.pas:112-115): ST_Delay@0(byte, unused here)
 * ST_PositionsPointer@1/ST_OrnamentsPointer@3/ST_PatternsPointer@5(word
 * each). */
bool detect_stc_structural(const filebuf* f, detection* d) {
  long r = (long)f->size - 1;
  if (r < 6) return false;
  if (!in_bounds(f, 0, 6)) return false;
  uint16_t positions_ptr = le16_at(f, 1);
  uint16_t ornaments_ptr = le16_at(f, 3);
  uint16_t patterns_ptr = le16_at(f, 5);

  if ((long)positions_ptr > r) return false;

  long j1 = (long)patterns_ptr - (long)ornaments_ptr;
  if (j1 <= 0) return false;
  long j2 = (long)positions_ptr - (long)ornaments_ptr;
  if (j2 == 0) return false;

  bool id = false;
  if (j2 > 0) {
    if (j2 % 0x21 != 0) return false;
  } else if (j1 % 0x21 != 0) {
    if (j1 < 55 || (j1 - 55) % 0x21 != 0) return false;
    id = true;
  }

  if (!in_bounds(f, positions_ptr, 1)) return false;
  long j = (long)byte_at(f, positions_ptr) * 2 + 3;

  if (j2 < 0) {
    if (j + j2 != 0) return false;
  } else if (j + (long)positions_ptr - (long)patterns_ptr != 0) {
    if ((long)patterns_ptr < 55 + 27 ||
        j + (long)positions_ptr - (long)patterns_ptr + 55 != 0)
      return false;
    id = true;
  }
  (void)id; /* Id-branch KsaId/StcId confirmation deliberately not ported - see above */

  long jo = (long)ornaments_ptr + 0x21;
  if (jo > 65535) return false;
  if (jo > r) return false;
  do {
    jo--;
    if (!in_bounds(f, (size_t)jo, 1) || byte_at(f, (size_t)jo) != 0) return false;
  } while (jo != ornaments_ptr);

  long jp = patterns_ptr;
  if (jp > r) return false;
  long maxw = 0;
  while (jp + 6 <= r && jp + 6 < 65536 && in_bounds(f, (size_t)jp, 1) &&
         byte_at(f, (size_t)jp) != 255) {
    jp++;
    if (!in_bounds(f, (size_t)jp, 2)) return false;
    long tmp = le16_at(f, (size_t)jp);
    if (maxw < tmp) maxw = tmp;
    jp += 2;
    if (!in_bounds(f, (size_t)jp, 2)) return false;
    tmp = le16_at(f, (size_t)jp);
    if (maxw < tmp) maxw = tmp;
    jp += 2;
    if (!in_bounds(f, (size_t)jp, 2)) return false;
    tmp = le16_at(f, (size_t)jp);
    if (maxw < tmp) maxw = tmp;
    jp += 2;
  }
  if (!in_bounds(f, (size_t)jp, 1) || byte_at(f, (size_t)jp) != 255) return false;
  if (maxw > r) return false;
  if (!in_bounds(f, (size_t)maxw - 1, 1) || byte_at(f, (size_t)maxw - 1) != 255)
    return false;

  long j1w = maxw;
  for (;;) {
    if (in_bounds(f, (size_t)j1w, 1)) {
      uint8_t v = byte_at(f, (size_t)j1w);
      if (v >= 0x83 && v <= 0x8e) j1w++;
    }
    j1w++;
    if (j1w > 65535 || j1w > r) break;
    if (!in_bounds(f, (size_t)j1w, 1) || byte_at(f, (size_t)j1w) == 255) break;
  }
  if (j1w > 65535) return false;
  if (j1w > r) return false;

  d->format = "STC";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}

/* Players.pas:4841-4920 FoundGTR, minus the final IntegrityCheck step.
 * ModTypes variant 12 (Players.pas:179-190): GTR_Delay@0(byte, unused)
 * GTR_ID@1(4 chars, unused) GTR_Address@5(word) GTR_Name@7(32 chars,
 * unused) GTR_SamplesPointers[0..14]@39(word*15) GTR_OrnamentsPointers
 * [0..15]@69(word*16) GTR_PatternsPointers[0..31]@101(3 words each:
 * PatternA/B/C) GTR_NumberOfPositions@293(byte) GTR_LoopPosition@294
 * (byte) GTR_Positions@295. Pascal's own code deliberately walks past the
 * end of GTR_OrnamentsPointers and GTR_SamplesPointers into the next
 * field, relying on packed-record contiguity (index 16 of the 16-entry
 * OrnamentsPointers array lands exactly on PatternsPointers[0].PatternA
 * at offset 101, since 69+16*2=101; similarly index 15..30 of
 * SamplesPointers[0..14]+OrnamentsPointers[0..15] together span
 * 39..39+30*2=99) - reproduced here as flat byte-offset arithmetic
 * instead of an actual array overrun. */
bool detect_gtr_structural(const filebuf* f, detection* d) {
  long r = (long)f->size - 1;
  if (r < 296) return false;
  if (!in_bounds(f, 0, 296)) return false;

  uint16_t adr = le16_at(f, 5);
  if (!in_bounds(f, 101, 2)) return false;
  uint16_t patA0 = le16_at(f, 101); /* GTR_PatternsPointers[0].PatternA */
  uint16_t w = (uint16_t)(patA0 - adr);
  if ((long)w > r) return false;
  uint8_t num_pos = byte_at(f, 293);
  if ((long)w != (long)num_pos + 295) return false;
  if (byte_at(f, 294) >= num_pos) return false;

  for (int j = 0; j <= 13; j++) {
    if (!in_bounds(f, 39 + (size_t)(j + 1) * 2, 2)) return false;
    long j1 = (long)le16_at(f, 39 + (size_t)(j + 1) * 2) -
              (long)le16_at(f, 39 + (size_t)j * 2);
    if (j1 < 6) return false;
    if ((j1 - 2) % 4 != 0) return false;
  }

  /* Ornaments walk: offsets 69,71,...,99 (index 0..15), each step compared
   * against the previous. */
  uint16_t ww = le16_at(f, 69);
  for (int i = 1; i <= 15; i++) {
    size_t off = 69 + (size_t)i * 2;
    if (!in_bounds(f, off, 2)) return false;
    uint16_t w2 = le16_at(f, off);
    if ((long)w2 - (long)ww < 3) return false;
    ww = w2;
  }
  /* Patterns walk resumes at offset 101 (flat index 16) as an unchecked
   * baseline reset - Pascal does not compare across the Ornaments/
   * Patterns array boundary - then continues checking flat indices
   * 17..111 (offsets 103..291), covering all 32*3 pattern words. */
  ww = patA0;
  for (int i = 17; i <= 111; i++) {
    size_t off = 69 + (size_t)i * 2;
    if (!in_bounds(f, off, 2)) return false;
    uint16_t w2 = le16_at(f, off);
    if ((long)w2 - (long)ww < 3) return false;
    ww = w2;
  }

  /* Samples+Ornaments flat word walk (offsets 39..99, k=0..30). */
  for (int k = 0; k <= 30; k++) {
    size_t off = 39 + (size_t)k * 2;
    if (!in_bounds(f, off, 2)) return false;
    uint16_t wk = (uint16_t)(le16_at(f, off) - adr);
    if ((long)wk >= r) return false;
    if (!in_bounds(f, (size_t)wk, 2)) return false;
    if (byte_at(f, (size_t)wk) >= byte_at(f, (size_t)wk + 1)) return false;
  }

  d->format = "GTR";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}

/* Players.pas:6090-6180 FoundFTC, minus the final IntegrityCheck step.
 * ModTypes variant 8 (Players.pas:151-162): FTC_MusicName@0(69 chars,
 * unused) FTC_Delay@69(byte,unused) FTC_Loop_Position@70(byte)
 * FTC_Slack@71(4-byte integer, unused padding) FTC_PatternsPointer@75
 * (word) FTC_Slack2@77(5 bytes, unused) FTC_SamplesPointers[0..31]@82
 * (word*32) FTC_OrnamentsPointers[0..32]@146(word*33)
 * FTC_Positions@212(=$d4; byte Pattern/shortint Transposition pairs). */
bool detect_ftc_structural(const filebuf* f, detection* d) {
  long r = (long)f->size - 1;
  if (r < 0xd4 + 3) return false;
  if (!in_bounds(f, 75, 2)) return false;
  long patterns_ptr = le16_at(f, 75);
  if (patterns_ptr >= r) return false;
  if (!in_bounds(f, 146, 2) || !in_bounds(f, 82, 2)) return false;
  if ((long)le16_at(f, 146) <= (long)le16_at(f, 82)) return false;

  long j1 = 0xd4;
  long maxpat = 0;
  while (j1 <= r && j1 < 0x1d4) {
    if (!in_bounds(f, (size_t)j1, 1)) return false;
    uint8_t v = byte_at(f, (size_t)j1);
    if (v >= 128) break;
    if (maxpat < v) maxpat = v;
    j1 += 2;
  }
  if (j1 >= 0x1d4) return false;
  if (j1 > r) return false;
  if (patterns_ptr <= j1) return false;
  if (!in_bounds(f, 70, 1)) return false;
  if ((long)byte_at(f, 70) >= (j1 - 0xd4) / 2) return false;

  if (!in_bounds(f, (size_t)patterns_ptr, 2)) return false;
  long address = le16_at(f, (size_t)patterns_ptr);
  address -= (maxpat + 1) * 6 + patterns_ptr + 2;
  if (address < 0) return false;

  if (!in_bounds(f, 82, 2)) return false;
  long samp0 = le16_at(f, 82);
  if (samp0 - address >= r) return false;
  if (patterns_ptr >= samp0 - address) return false;
  if (!in_bounds(f, 146, 2)) return false;
  long orn0 = le16_at(f, 146);
  if (orn0 - address >= r) return false;
  (void)orn0;

  long omin = 65535, omax = 0;
  for (int j2 = 0; j2 <= 32; j2++) {
    if (!in_bounds(f, 146 + (size_t)j2 * 2, 2)) return false;
    long v = le16_at(f, 146 + (size_t)j2 * 2);
    if (omin > v) omin = v;
    if (omax < v) omax = v;
  }
  if (omin - address > 65535) return false;
  if (omin - address >= r) return false;
  if (omax - address > 65533) return false;
  if (omax - address >= r) return false;

  long smax = 0;
  for (int j2 = 0; j2 <= 31; j2++) {
    if (!in_bounds(f, 82 + (size_t)j2 * 2, 2)) return false;
    long v = le16_at(f, 82 + (size_t)j2 * 2);
    if (smax < v) smax = v;
  }
  if (smax - address <= patterns_ptr) return false;
  if (smax - address > 65533) return false;
  if (smax - address >= r) return false;
  if (!in_bounds(f, (size_t)(smax - address) + 2, 1)) return false;
  long tail1 = byte_at(f, (size_t)(smax - address) + 2);
  if (smax + 3 + (tail1 + 1) * 5 != omin) return false;

  if (!in_bounds(f, (size_t)(omax - address) + 2, 1)) return false;
  long tail2 = byte_at(f, (size_t)(omax - address) + 2);
  long length = omax + 3 + (tail2 + 1) * 2 - address;
  if (length > 65536) return false;
  if (length > r + 1) return false;
  if (length < patterns_ptr) return false;

  d->format = "FTC";
  d->confidence = "probable";
  d->chips = 1;
  return true;
}
