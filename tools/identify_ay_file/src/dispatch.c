/* Top-level dispatch, mirroring Players.pas AddFile's Tier A/B/C exactly.
 *
 * 1. ay_emul/Players.pas's AddFile (~line 8047 onward) is the real,
 *    top-level dispatcher. It runs in three tiers, in this exact order:
 *
 *      Tier A - extension lookup (GetFileTypeFromFNExt, filetypes.pas,
 *      driven by the [fnext ...] sections of Ay_Emul.fmt): if the file's
 *      extension maps to exactly one format, that format is used directly
 *      with NO byte-content verification at all (Players.pas:8100-8103:
 *      `else if IsAYChipFileType(extftype) then added := Add(extftype,0,-1)`)
 *      - i.e. the real program trusts a recognised, unambiguous extension
 *      outright.
 *
 *      Tier B - a small hand-written special case (Players.pas:8104-8114)
 *      for the three extensions that map to MORE than one format
 *      (".ay" -> AY or AYAMAD, ".ym" -> YM/YM2/YM3/YM3b/YM5/YM6, ".psg" ->
 *      PSG or EPSG): for these, Pascal does NOT re-check magic bytes either
 *      - it just always resolves ".ay"->OpenAYFile, ".ym"->FT.YM, ".psg"->
 *      FT.PSG, leaving the *real* sub-variant to be discovered later, when
 *      the file is actually opened/parsed for playback.
 *
 *      Tier C - content sniffing (Players.pas:8115-8146, the `else if
 *      Detect then` block), used ONLY when the extension is completely
 *      unrecognised (or absent). This checks, in order: "ZXAY" + TypeID;
 *      "PSG"+#26/"EPSG"; the 4-byte YM2!/YM3!/YM3b/YM5!/YM6! prefixes; the
 *      VTX 2-letter+version-byte pattern; "-lh5-" at offset 2. If NONE of
 *      those match, it falls through to Module_Detector (Players.pas:
 *      6830-7024), which slides a window over the *entire* file calling,
 *      in this exact order, FoundST1, FoundST3, FoundSTC, FoundASC1,
 *      FoundASC0, FoundSTF, FoundSTP, FoundPT2, FoundPT3 (x2, an address-
 *      detection variant), FoundPSC (x2, ditto), FoundFTC, FoundPT1,
 *      FoundGTR, FoundSQT, FoundFLS - first match wins. Note FXM and PSM
 *      (which DO have Ay_Emul.fmt byte signatures) are NOT part of this
 *      chain - confirmed by reading Module_Detector's body in full; they
 *      are only ever reached via Tier A's extension trust, never via
 *      content-sniffing in the real program.
 *
 *      This tool reproduces Tier A and B exactly, reproduces every Tier C
 *      magic check exactly, and reproduces Module_Detector's *entire*
 *      format list for extensionless input (migration_debt.yaml
 *      MIG-0023) as a genuine byte-by-byte sliding scan
 *      (scan_whole_file_module_detector below), re-running all seventeen
 *      FoundXXX checks at every candidate offset in Pascal's exact order,
 *      same as Module_Detector's own `repeat Inc(F_Offset) ... until
 *      May_Quit or (F_Offset >= FilSiz)` loop (Players.pas:6835-7017).
 *      Note this corrects an earlier misconception in this codebase: an
 *      older version of this tool substituted a direct Ay_Emul.fmt-
 *      signature substring search for STC/PSC/FTC/PT3/GTR, believing it
 *      "equivalent in practice" to Module_Detector's real FoundSTC/
 *      FoundPSC/FoundFTC/FoundPT3/FoundGTR structural checks. Tracing
 *      every reference to Ay_Emul.fmt's `match=` fields through
 *      filetypes.pas showed they are consumed ONLY by its desktop
 *      shared-mime-info XML writer (Rewrite/Writeln calls building
 *      Ay_Emul.xml for OS file-manager registration) - `grep`ing all of
 *      Players.pas confirms `formatParams`/`.matches` is never referenced
 *      there at all. So those signatures were never actually part of the
 *      program's own file-identification logic in any tier; real
 *      Module_Detector identification for all seventeen formats always
 *      goes through genuine pointer/table-following FoundXXX functions,
 *      now all ported (detect_st_family.c, detect_pt_asc_family.c,
 *      detect_stf.c, detect_fls.c, detect_signature_trackers.c's
 *      detect_stc_structural/detect_gtr_structural/detect_ftc_structural,
 *      detect_pt3.c's detect_pt3_structural, detect_pt_asc_family.c's
 *      detect_psc_structural). The old substring-search functions
 *      (scan_whole_file_for_signature_trackers, scan_whole_file_for_pt3)
 *      are kept only for reference/tests, no longer called from here.
 *
 *      The final `IntegrityCheck` confirmation step (a LoadTrackerModule
 *      + GetTimeXXX call computing a playable duration, rejecting a
 *      structural match if it comes back zero) is now also performed for
 *      ALL SEVENTEEN formats - via integrity_check.c, which links against
 *      engine/libayengine.a and reuses its already oracle-validated
 *      per-format GetTimeXXX ports (migration_debt.yaml MIG-0023b/
 *      MIG-0101/MIG-0103/MIG-0104) rather than re-deriving the same
 *      Pascal logic a second time independently. 12 formats have their
 *      own engine/ port (STC, ASC, ASC0, STP, PT2, PT3, PSC, FTC, PT1,
 *      GTR, FLS, SQT); ST1/ST3/STF have none of their own but reuse
 *      STC's/STP's via a small converter (st_convert.h/MIG-0105),
 *      exactly mirroring LoadTrackerModule's own real dispatch
 *      (Players.pas:2547-2558: ST1/ST3 convert to STC's layout, STF
 *      converts to STP's). confidence=definite once confirmed, matching
 *      Pascal, which never reports a match that failed IntegrityCheck at
 *      all. IMPORTANT: unlike the other 14, ST1/ST3/STF have NOT been
 *      oracle-diff-validated against a real file - none exists anywhere
 *      in this repo's test corpus - see st_convert.h's file comment and
 *      migration_debt.yaml MIG-0105 for the full caveat.
 *
 * 2. ay_emul/Ay_Emul.fmt is the authoritative, data-driven source of every
 *    format's canonical name, extension(s) and (where present) magic-byte
 *    signature; loaded at runtime by filetypes.pas's load_formats via an
 *    embedded RCDATA resource (see Ay_Emul.lpi). It is quoted verbatim in
 *    the per-detector source comments wherever it drives a decision.
 *
 * Explicitly NOT implemented (by design, per the task's scope): playback,
 * emulation, decompression of file bodies (only signature bytes are
 * inspected - e.g. LHA/ICE-compressed payloads are reported as
 * compressed=lha/ice without being decompressed), and format conversion.
 * Every detector also skips the final `IntegrityCheck` confirmation step
 * (a LoadTrackerModule + GetTimeXXX call computing a playable duration) -
 * see detect_st_family.h's file comment.
 */
