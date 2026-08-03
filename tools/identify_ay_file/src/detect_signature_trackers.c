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
