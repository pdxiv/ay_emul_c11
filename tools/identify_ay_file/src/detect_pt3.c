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
