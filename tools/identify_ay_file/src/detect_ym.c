#include "identify/detect_ym.h"

#include <stdio.h>
#include <string.h>

/* Ay_Emul.fmt: [format+ YM2] match=0:YM2! ; [format+ YM3] match=0:YM3! ;
 * [format+ YM3b] match=0:YM3b ; [format+ YM5] match=0:YM5!LeOnArD! ;
 * [format+ YM6] match=0:YM6!LeOnArD! ; [format+ YM] match=2:-lh5- (whole
 * file LHA-wrapped; the wrapped payload then starts with one of the above).
 * NOTE Players.pas:8132-8134's inline content-sniff (used only in Tier C,
 * unrecognised extension) checks just the 4-byte YM2!/YM3!/YM3b/YM5!/YM6!
 * prefix, looser than Ay_Emul.fmt's own YM5/YM6 match rule which also
 * requires the following "LeOnArD!" 8 bytes - we check the stricter
 * (LeOnArD!-qualified) form for YM5/YM6 since it is a strictly more precise
 * signature and both are drawn from the real Pascal source; the 4-byte-only
 * check would occasionally accept corrupt data the loader itself would
 * reject as EYM5FileHeader-invalid.
 * TYM5FileHeader (Players.pas:274-283, all fields big-endian): Id[0..3]
 * Leo[4..11] Num_of_tiks[12..15] Song_Attr[16..19] Num_of_Dig[20..21]
 * ChipFrq[22..25] InterFrq[26..27] Loop[28..31] Add_Size[32..33]. The
 * "extended interleaved" YM5/YM6 sub-variant (which selects
 * YM5i_Get_Registers/YM6i_Get_Registers over the plain YM5_Get_Registers/
 * YM6_Get_Registers - Players.pas:2812-2870) is bit 0 of Song_Attr's LAST
 * (i.e. least-significant, since Song_Attr is big-endian dword) byte,
 * offset 19. */
bool detect_ym_body(const filebuf* f, size_t base, detection* d) {
  bool is_lha = base != 0; /* caller already matched "-lh5-" at offset 2 */
  if (str_at(f, base, "YM2!")) {
    d->format = "YM";
    d->subtype = "YM2";
  } else if (str_at(f, base, "YM3!")) {
    d->format = "YM";
    d->subtype = "YM3";
  } else if (str_at(f, base, "YM3b")) {
    d->format = "YM";
    d->subtype = "YM3b";
  } else if (str_at(f, base, "YM5!LeOnArD!")) {
    d->format = "YM";
    d->subtype = "YM5";
  } else if (str_at(f, base, "YM6!LeOnArD!")) {
    d->format = "YM";
    d->subtype = "YM6";
  } else {
    return false;
  }
  d->confidence = "definite";
  d->chips = 1;
  d->compressed = is_lha ? "lha" : "none";
  if (strcmp(d->subtype, "YM5") == 0 || strcmp(d->subtype, "YM6") == 0) {
    size_t hdr = base + 12; /* Num_of_tiks..Add_Size, see comment above */
    if (!in_bounds(f, hdr, 22)) {
      d->malformed = true;
      d->malformed_reason = "truncated YM5/YM6 header (before Add_Size)";
      return true;
    }
    uint32_t num_of_dig = be16_at(f, hdr + 8);
    d->digi_drum = num_of_dig > 0 ? 1 : 0;
    bool extended = (byte_at(f, hdr + 7) & 1) != 0; /* Song_Attr low byte, bit0 */
    d->extra_key = "extended";
    snprintf(d->extra_val, sizeof(d->extra_val), "%s", extended ? "yes" : "no");
  }
  return true;
}

bool detect_ym(const filebuf* f, detection* d) {
  if (str_at(f, 2, "-lh5-")) {
    /* TLZHFileHeader (Players.pas:286-296) follows at offset 2; the
     * compressed payload begins after it (Players.pas:7708-7736). We do
     * not decompress it (out of scope - identification only), so the
     * inner YM2!/.../YM6! sub-variant and its properties cannot be
     * determined from the outer file alone. */
    d->format = "YM";
    d->confidence = "definite";
    d->compressed = "lha";
    d->chips = 1;
    return true;
  }
  return detect_ym_body(f, 0, d);
}
