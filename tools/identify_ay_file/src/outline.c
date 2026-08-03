/* Output-line builder: one space-separated key=value line per file. See
 * identify_ay_file.md's "Escaping" section for the convention implemented
 * here. */
#include "identify/common.h"

#include <stdio.h>
#include <string.h>

void out_init(outline* o) {
  o->buf[0] = '\0';
  o->len = 0;
}

/* Escaping convention: within a value, backslash -> "\\", space -> "\ ",
 * and any byte < 0x20 or >= 0x7f -> "\xHH". This keeps the whole line
 * splittable on unescaped whitespace without ever needing quotes, so
 * `grep 'key=value'` and `awk` both work unmodified on the output. Keys
 * are always from a fixed internal set of plain identifiers and never
 * need escaping. */
void out_kv(outline* o, const char* key, const char* value) {
  size_t remaining = sizeof(o->buf) - o->len;
  if (remaining < 4) return;
  int n = snprintf(o->buf + o->len, remaining, "%s%s=", o->len ? " " : "", key);
  if (n < 0 || (size_t)n >= remaining) {
    o->len = sizeof(o->buf) - 1;
    return;
  }
  o->len += (size_t)n;
  for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
    char esc[5];
    size_t esclen;
    if (*p == '\\') {
      esc[0] = '\\';
      esc[1] = '\\';
      esclen = 2;
    } else if (*p == ' ') {
      esc[0] = '\\';
      esc[1] = ' ';
      esclen = 2;
    } else if (*p < 0x20 || *p >= 0x7f) {
      snprintf(esc, sizeof(esc), "\\x%02X", *p);
      esclen = 4;
    } else {
      esc[0] = (char)*p;
      esclen = 1;
    }
    if (o->len + esclen >= sizeof(o->buf) - 1) break;
    memcpy(o->buf + o->len, esc, esclen);
    o->len += esclen;
  }
  o->buf[o->len] = '\0';
}

void out_kv_int(outline* o, const char* key, long value) {
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%ld", value);
  out_kv(o, key, tmp);
}

void out_kv_bool(outline* o, const char* key, int tristate) {
  out_kv(o, key, tristate < 0 ? "unknown" : (tristate ? "yes" : "no"));
}
