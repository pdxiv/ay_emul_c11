#include "identify/detect_vtx.h"

#include <stdio.h>

/* Ay_Emul.fmt: [format+ VTX] matchprior=49 match1=0:ay match2=0:ym
 * match3=0:AY match4=0:YM. Players.pas:8135-8140's fuller inline check adds
 * the constraint the .fmt match alone can't express: the byte immediately
 * after the 2-letter Id must be in 0..6 (the VTX header's Mode byte,
 * TVTXFileHeader.Mode at offset 2, Players.pas:261-269) - without this the
 * check would false-positive on any file merely starting with "ay"/"ym"/
 * "AY"/"YM". Chip_Type (Players.pas:7674-7677): lowercase "ay"/"ym" id ->
 * long header (with Year/Programm/Tracker/Comment strings); uppercase
 * "AY"/"YM" -> short header. The letter pair itself also names the AY-chip
 * vs YM-chip synthesis mode used for playback (independent of header
 * length). */
bool detect_vtx(const filebuf* f, detection* d) {
  if (!in_bounds(f, 0, 3)) return false;
  char c0 = (char)byte_at(f, 0), c1 = (char)byte_at(f, 1);
  bool ay_lc = c0 == 'a' && c1 == 'y';
  bool ym_lc = c0 == 'y' && c1 == 'm';
  bool ay_uc = c0 == 'A' && c1 == 'Y';
  bool ym_uc = c0 == 'Y' && c1 == 'M';
  if (!ay_lc && !ym_lc && !ay_uc && !ym_uc) return false;
  uint8_t mode = byte_at(f, 2);
  if (mode > 6) return false;
  d->format = "VTX";
  d->confidence = "definite";
  d->chips = 1;
  d->compressed = "lha"; /* VTX bodies are always lh5-compressed, no -lh5- tag (see vtx_file.h) */
  d->extra_key = "chip_type";
  snprintf(d->extra_val, sizeof(d->extra_val), "%s", (ay_lc || ay_uc) ? "AY" : "YM");
  d->subtype = (ay_lc || ym_lc) ? "long_header" : "short_header";
  return true;
}
