#include "identify/detect_container.h"

#include <stdio.h>
#include <string.h>

/* Ay_Emul.fmt: [format+ AY] match=0:ZXAYEMUL ; [format+ AYAMAD]
 * match=0:ZXAYAMAD - these are the OS-mime-type/freedesktop names, and
 * NOT what the real in-app loader reports. OpenAYFile (Players.pas:
 * 7132-7236) is the ACTUAL dispatcher for all three TypeIDs Detect's
 * Players.pas:8123 check accepts (EMUL/AMAD/ST11), and it does NOT
 * classify AMAD/ST11 the way Ay_Emul.fmt's names would suggest - confirmed
 * by reading OpenAYFile's body directly (Players.pas:7192-7230):
 *   TypeID "EMUL" ($4C554D45) -> FileType := FT.AY        (Players.pas:7195)
 *   TypeID "AMAD" ($44414D41) -> FileType := FT.FXM        (Players.pas:7216)
 *   TypeID "ST11" ($31315453) -> FileType := FT.ST1        (Players.pas:7229)
 * i.e. a ZXAY+AMAD container is loaded and played as an ordinary FXM
 * (Fuxoft AY Language) file, and a ZXAY+ST11 container as an ordinary
 * ST1 (Sound Tracker 1, uncompiled) file - both share ONLY their outer
 * container wrapper with the real ZXAYEMUL format, not their actual
 * in-memory representation or FileType. This was verified by comparing
 * against a real ay_emul build via a new OracleHarness.pas scenario (see
 * tests/oracle_diff) - our first implementation reported format=AYAMAD/
 * format=AY;subtype=ST11 here, which was wrong.
 * TAYFileHeader (Players.pas:220-230): offset 0-3 "ZXAY", 4-7 TypeID,
 * 16 NumOfSongs (0-based count -> +1 total songs), 17 FirstSong. Note:
 * NumOfSongs/FirstSong/"songs=" only apply to the real FT.AY case - AMAD/
 * ST11 files are single-song by the time OpenAYFile is done with them, so
 * "songs=" is not reported for those. */
bool detect_ay_container(const filebuf* f, detection* d) {
  if (!str_at(f, 0, "ZXAY")) return false;
  d->confidence = "definite";
  d->chips = 1;
  if (!in_bounds(f, 4, 4)) {
    d->format = "ZXAY";
    d->chips = -1;
    d->malformed = true;
    d->malformed_reason = "truncated before TypeID field (offset 4-7)";
    return true;
  }
  if (str_at(f, 4, "AMAD")) {
    d->format = "FXM";
    d->confidence = "definite";
    return true;
  }
  if (str_at(f, 4, "ST11")) {
    d->format = "ST1";
    d->confidence = "definite";
    return true;
  }
  if (str_at(f, 4, "EMUL")) {
    d->format = "AY";
  } else {
    /* Players.pas:8124: any other TypeID -> the generic register-stream
     * "ZXAY" format (Ay_Emul.fmt [format+ ZXAY], type=AYRS), NOT the
     * Z80-driven AY container. */
    d->format = "ZXAY";
    d->chips = -1;
    return true;
  }
  if (in_bounds(f, 17, 1)) {
    d->extra_key = "songs";
    snprintf(d->extra_val, sizeof(d->extra_val), "%d", byte_at(f, 16) + 1);
  }
  if (!in_bounds(f, 18, 2)) {
    d->malformed = true;
    d->malformed_reason = "truncated before PSongsStructure pointer (offset 18)";
  }
  return true;
}

/* Ay_Emul.fmt: [format+ AYM] match=0:AYM0. TAYMFileHeader (Players.pas:
 * 245-254): AYM[0..2]="AYM", Rev[3], Name[4..31], Author[32..47],
 * MusMin[52], MusMax[53]. OpenAYMFile (Players.pas:7026-7038) hard-requires
 * Rev='0', raising an error otherwise - we mirror that by marking the file
 * malformed rather than accepting an unsupported revision silently. */
bool detect_aym(const filebuf* f, detection* d) {
  if (!str_at(f, 0, "AYM")) return false;
  d->format = "AYM";
  d->confidence = "definite";
  d->chips = 1;
  if (!in_bounds(f, 54, 1)) {
    d->malformed = true;
    d->malformed_reason = "truncated AYM header (before MusMax at offset 53)";
    return true;
  }
  if (byte_at(f, 3) != '0') {
    d->malformed = true;
    d->malformed_reason = "unsupported AYM revision (Players.pas requires Rev='0')";
  }
  d->extra_key = "songs";
  snprintf(d->extra_val, sizeof(d->extra_val), "%d",
           byte_at(f, 53) - byte_at(f, 52) + 1);
  return true;
}

/* Ay_Emul.fmt: [format+ EPSG] match=0:EPSG#26; match=10:#0;#0;#0;#0;#0;#0;
 * (i.e. "EPSG"+0x1A at offset 0, six zero bytes at offset 10) and
 * [format+ PSG] match=0:PSG#26; ("PSG"+0x1A). IMPORTANT Pascal quirk,
 * confirmed at Players.pas:8130 (`if (zag='PSG'#$1a) or (zag='EPSG') then
 * added := Add(FT.PSG,...)`): EVEN IN THE CONTENT-SNIFF PATH, an EPSG file
 * is dispatched as plain FT.PSG - Ay_Emul.fmt's separately-named EPSG
 * format is never actually selected by AddFile's real dispatch logic (dead
 * data in the format table). We therefore report format=PSG for both,
 * with subtype=EPSG|plain to still preserve the distinction Ay_Emul.fmt
 * itself draws, per the task's "preserve distinctions" instruction, without
 * inventing a format name Pascal's real dispatcher never produces. */
bool detect_psg(const filebuf* f, detection* d) {
  static const uint8_t psg_sig[4] = {'P', 'S', 'G', 0x1A};
  static const uint8_t epsg_sig[5] = {'E', 'P', 'S', 'G', 0x1A};
  static const uint8_t six_zero[6] = {0, 0, 0, 0, 0, 0};
  bool is_psg = has_at(f, 0, psg_sig, 4);
  bool is_epsg = has_at(f, 0, epsg_sig, 5);
  if (!is_psg && !is_epsg) return false;
  d->format = "PSG";
  d->confidence = "definite";
  d->chips = 1;
  d->subtype = is_epsg ? "EPSG" : "plain";
  if (is_epsg && !has_at(f, 10, six_zero, 6)) {
    d->malformed = true;
    d->malformed_reason = "EPSG header's reserved zero field (offset 10-15) is non-zero";
  }
  return true;
}