#include "identify/dispatch.h"

#include "identify/detect_container.h"
#include "identify/detect_fls.h"
#include "identify/detect_pt3.h"
#include "identify/detect_pt_asc_family.h"
#include "identify/detect_signature_trackers.h"
#include "identify/detect_sndh.h"
#include "identify/detect_st_family.h"
#include "identify/detect_stf.h"
#include "identify/detect_vtx.h"
#include "identify/detect_ym.h"
#include "identify/integrity_check.h"

#include <ctype.h>
#include <string.h>

/* Ay_Emul.fmt's [fnext ...] sections and Players.pas:8073-8114's
 * GetFileTypeFromFNExt + ambiguous-extension special case. Formats
 * reached ONLY by extension (no Ay_Emul.fmt match= signature at all):
 * OUT, TS. The ten formerly-signature-less trackers (ST1, ST3, ASC, ASC0,
 * STF, STP, FLS, PT1, PT2, SQT) now also have a real structural detector
 * (see run_named_detector), but ONLY for Tier C (extensionless) input -
 * Tier A never runs them, matching Players.pas exactly (see this file's
 * top comment). */
typedef struct {
  const char* ext;   /* without leading dot, lowercase */
  const char* format; /* NULL => ambiguous, handled specially below */
} ext_entry;

static const ext_entry EXT_TABLE[] = {
    {"out", "OUT"},   {"zxay", "ZXAY"}, {"aym", "AYM"},   {"st1", "ST1"},
    {"stc", "STC"},   {"zxs", "STC"},   {"st3", "ST3"},   {"asc", "ASC"},
    {"as0", "ASC0"},  {"stf", "STF"},   {"stp", "STP"},   {"psc", "PSC"},
    {"fls", "FLS"},   {"ftc", "FTC"},   {"pt1", "PT1"},   {"pt2", "PT2"},
    {"pt3", "PT3"},   {"sqt", "SQT"},   {"gtr", "GTR"},   {"fxm", "FXM"},
    {"psm", "PSM"},   {"vtx", "VTX"},   {"ts", "TS"},     {"sndh", "SNDH"},
    {"snd", "SNDH"},  {"ay", NULL} /* ambiguous: AY or AYAMAD */,
    {"ym", NULL} /* ambiguous: YM2/YM3/YM3b/YM5/YM6 */,
    {"psg", NULL} /* ambiguous: PSG or EPSG */,
};
#define EXT_TABLE_LEN (sizeof(EXT_TABLE) / sizeof(EXT_TABLE[0]))

