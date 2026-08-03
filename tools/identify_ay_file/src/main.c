/* identify_ay_file - standalone AY/YM chiptune format identifier.
 *
 * Reproduces the file-format identification behaviour of the original
 * ay_emul Object Pascal program (ay_emul/Players.pas, ay_emul/filetypes.pas,
 * ay_emul/Ay_Emul.fmt), WITHOUT any of its playback/emulation code. See
 * identify_ay_file.md for the full design writeup, output schema, and
 * detector-to-Pascal-source mapping; see dispatch.c for the top-level
 * Tier A/B/C dispatch this file drives.
 *
 * Output: exactly one line per successfully *opened* file, to stdout, of
 * the form `key=value key=value ...`. Usage/file-access errors go to
 * stderr with a nonzero exit status; malformed/truncated content is
 * reported on stdout as data (malformed=yes) rather than as a hard error,
 * since the file itself WAS successfully opened and read.
 *
 * Build: see Makefile (`make`), or invoke cc directly on every src/ file
 * with -Iinclude (see the Makefile for the exact invocation).
 */
#include "identify/common.h"
#include "identify/dispatch.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <music-file>\n", argc > 0 ? argv[0] : "identify_ay_file");
    return 2;
  }
  const char* path = argv[1];

  filebuf f;
  char errbuf[512];
  if (read_whole_file(path, &f, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "identify_ay_file: %s\n", errbuf);
    return 1;
  }

  if (f.on_disk > MAX_READ_SIZE) {
    fprintf(stderr,
            "identify_ay_file: warning: '%s' is %zu bytes, only the first "
            "%d were inspected\n",
            path, f.on_disk, MAX_READ_SIZE);
  }

  detection d;
  identify(&f, path, &d);

  outline o;
  out_init(&o);
  out_kv(&o, "file", path);
  out_kv(&o, "format", d.format);
  out_kv(&o, "subtype", d.subtype);
  if (d.version >= 0) out_kv_int(&o, "version", d.version);
  else out_kv(&o, "version", "unknown");
  if (d.chips >= 0) out_kv_int(&o, "chips", d.chips);
  else out_kv(&o, "chips", "unknown");
  out_kv_bool(&o, "turbo_sound", d.turbo_sound);
  out_kv_bool(&o, "digi_drum", d.digi_drum);
  out_kv(&o, "compressed", d.compressed);
  if (d.extra_key) out_kv(&o, d.extra_key, d.extra_val);
  out_kv_bool(&o, "malformed", d.malformed ? 1 : 0);
  out_kv(&o, "confidence", d.confidence);

  printf("%s\n", o.buf);

  if (d.malformed && d.malformed_reason) {
    fprintf(stderr, "identify_ay_file: %s: %s\n", path, d.malformed_reason);
  }

  free(f.data);
  return 0;
}
