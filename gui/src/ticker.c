#include "gui/ticker.h"

#include <stdint.h>
#include <string.h>

#define TICKER_FONT_SIZE (GUI_TICKER_LINE_HEIGHT - 6)
/* MainWin.pas:3389: BMP_VScroll.Canvas.Font.Color := $606060 - a
 * neutral gray (equal RGB channels, so BGR-vs-RGB byte order is moot). */
#define TICKER_COLOR (0x60 / 255.0)

static void set_ticker_font(cairo_t* cr) {
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, TICKER_FONT_SIZE);
}

/* MainWin.pas: TextWidth, via a throwaway 1x1 surface - just needed for
 * font metrics, no real drawing happens on it. */
static double measure_text_width(const char* text) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(surf);
  set_ticker_font(cr);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, text, &ext);
  double w = ext.x_advance;
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return w;
}

/* MainWin.pas: GetStringWnJ - centers text that fits the display width
 * (sj = the centering offset), or left-aligns at 0 for text that needs
 * to scroll instead (the caller then positions it via horiz_offset). */
static double text_x_offset(double width) {
  if (width <= GUI_TICKER_WIDTH) return (GUI_TICKER_WIDTH - width) / 2.0;
  return 0.0;
}

void gui_ticker_init(gui_ticker* t) {
  memset(t, 0, sizeof(*t));
  t->do_scroll = true; /* MainWin.pas: Do_Scroll's own default */
  t->scroll_pause = 1; /* MainWin.pas: Scr_Pause's own default */
}

void gui_ticker_set_target(gui_ticker* t, const char* text, bool is_next,
                            bool is_prev) {
  if (!t->has_content) {
    /* MainWin.pas: Item_Displayed's initial -1 -> the first valid
     * index is always a "big jump" (nothing to slide from). */
    strncpy(t->text, text, sizeof(t->text) - 1);
    t->text[sizeof(t->text) - 1] = '\0';
    t->text_width = measure_text_width(t->text);
    t->has_content = true;
    t->transitioning = false;
    t->horiz_offset = 0;
    t->scroll_left = false;
    t->scroll_pause = 1;
    return;
  }

  const char* current = t->transitioning ? t->new_text : t->text;
  if (strcmp(current, text) == 0) return; /* already there/heading there */

  if (is_next || is_prev) {
    /* MainWin.pas: PlayItem setting Scroll_Distination exactly one
     * away from Item_Displayed - animate the single-line slide. A
     * transition already in flight is abandoned and restarted fresh
     * toward the new target (see ticker.h's own file comment on why
     * this port doesn't reimplement the original's full multi-line
     * "still catching up" chase logic). */
    strncpy(t->old_text, current, sizeof(t->old_text) - 1);
    t->old_text[sizeof(t->old_text) - 1] = '\0';
    strncpy(t->new_text, text, sizeof(t->new_text) - 1);
    t->new_text[sizeof(t->new_text) - 1] = '\0';
    t->direction = is_next ? 1 : -1;
    t->transitioning = true;
    t->scroll_offset = 0;
  } else {
    /* Arbitrary jump - snap (see ticker.h's own file comment). */
    strncpy(t->text, text, sizeof(t->text) - 1);
    t->text[sizeof(t->text) - 1] = '\0';
    t->text_width = measure_text_width(t->text);
    t->transitioning = false;
  }
  t->horiz_offset = 0;
  t->scroll_left = false;
  t->scroll_pause = 1;
}