static const char* lookup_ext_format(const char* ext, bool* ambiguous) {
  *ambiguous = false;
  for (size_t i = 0; i < EXT_TABLE_LEN; i++) {
    if (strcmp(EXT_TABLE[i].ext, ext) == 0) {
      if (EXT_TABLE[i].format == NULL) *ambiguous = true;
      return EXT_TABLE[i].format;
    }
  }
  return NULL;
}

/* Formats whose Tier A/B detector checks genuine STRUCTURAL integrity
 * (a truncated header, a bad revision byte, a non-zero reserved field) -
 * i.e. formats where the *file itself* is presumed to be really broken
 * if run_named_detector() returns false. Used only to decide whether an
 * extension-trusted Tier A/B result that fails its own detector should be
 * flagged malformed=yes.
 *
 * Deliberately EXCLUDES STC/PSC/FTC/GTR/PT3/FXM/PSM even though they have
 * a run_named_detector() entry: their Ay_Emul.fmt signature is a
 * *compiler-stamp text* fingerprint (e.g. PT3's "ProTracker 3.X
 * compilation of..."), used by real Pascal only for Tier C content-
 * sniffing/OS mime-type registration - never checked by the real Tier A
 * loader at all. A real-world corpus run turned up hundreds of
 * legitimate files (e.g. FTC modules compiled with Fast Tracker v1.08
 * rather than the v1.00 the fixed signature expects, PT3 modules resaved
 * without their identifying text, STC files stamped by compiler tools
 * Ay_Emul.fmt's ten-entry list doesn't happen to cover) that would have
 * been wrongly flagged malformed=yes here - a signature mismatch for
 * these seven formats means only "we couldn't independently confirm the
 * sub-variant," not "the file is broken," so it now leaves
 * malformed=no/confidence=probable instead.
 *
 * Also deliberately does NOT include the ten structural-only trackers
 * (ST1/ST3/ASC/ASC0/STF/STP/PT1/PT2/SQT/FLS): Tier A never runs their
 * detectors at all (see this file's top comment), so there is nothing to
 * fail. */
