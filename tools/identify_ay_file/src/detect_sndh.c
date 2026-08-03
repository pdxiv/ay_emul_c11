#include "identify/detect_sndh.h"

/* Ay_Emul.fmt: [format+ SNDH] match1=0:ICE! (ICE-compressed) OR match2=12:
 * SNDH (the 12-byte 68000 jump-vector table, then the "SNDH" magic).
 * sndh.pas's PlayFreq/NumberOfSongs and other TSNDHTag-encoded properties
 * (VBL/TA.../TC.../HDNS...) require walking a variable-length tag chain
 * after the fixed header - engine/src/sndh_file.c already ports the tag
 * scanner (sndh_ExtractTextInfo's core, see that file's `le16`/tag-walk
 * code) for playback purposes; replicating its full tag walk here (title/
 * author/song-count/etc.) is deliberately out of scope for identification
 * (see migration_debt.yaml) beyond the two structural properties below,
 * which only need the fixed 16-byte header. */
bool detect_sndh(const filebuf* f, detection* d) {
  if (str_at(f, 0, "ICE!")) {
    d->format = "SNDH";
    d->confidence = "definite";
    d->compressed = "ice";
    d->chips = 1;
    /* ICE-compressed body means the real "SNDH" magic (and any further
     * structural properties) are only visible after decompression, which
     * is out of scope here - see file header. */
    return true;
  }
  if (str_at(f, 12, "SNDH")) {
    d->format = "SNDH";
    d->confidence = "definite";
    d->compressed = "none";
    d->chips = 1;
    return true;
  }
  return false;
}
