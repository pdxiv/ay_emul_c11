#include "gui/zones.h"

#include <gdk/gdk.h>
#include <math.h>

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

double gui_hslider_value_from_x(const gui_hslider* s, int mx) {
  if (s->w <= 0) return 0.0;
  double v = (double)(mx - s->x) / (double)s->w;
  if (v < 0.0) v = 0.0;
  if (v > 1.0) v = 1.0;
  return v;
}

void gui_hslider_draw(const gui_hslider* s, cairo_t* cr) {
  /* The skin's base bitmap already shows the track/groove graphic under
   * this rect (drawn as part of the main window's background blit) -
   * only a thumb marker is overlaid here, not a full custom track. The
   * original composites a real thumb bitmap from the skin
   * (MoveVol/MoveProgr.AddBitmaps, MainWin.pas:3813-3814); this port
   * draws a plain filled rectangle instead (see zones.h's file comment)
   * - a future milestone could draw the real thumb bitmap here without
   * changing this struct's shape. */
  int thumb_w = 6;
  double thumb_x = s->x + s->value * (s->w - thumb_w);
  cairo_save(cr);
  cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
  cairo_rectangle(cr, thumb_x, s->y, thumb_w, s->h);
  cairo_fill(cr);
  cairo_restore(cr);
}
