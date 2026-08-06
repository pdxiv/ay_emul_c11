/* C11/GTK2 port of MainWin.pas's LoadSkin/SetMainBmp (skin file loading).
 *
 * The `.ays` skin file format (confirmed by direct decode of the real
 * ay_emul/Ay_Emul2.ays default skin, not guessed):
 *   - 24-byte fixed ID string, `SkinId` (MainWin.pas:84):
 *     "Ay_Emul 2.0 Skin File\r\n\x1a".
 *   - 4-byte little-endian `Original_Size` (the UNCOMPRESSED payload
 *     size - x86-native byte order, no swap needed, unlike this
 *     project's register-dump formats).
 *   - The rest of the file is a BARE "-lh5-" compressed stream (no
 *     embedded TLZHFileHeader, unlike .ym files - confirmed by reading
 *     lh5.pas's InitLZHDepacker, which takes its uncompressed-size
 *     bookkeeping from the SkinId header's own Original_Size global, not
 *     from any header inside the compressed bytes themselves) -
 *     decompresses directly via the engine's existing lh5_decompress
 *     (ay_engine/lh5.h), verified end-to-end against the real skin file.
 *   - The decompressed payload is: a null-terminated Author string, a
 *     null-terminated Comment string, then a raw standard .bmp file
 *     (MainWin.pas's SetMainBmp loads it via TBitmap.LoadFromStream -
 *     nothing custom) - decoded here via GdkPixbufLoader, which handles
 *     BMP natively.
 *
 * Only the default skin (embedded as a C byte array, gui/src/
 * default_skin.c, generated from ay_emul/Ay_Emul2.ays) is loaded in this
 * milestone - user-supplied external .ays files (MainWin.pas's "load a
 * different skin" menu item) are a later-milestone addition; skin_load
 * below already takes a path so that's additive, not a redesign, when
 * it's added.
 */
#ifndef GUI_SKIN_H
#define GUI_SKIN_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct gui_skin {
  GdkPixbuf* bitmap; /* the full sprite-sheet bitmap (358x150 for the
                       * default skin - MWWidth x (MWHeight+27), see
                       * MainWin.pas:40-41 and SetMainBmp's per-button
                       * "pressed" row offset) */
  char* author;       /* owned, g_free */
  char* comment;       /* owned, g_free */
} gui_skin;

/* Loads and decompresses the embedded default skin (Ay_Emul2.ays, baked
 * in via gui/src/default_skin.c) into `skin`. Returns false (and leaves
 * `skin` zeroed) on any decompress/decode failure - not expected to ever
 * happen for the embedded default, but the real .ays file's own
 * lh5_decompress call can fail on malformed input, so this is checked
 * rather than assumed. */
bool gui_skin_load_default(gui_skin* skin);

/* Loads an external .ays file from `path`, same contract as
 * gui_skin_load_default otherwise. Not wired to any menu/UI action yet
 * (see file comment) - exposed now so it doesn't need a signature change
 * later. */
bool gui_skin_load_file(gui_skin* skin, const char* path);

void gui_skin_free(gui_skin* skin);

#endif /* GUI_SKIN_H */
