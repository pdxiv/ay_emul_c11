#define _POSIX_C_SOURCE 200809L /* for strnlen, under strict -std=c11 */
#include "gui/skin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/lh5.h"

/* gui/src/default_skin.c - generated from ay_emul/Ay_Emul2.ays, see that
 * file's own header comment for how to regenerate it. */
extern const unsigned char gui_default_skin_ays[];
extern const unsigned int gui_default_skin_ays_len;

/* MainWin.pas:84-85. */
#define SKIN_ID "Ay_Emul 2.0 Skin File\r\n\x1a"
#define SKIN_ID_LEN 24

static bool load_from_bytes(gui_skin* skin, const uint8_t* data,
                             size_t size) {
  memset(skin, 0, sizeof(*skin));

  if (size < SKIN_ID_LEN + 4) return false;
  if (memcmp(data, SKIN_ID, SKIN_ID_LEN) != 0) return false;

  uint32_t original_size;
  memcpy(&original_size, data + SKIN_ID_LEN, 4); /* x86-native LE, no swap */
  if (original_size == 0) return false;

  size_t comp_off = SKIN_ID_LEN + 4;
  if (comp_off > size) return false;
  int64_t comp_size = (int64_t)size - (int64_t)comp_off;
  if (comp_size <= 0 || comp_size > INT32_MAX) return false;

  uint8_t* buf = (uint8_t*)malloc(original_size);
  if (!buf) return false;
  if (!lh5_decompress(data + comp_off, (int32_t)comp_size, buf,
                       (int32_t)original_size)) {
    free(buf);
    return false;
  }

  /* Payload: null-terminated Author, null-terminated Comment, then the
   * raw .bmp bytes (MainWin.pas:3696-3712/3732). Bounds-check each
   * strnlen so a truncated/malformed decompressed buffer can't run past
   * the allocation. */
  size_t max = original_size;
  size_t alen = strnlen((char*)buf, max);
  if (alen >= max) { free(buf); return false; }
  size_t cstart = alen + 1;
  size_t clen = strnlen((char*)buf + cstart, max - cstart);
  if (cstart + clen >= max) { free(buf); return false; }
  size_t bmp_off = cstart + clen + 1;
  size_t bmp_size = max - bmp_off;

  skin->author = g_strndup((char*)buf, alen);
  skin->comment = g_strndup((char*)buf + cstart, clen);

  GError* err = NULL;
  GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
  bool ok = gdk_pixbuf_loader_write(loader, buf + bmp_off, bmp_size, &err);
  if (ok) ok = gdk_pixbuf_loader_close(loader, &err) != FALSE;
  if (err) g_error_free(err);
  if (ok) {
    skin->bitmap = gdk_pixbuf_loader_get_pixbuf(loader);
    if (skin->bitmap) g_object_ref(skin->bitmap);
  }
  g_object_unref(loader);
  free(buf);

  if (!skin->bitmap) {
    gui_skin_free(skin);
    return false;
  }
  return true;
}

bool gui_skin_load_default(gui_skin* skin) {
  return load_from_bytes(skin, gui_default_skin_ays, gui_default_skin_ays_len);
}

bool gui_skin_load_file(gui_skin* skin, const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
  long size = ftell(f);
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
  uint8_t* data = (uint8_t*)malloc((size_t)size);
  if (!data) { fclose(f); return false; }
  size_t read = fread(data, 1, (size_t)size, f);
  fclose(f);
  bool ok = (read == (size_t)size) && load_from_bytes(skin, data, read);
  free(data);
  return ok;
}

void gui_skin_free(gui_skin* skin) {
  if (skin->bitmap) g_object_unref(skin->bitmap);
  g_free(skin->author);
  g_free(skin->comment);
  memset(skin, 0, sizeof(*skin));
}