void gui_ticker_tick(gui_ticker* t) {
  if (!t->has_content) return;

  bool scr_flag = true; /* MainWin.pas: ScrFlg */
  if (t->transitioning) {
    scr_flag = false;
    /* MainWin.pas: Y_Stp - the original's own formula reduces to 1 for
     * a single 24px line distance (see ticker.h's file comment on
     * this port's simplified single-step-only transition model), so
     * it's applied directly here rather than re-derived generally. */
    t->scroll_offset += 1;
    if (t->scroll_offset >= GUI_TICKER_LINE_HEIGHT) {
      t->scroll_offset = GUI_TICKER_LINE_HEIGHT;
      strncpy(t->text, t->new_text, sizeof(t->text) - 1);
      t->text[sizeof(t->text) - 1] = '\0';
      t->text_width = measure_text_width(t->text);
      t->transitioning = false;
      t->horiz_offset = 0;
      t->scroll_pause = 1;
      t->scroll_left = false;
    }
  }

  if (!scr_flag) return; /* MainWin.pas: the horizontal-scroll block
                           * below is skipped entirely whenever a
                           * vertical step just ran this tick */

  if (!t->do_scroll || t->dragging || t->text_width <= GUI_TICKER_WIDTH)
    return;

  /* MainWin.pas:750-786. */
  if (--t->scroll_pause != 0) return;
  t->scroll_pause = 1;
  if (t->scroll_left) {
    t->horiz_offset--;
    if (t->horiz_offset < 0) {
      t->scroll_left = false;
      t->horiz_offset = 0;
      t->scroll_pause = 50;
    }
  } else {
    t->horiz_offset++;
    if ((double)t->horiz_offset > t->text_width - GUI_TICKER_WIDTH) {
      t->scroll_left = true;
      t->horiz_offset = (int)(t->text_width - GUI_TICKER_WIDTH);
      t->scroll_pause = 50;
    }
  }
}

/* MainWin.pas: RedrawScroll/DoVisualisation's own `BMP_VScroll`
 * (drawn white-on-white then text in $606060) + `cmSrcAnd` blit
 * against the skin's own background (see ticker.h's file comment for
 * why this isn't just a solid box). Renders `n` stacked lines (1 for
 * the settled case, 2 during a vertical transition) into an off-
 * screen ARGB32 buffer, ANDs a GUI_TICKER_LINE_HEIGHT-tall window of
 * it (starting at `window_y`) against `skin`'s own pixels at
 * (GUI_TICKER_X, GUI_TICKER_Y), and paints the result onto `cr`. */