static bool format_has_structural_signature(const char* format) {
  static const char* const names[] = {"AY", "AYAMAD", "AYM", "PSG", "YM", "VTX", "SNDH"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (strcmp(format, names[i]) == 0) return true;
  return false;
}

/* Runs one specific detector by canonical format name, for the extension-
 * trusted Tier A/B path's "minimum safe parsing" of format-specific
 * properties (version/turbosound/digidrum/chip_type/etc.) - i.e. the same
 * detectors Tier C uses, just called unconditionally (without requiring
 * their own signature to match) since Tier A/B's job is only to fill in
 * properties for a format ALREADY chosen by extension, not to reconfirm
 * the format itself. If the detector's own signature also happens to
 * match, confidence upgrades from "probable" to "definite" - a strict
 * improvement, not a behaviour change. Only formats with a REAL Tier
 * A/B-reachable signature are wired here (see format_has_content_detector);
 * the ten structural-only trackers are Tier-C-only and handled solely in
 * identify()'s fallback chain below. */
static bool run_named_detector(const filebuf* f, const char* format, detection* d) {
  if (strcmp(format, "AY") == 0 || strcmp(format, "AYAMAD") == 0)
    return detect_ay_container(f, d);
  if (strcmp(format, "AYM") == 0) return detect_aym(f, d);
  if (strcmp(format, "PSG") == 0) return detect_psg(f, d);
  if (strcmp(format, "YM") == 0) return detect_ym(f, d);
  if (strcmp(format, "VTX") == 0) return detect_vtx(f, d);
  if (strcmp(format, "PT3") == 0) return detect_pt3(f, d);
  if (strcmp(format, "STC") == 0) return detect_stc(f, d);
  if (strcmp(format, "PSC") == 0) return detect_psc(f, d);
  if (strcmp(format, "FTC") == 0) return detect_ftc(f, d);
  if (strcmp(format, "GTR") == 0) return detect_gtr(f, d);
  if (strcmp(format, "FXM") == 0) return detect_fxm(f, d);
  if (strcmp(format, "PSM") == 0) return detect_psm(f, d);
  if (strcmp(format, "SNDH") == 0) return detect_sndh(f, d);
  return false;
}

/* Tier C Module_Detector fallback: a real byte-by-byte sliding scan over
 * the whole file, re-running every structural detector at every offset,
 * in Players.pas's exact if/elseif order (Players.pas:6901-7002:
 * FoundST1, FoundST3, FoundSTC, FoundASC1, FoundASC0, FoundSTF, FoundSTP,
 * FoundPT2, FoundPT3(False), FoundPT3(True), FoundPSC(False),
 * FoundPSC(True), FoundFTC, FoundPT1, FoundGTR, FoundSQT, FoundFLS -
 * first match wins), matching Module_Detector's own `repeat Inc(F_Offset)
 * ... until May_Quit or (F_Offset >= FilSiz)` loop (Players.pas:6835-7017).
 *
 * Each detector is written to treat offset 0 of the filebuf it's given as
 * the candidate window start (mirroring Pascal's F_Frame := @F_Buffer
 * [F_Index], a pointer into the buffer at the current slide offset) - so
 * sliding is implemented here by handing each detector a "windowed" view
 * of the real buffer (same backing memory, `data` advanced by `base`,
 * `size` shrunk to match) rather than threading a base-offset parameter
 * through every detector's internal offset arithmetic. This is a pure
 * reframing, not a behavioural approximation: it is exactly Pascal's own
 * F_Frame indirection, just expressed as a pointer+length pair instead of
 * a live pointer into a rolling double-buffer.
 *
 * Confidence stays "probable" throughout (never "definite"): the one
 * remaining documented approximation is that every detector still omits
 * the final IntegrityCheck confirmation step (see detect_st_family.h's
 * file comment) - migration_debt.yaml MIG-0023. */
/* If `matched`, `d` already holds the structural guess - confirm it via
 * engine/'s oracle-validated GetTimeXXX port (migration_debt.yaml
 * MIG-0023b), exactly mirroring Pascal's own FoundXXX tail (`if
 * IntegrityCheck then ... if TimeLength = 0 then exit`): a structural
 * match alone is provisional, only a nonzero computed duration makes it
 * real. On confirmation, upgrades confidence to "definite" (Pascal never
 * reports a Module_Detector match that failed IntegrityCheck at all) and
 * signals the caller to accept it; on failure, resets `d` back to a
 * clean slate so the next detector tried at this same offset doesn't
 * inherit a rejected candidate's leftover fields. */
static bool confirm(bool matched, detection* d, bool (*check)(const uint8_t*, size_t),
                     const filebuf* w) {
  if (!matched) return false;
  if (check(w->data, w->size)) {
    d->confidence = "definite";
    return true;
  }
  detection_init(d);
  return false;
}

static bool confirm_asc(bool matched, detection* d, bool is_asc0, const filebuf* w) {
  if (!matched) return false;
  if (integrity_check_asc(w->data, w->size, is_asc0)) {
    d->confidence = "definite";
    return true;
  }
  detection_init(d);
  return false;
}

static bool scan_whole_file_module_detector(const filebuf* f, detection* d) {
  for (size_t base = 0; base < f->size; base++) {
    filebuf w = {f->data + base, f->size - base, 0};
    /* ST1/ST3/STF: no engine/ port exists for these formats themselves,
     * but LoadTrackerModule converts them to STC's/STP's own layout at
     * load time in the real program too (Players.pas:2547-2558) - see
     * st_convert.h - so their IntegrityCheck reuses the exact same
     * stc_file_load/stp_file_load via a conversion step first. Unlike
     * every other confirm() call here, these three have NOT been
     * oracle-diff-validated against a real file (none exists anywhere in
     * this repo) - see st_convert.h's file comment and
     * migration_debt.yaml for the caveat. */
    if (confirm(detect_st1_structural(&w, d), d, integrity_check_st1, &w)) return true;
    if (confirm(detect_st3_structural(&w, d), d, integrity_check_st3, &w)) return true;
    if (confirm(detect_stc_structural(&w, d), d, integrity_check_stc, &w)) return true;
    if (confirm_asc(detect_asc1_structural(&w, d), d, false, &w)) return true;
    if (confirm_asc(detect_asc0_structural(&w, d), d, true, &w)) return true;
    if (confirm(detect_stf_structural(&w, d), d, integrity_check_stf, &w)) return true;
    if (confirm(detect_stp_structural(&w, d), d, integrity_check_stp, &w)) return true;
    if (confirm(detect_pt2_structural(&w, d), d, integrity_check_pt2, &w)) return true;
    if (confirm(detect_pt3_structural(&w, false, d), d, integrity_check_pt3, &w)) return true;
    if (confirm(detect_pt3_structural(&w, true, d), d, integrity_check_pt3, &w)) return true;
    if (confirm(detect_psc_structural(&w, false, d), d, integrity_check_psc, &w)) return true;
    if (confirm(detect_psc_structural(&w, true, d), d, integrity_check_psc, &w)) return true;
    if (confirm(detect_ftc_structural(&w, d), d, integrity_check_ftc, &w)) return true;
    if (confirm(detect_pt1_structural(&w, d), d, integrity_check_pt1, &w)) return true;
    if (confirm(detect_gtr_structural(&w, d), d, integrity_check_gtr, &w)) return true;
    if (confirm(detect_sqt_structural(&w, d), d, integrity_check_sqt, &w)) return true;
    if (confirm(detect_fls_structural(&w, d), d, integrity_check_fls, &w)) return true;
  }
  return false;
}

static void to_lower_ascii(char* s) {
  for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static const char* file_ext(const char* path) {
  const char* slash = strrchr(path, '/');
  const char* base = slash ? slash + 1 : path;
  const char* dot = strrchr(base, '.');
  if (!dot || dot == base) return "";
  return dot + 1;
}

void identify(const filebuf* f, const char* path, detection* d) {
  detection_init(d);
  char ext[32];
  const char* raw_ext = file_ext(path);
  size_t extlen = strlen(raw_ext);
  if (extlen >= sizeof(ext)) extlen = sizeof(ext) - 1;
  memcpy(ext, raw_ext, extlen);
  ext[extlen] = '\0';
  to_lower_ascii(ext);

  bool ambiguous = false;
  const char* fmt = ext[0] ? lookup_ext_format(ext, &ambiguous) : NULL;

  if (fmt != NULL) {
    /* Tier A: unambiguous extension - Pascal trusts it outright
     * (Players.pas:8100-8103). We additionally run the matching detector
     * (if one exists at this tier) to fill in format-specific properties. */
    d->format = fmt;
    d->confidence = "probable";
    d->chips = (strcmp(fmt, "TS") == 0) ? 2 : d->chips; /* TS = Turbo Sound container, always dual-chip by construction */
    if (!run_named_detector(f, fmt, d)) {
      d->format = fmt; /* run_named_detector may have left format=unknown on failure */
      d->confidence = "probable";
      if (format_has_structural_signature(fmt)) {
        d->malformed = true;
        d->malformed_reason =
            "extension is trusted per Pascal's dispatch, but content does "
            "not match this format's known signature";
      }
    }
    return;
  }

  if (ambiguous) {
    /* Tier B (Players.pas:8104-8114): ".ay"/".ym"/".psg" - resolved by a
     * hand-written extension check, NOT by content, in the real program.
     * We still peek content to report the real sub-variant/properties,
     * since Pascal itself discovers that once OpenAYFile/the YM or PSG
     * loader actually parses the header - i.e. "minimum safe parsing" of
     * a property Pascal derives after partially loading the file. */
    if (strcmp(ext, "ay") == 0) {
      if (!detect_ay_container(f, d)) {
        d->format = "AY";
        d->confidence = "probable";
        d->malformed = true;
        d->malformed_reason = "extension is .ay but content is not a ZXAY container";
      }
      return;
    }
    if (strcmp(ext, "ym") == 0) {
      if (!detect_ym(f, d)) {
        d->format = "YM";
        d->confidence = "probable";
      }
      return;
    }
    if (strcmp(ext, "psg") == 0) {
      if (!detect_psg(f, d)) {
        d->format = "PSG";
        d->confidence = "probable";
      }
      return;
    }
  }

  /* Tier C (Players.pas:8115-8146): unrecognised/absent extension - pure
   * content sniffing, in the exact order the Pascal Detect block uses. */
  if (str_at(f, 0, "ZXAY")) {
    detect_ay_container(f, d);
    return;
  }
  if (detect_psg(f, d)) return;
  if (detect_ym_body(f, 0, d)) return;
  if (detect_vtx(f, d)) return;
  if (str_at(f, 2, "-lh5-")) {
    d->format = "YM";
    d->confidence = "definite";
    d->compressed = "lha";
    d->chips = 1;
    return;
  }
  if (scan_whole_file_module_detector(f, d)) return;

  d->format = "unknown";
  d->confidence = "unknown";
}
