#include "gui/zones.h"

#include <gdk/gdk.h>
#include <math.h>
#include <stdint.h>

static void draw_pixbuf_region(cairo_t* cr, GdkPixbuf* skin, int src_x,
                                int src_y, int w, int h, int dest_x,
                                int dest_y) {
  cairo_save(cr);
  cairo_translate(cr, dest_x - src_x, dest_y - src_y);
  gdk_cairo_set_source_pixbuf(cr, skin, 0, 0);
  cairo_rectangle(cr, src_x, src_y, w, h);
  cairo_fill(cr);
  cairo_restore(cr);
}

/* TMoveZone.AddBitmaps' `m: boolean` color-key path (MainWin.pas:2676-
 * 2681: `Bmp1.TransparentColor := Bmp1.Canvas.Pixels[0, 0]; Transparent
 * := True; TransparentMode := tmFixed;`) - the handle bitmap's own
 * (0,0) pixel is the transparent color key, so any pixel matching it
 * (typically the rounded corners' background fill) is skipped rather
 * than drawn. Cairo has no color-key compositing primitive, so this
 * builds a small ARGB32 surface by hand (same raw-pixel technique as
 * gui/src/ticker.c's AND-mask rendering) with alpha=0 on key-color
 * pixels, alpha=255 elsewhere, then paints that.
 *
 * C11-only enhancement, not present in the original (Delphi's TBitmap
 * has no real alpha channel, only this fixed-color-key trick): if the
 * skin bitmap actually carries a real alpha channel (a 32-bit BMP,
 * `gdk_pixbuf_get_has_alpha`), that per-pixel alpha is used directly
 * instead of the color-key test - a skin author can then paint real
 * soft/antialiased edges on a handle instead of a hard-cut silhouette.
 * A plain 24-bit skin (no alpha channel) reports has_alpha=false and
 * takes the exact same color-key path as before, byte-for-byte -
 * existing skins are entirely unaffected. See docs/
 * creating_custom_skins.md's "Alpha channel support" section. */
static void draw_pixbuf_region_keyed(cairo_t* cr, GdkPixbuf* skin,
                                      int src_x, int src_y, int w, int h,
                                      int dest_x, int dest_y) {
  int rowstride = gdk_pixbuf_get_rowstride(skin);
  int nch = gdk_pixbuf_get_n_channels(skin);
  guchar* pixels = gdk_pixbuf_get_pixels(skin);
  bool has_alpha = gdk_pixbuf_get_has_alpha(skin) && nch >= 4;
  guchar* key = pixels + (size_t)src_y * rowstride + (size_t)src_x * nch;
  guchar kr = key[0], kg = key[1], kb = key[2];

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                       w, h);
  unsigned char* dst = cairo_image_surface_get_data(surf);
  int dst_stride = cairo_image_surface_get_stride(surf);
  for (int yy = 0; yy < h; yy++) {
    guchar* srow = pixels + (size_t)(src_y + yy) * rowstride +
                    (size_t)src_x * nch;
    uint32_t* drow = (uint32_t*)(dst + yy * dst_stride);
    for (int xx = 0; xx < w; xx++) {
      guchar r = srow[xx * nch + 0];
      guchar g = srow[xx * nch + 1];
      guchar b = srow[xx * nch + 2];
      guchar a;
      if (has_alpha) {
        a = srow[xx * nch + 3];
      } else {
        a = (r == kr && g == kg && b == kb) ? 0 : 255;
      }
      if (a == 0) {
        drow[xx] = 0u;
      } else {
        /* Cairo's ARGB32 format is premultiplied - a no-op when a=255
         * (matches the old color-key-only math exactly), a real
         * scale-down for partial alpha. */
        uint32_t pr = (uint32_t)r * a / 255u;
        uint32_t pg = (uint32_t)g * a / 255u;
        uint32_t pb = (uint32_t)b * a / 255u;
        drow[xx] = ((uint32_t)a << 24) | (pr << 16) | (pg << 8) | pb;
      }
    }
  }
  cairo_surface_mark_dirty(surf);
  cairo_save(cr);
  cairo_set_source_surface(cr, surf, dest_x, dest_y);
  cairo_paint(cr);
  cairo_restore(cr);
  cairo_surface_destroy(surf);
}