static void draw_and_masked(cairo_t* cr, GdkPixbuf* skin,
                             const char* const* lines, const double* x_offsets,
                             int n, int window_y) {
  int total_h = GUI_TICKER_LINE_HEIGHT * n;

  cairo_surface_t* mask_surf = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, GUI_TICKER_WIDTH, total_h);
  cairo_t* mcr = cairo_create(mask_surf);
  cairo_set_source_rgb(mcr, 1, 1, 1);
  cairo_paint(mcr);
  set_ticker_font(mcr);
  cairo_set_source_rgb(mcr, TICKER_COLOR, TICKER_COLOR, TICKER_COLOR);
  cairo_font_extents_t fe;
  cairo_font_extents(mcr, &fe);
  for (int i = 0; i < n; i++) {
    if (!lines[i] || !lines[i][0]) continue;
    double ty = i * GUI_TICKER_LINE_HEIGHT +
                (GUI_TICKER_LINE_HEIGHT - fe.height) / 2.0 + fe.ascent;
    cairo_move_to(mcr, x_offsets[i], ty);
    cairo_show_text(mcr, lines[i]);
  }
  cairo_destroy(mcr);
  cairo_surface_flush(mask_surf);

  int mask_stride = cairo_image_surface_get_stride(mask_surf);
  unsigned char* mask_data = cairo_image_surface_get_data(mask_surf);

  int skin_w = gdk_pixbuf_get_width(skin);
  int skin_h = gdk_pixbuf_get_height(skin);
  int skin_stride = gdk_pixbuf_get_rowstride(skin);
  int skin_channels = gdk_pixbuf_get_n_channels(skin);
  const unsigned char* skin_data = gdk_pixbuf_get_pixels(skin);

  cairo_surface_t* out_surf = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, GUI_TICKER_WIDTH, GUI_TICKER_LINE_HEIGHT);
  int out_stride = cairo_image_surface_get_stride(out_surf);
  unsigned char* out_data = cairo_image_surface_get_data(out_surf);

  for (int y = 0; y < GUI_TICKER_LINE_HEIGHT; y++) {
    int my = window_y + y;
    if (my < 0) my = 0;
    if (my >= total_h) my = total_h - 1;
    const uint32_t* mrow = (const uint32_t*)(mask_data + (size_t)my * mask_stride);
    int sy = GUI_TICKER_Y + y;
    uint32_t* orow = (uint32_t*)(out_data + (size_t)y * out_stride);
    for (int x = 0; x < GUI_TICKER_WIDTH; x++) {
      int sx = GUI_TICKER_X + x;
      uint8_t sr = 255, sg = 255, sb = 255;
      if (sx >= 0 && sx < skin_w && sy >= 0 && sy < skin_h) {
        const unsigned char* sp =
            skin_data + (size_t)sy * skin_stride + (size_t)sx * skin_channels;
        sr = sp[0];
        sg = sp[1];
        sb = sp[2];
      }
      uint32_t mpix = mrow[x];
      uint8_t mr = (uint8_t)((mpix >> 16) & 0xFFu);
      uint8_t mg = (uint8_t)((mpix >> 8) & 0xFFu);
      uint8_t mb = (uint8_t)(mpix & 0xFFu);
      /* MainWin.pas: cmSrcAnd - bitwise AND, per channel, not an
       * alpha-blend/multiply - Cairo has no native raster-op
       * equivalent, so this is done by hand on the raw pixel bytes. */
      uint8_t r = (uint8_t)(sr & mr);
      uint8_t g = (uint8_t)(sg & mg);
      uint8_t b = (uint8_t)(sb & mb);
      orow[x] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
  }
  cairo_surface_mark_dirty(out_surf);

  cairo_save(cr);
  cairo_set_source_surface(cr, out_surf, GUI_TICKER_X, GUI_TICKER_Y);
  cairo_rectangle(cr, GUI_TICKER_X, GUI_TICKER_Y, GUI_TICKER_WIDTH,
                   GUI_TICKER_LINE_HEIGHT);
  cairo_fill(cr);
  cairo_restore(cr);

  cairo_surface_destroy(mask_surf);
  cairo_surface_destroy(out_surf);
}

void gui_ticker_draw(const gui_ticker* t, cairo_t* cr, GdkPixbuf* skin) {
  if (!t->has_content) return;

  if (t->transitioning) {
    double old_w = measure_text_width(t->old_text);
    double new_w = measure_text_width(t->new_text);
    const char* lines[2];
    double x_offsets[2];
    int window_y;
    if (t->direction > 0) {
      lines[0] = t->old_text;
      lines[1] = t->new_text;
      x_offsets[0] = text_x_offset(old_w);
      x_offsets[1] = text_x_offset(new_w);
      window_y = t->scroll_offset;
    } else {
      lines[0] = t->new_text;
      lines[1] = t->old_text;
      x_offsets[0] = text_x_offset(new_w);
      x_offsets[1] = text_x_offset(old_w);
      window_y = GUI_TICKER_LINE_HEIGHT - t->scroll_offset;
    }
    draw_and_masked(cr, skin, lines, x_offsets, 2, window_y);
  } else {
    const char* lines[1] = {t->text};
    double x_offsets[1] = {text_x_offset(t->text_width) -
                            t->horiz_offset};
    draw_and_masked(cr, skin, lines, x_offsets, 1, 0);
  }
}

void gui_ticker_toggle_scroll(gui_ticker* t) { t->do_scroll = !t->do_scroll; }

void gui_ticker_drag(gui_ticker* t, int dx) {
  if (t->transitioning) return; /* MainWin.pas: `if Scroll_Distination <>
                                  * Item_Displayed then Exit` */
  if (t->text_width <= GUI_TICKER_WIDTH) return;
  t->horiz_offset -= dx;
  if (t->horiz_offset < 0) {
    t->horiz_offset = 0;
  } else if ((double)t->horiz_offset > t->text_width - GUI_TICKER_WIDTH) {
    t->horiz_offset = (int)(t->text_width - GUI_TICKER_WIDTH);
  }
}
