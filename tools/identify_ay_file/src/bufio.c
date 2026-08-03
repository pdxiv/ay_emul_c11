/* File loading and bounds-checked byte access shared by every detector. */
#include "identify/common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool has_at(const filebuf* f, size_t offset, const uint8_t* pat, size_t patlen) {
  if (offset > f->size || patlen > f->size - offset) return false;
  return memcmp(f->data + offset, pat, patlen) == 0;
}

bool str_at(const filebuf* f, size_t offset, const char* s) {
  return has_at(f, offset, (const uint8_t*)s, strlen(s));
}

bool in_bounds(const filebuf* f, size_t offset, size_t len) {
  return offset <= f->size && len <= f->size - offset;
}

uint8_t byte_at(const filebuf* f, size_t offset) { return f->data[offset]; }

uint16_t be16_at(const filebuf* f, size_t offset) {
  return (uint16_t)((uint16_t)byte_at(f, offset) << 8 | byte_at(f, offset + 1));
}

/* x86 Pascal `word` fields (PWord(...)^) are native-endian, i.e. little
 * endian - used by ST3/ASC/STP/PT1/PT2/SQT/FLS's on-disk pointer tables. */
uint16_t le16_at(const filebuf* f, size_t offset) {
  return (uint16_t)(byte_at(f, offset) | (uint16_t)byte_at(f, offset + 1) << 8);
}

size_t find_bytes(const filebuf* f, size_t from, const uint8_t* pat, size_t patlen) {
  if (patlen == 0 || patlen > f->size) return (size_t)-1;
  for (size_t i = from; i + patlen <= f->size; i++) {
    if (memcmp(f->data + i, pat, patlen) == 0) return i;
  }
  return (size_t)-1;
}

int read_whole_file(const char* path, filebuf* f, char* errbuf, size_t errbuf_size) {
  f->data = NULL;
  f->size = 0;
  f->on_disk = 0;
  FILE* fp = fopen(path, "rb");
  if (!fp) {
    snprintf(errbuf, errbuf_size, "cannot open '%s': %s", path, strerror(errno));
    return -1;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    snprintf(errbuf, errbuf_size, "cannot seek '%s': %s", path, strerror(errno));
    fclose(fp);
    return -1;
  }
  long sz = ftell(fp);
  if (sz < 0) {
    snprintf(errbuf, errbuf_size, "cannot determine size of '%s'", path);
    fclose(fp);
    return -1;
  }
  f->on_disk = (size_t)sz;
  if (fseek(fp, 0, SEEK_SET) != 0) {
    snprintf(errbuf, errbuf_size, "cannot rewind '%s'", path);
    fclose(fp);
    return -1;
  }
  size_t to_read = f->on_disk;
  if (to_read > MAX_READ_SIZE) to_read = MAX_READ_SIZE;
  f->data = malloc(to_read > 0 ? to_read : 1);
  if (!f->data) {
    snprintf(errbuf, errbuf_size, "out of memory reading '%s'", path);
    fclose(fp);
    return -1;
  }
  f->size = fread(f->data, 1, to_read, fp);
  if (ferror(fp)) {
    snprintf(errbuf, errbuf_size, "read error on '%s'", path);
    free(f->data);
    f->data = NULL;
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}