bool gui_button_hit_test(const gui_button* b, int mx, int my) {
  return mx >= b->x && mx < b->x + b->w && my >= b->y && my < b->y + b->h;
}

void gui_button_draw(const gui_button* b, cairo_t* cr, GdkPixbuf* skin) {
  int sx = (b->is_pushed || b->is_on) ? b->src_pushed_x : b->src_normal_x;
  int sy = (b->is_pushed || b->is_on) ? b->src_pushed_y : b->src_normal_y;
  draw_pixbuf_region(cr, skin, sx, sy, b->w, b->h, b->x, b->y);
}

void gui_led_draw(const gui_led* led, cairo_t* cr, GdkPixbuf* skin) {
  int sx = led->state ? led->src_on_x : led->src_off_x;
  int sy = led->state ? led->src_on_y : led->src_off_y;
  draw_pixbuf_region(cr, skin, sx, sy, led->w, led->h, led->x, led->y);
}

bool gui_hslider_hit_test(const gui_hslider* s, int mx, int my) {
  return mx >= s->x && mx < s->x + s->w && my >= s->y && my < s->y + s->h;
}

/* Current on-screen pixel offset of the handle's left edge, relative to
 * s->x - MainWin.pas: TMoveZone::PosX, derived from `value` (the
 * normalized 0.0-1.0 form this struct keeps as its source of truth). */
static int slider_travel(const gui_hslider* s) {
  int t = s->w - s->thumb_w;
  return t > 0 ? t : 0;
}

static int slider_pos_x(const gui_hslider* s) {
  return (int)lround(s->value * slider_travel(s));
}

static void slider_set_pos_x(gui_hslider* s, int pos_x) {
  int travel = slider_travel(s);
  if (pos_x < 0) pos_x = 0;
  if (pos_x > travel) pos_x = travel;
  s->value = travel > 0 ? (double)pos_x / (double)travel : 0.0;
}

static bool slider_thumb_hit_test(const gui_hslider* s, int mx, int my) {
  int tx = s->x + slider_pos_x(s);
  return mx >= tx && mx < tx + s->thumb_w && my >= s->y &&
         my < s->y + s->thumb_h;
}

void gui_hslider_press(gui_hslider* s, int mx) {
  /* MainWin.pas:2131-2155 - ToucheBut (thumb) match keeps PosX as-is
   * and just starts tracking the delta from here; a Touche-only (bare
   * track) match jumps the thumb to be centered under the click first. */
  if (!slider_thumb_hit_test(s, mx, s->y)) {
    slider_set_pos_x(s, mx - s->x - s->thumb_w / 2);
  }
  s->dragging = true;
  s->drag_anchor_x = mx;
}

void gui_hslider_drag(gui_hslider* s, int mx) {
  if (!s->dragging) return;
  int pos_x = slider_pos_x(s) + (mx - s->drag_anchor_x);
  s->drag_anchor_x = mx;
  slider_set_pos_x(s, pos_x);
}

void gui_hslider_draw(const gui_hslider* s, cairo_t* cr, GdkPixbuf* skin) {
  /* The skin's base bitmap already shows the track/groove graphic under
   * this rect (drawn as part of the main window's background blit) -
   * only the real handle bitmap (MoveVol/MoveProgr.AddBitmaps,
   * MainWin.pas:3813-3814) is composited on top here, color-keyed same
   * as the original (see draw_pixbuf_region_keyed's own comment). */
  if (s->thumb_w <= 0 || s->thumb_h <= 0) return;
  int tx = s->x + slider_pos_x(s);
  draw_pixbuf_region_keyed(cr, skin, s->thumb_src_x, s->thumb_src_y,
                            s->thumb_w, s->thumb_h, tx, s->y);
}
