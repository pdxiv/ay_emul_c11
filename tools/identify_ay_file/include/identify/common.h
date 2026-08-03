/* Shared types and small bounds-checked helpers used by every detector
 * module. See identify_ay_file.md for the overall design; each detector's
 * own header cites the exact Ay_Emul.fmt/Players.pas source it ports. */
#ifndef IDENTIFY_COMMON_H
#define IDENTIFY_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Cap on how much of a file we read into memory. Real AY/YM/tracker files
 * are at most a few hundred KB; SNDH/AY files can carry a compressed 68000
 * or Z80 memory image but even those are well under this. Generic
 * BASS-decoded formats (mp3/wav/flac/...) are out of scope and are
 * reported as format=unknown without reading their bodies at all. */
#define MAX_READ_SIZE (16 * 1024 * 1024)

typedef struct {
  uint8_t* data;
  size_t size;    /* bytes actually read */
  size_t on_disk; /* full on-disk size, from ftell/fseek */
} filebuf;

int read_whole_file(const char* path, filebuf* f, char* errbuf, size_t errbuf_size);

/* ---- small bounds-checked helpers, shared by every detector ------------- */

bool has_at(const filebuf* f, size_t offset, const uint8_t* pat, size_t patlen);
bool str_at(const filebuf* f, size_t offset, const char* s);
bool in_bounds(const filebuf* f, size_t offset, size_t len);
uint8_t byte_at(const filebuf* f, size_t offset); /* caller must check in_bounds first */
uint16_t be16_at(const filebuf* f, size_t offset);
uint16_t le16_at(const filebuf* f, size_t offset);

/* Case-sensitive substring search bounded to f->size bytes, used for the
 * best-effort Module_Detector-style whole-file scans (Tier C fallback).
 * Returns the match start offset, or (size_t)-1. */
size_t find_bytes(const filebuf* f, size_t from, const uint8_t* pat, size_t patlen);

/* ---- output line builder -------------------------------------------------- */

typedef struct {
  char buf[8192];
  size_t len;
} outline;

void out_init(outline* o);
void out_kv(outline* o, const char* key, const char* value);
void out_kv_int(outline* o, const char* key, long value);
void out_kv_bool(outline* o, const char* key, int tristate /* -1/0/1 */);

/* ---- detection result ------------------------------------------------------ */

typedef struct {
  const char* format;     /* canonical Pascal-derived name, or "unknown" */
  const char* subtype;    /* or "none" */
  long version;           /* -1 => unknown/not applicable */
  int chips;               /* -1 => unknown; else chip count */
  int turbo_sound;         /* -1/0/1 */
  int digi_drum;           /* -1/0/1 */
  const char* compressed; /* "none" | "lha" | "ice" | "unknown" */
  const char* confidence; /* "definite" | "probable" | "unknown" */
  const char* extra_key;  /* one optional format-specific extra field */
  char extra_val[64];
  bool malformed;
  const char* malformed_reason;
} detection;

static inline void detection_init(detection* d) {
  d->format = "unknown";
  d->subtype = "none";
  d->version = -1;
  d->chips = -1;
  d->turbo_sound = -1;
  d->digi_drum = -1;
  d->compressed = "none";
  d->confidence = "unknown";
  d->extra_key = NULL;
  d->extra_val[0] = '\0';
  d->malformed = false;
  d->malformed_reason = NULL;
}

#endif /* IDENTIFY_COMMON_H */
