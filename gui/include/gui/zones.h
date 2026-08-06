/* C11/GTK2/Cairo port of MainWin.pas's TButtZone/TLedZone/TMoveZone
 * hit-testing + redraw classes (see MainWin.pas:112-168 for the
 * originals). TSensZone (spectrum/amp/time-display click zones, used
 * only by the visualizer) is NOT ported - out of scope for this
 * milestone (see migration_debt.yaml).
 *
 * Simplifications from the original, deliberate and documented (not
 * silent):
 *  - Button/LED hit-testing here is a plain axis-aligned bounding-box
 *    rect check. The original computes a true rounded-rectangle GDI
 *    region per button (CreateRoundRectRgn, MainWin.pas:3211-3270) and
 *    tests membership in THAT region - the practical difference is only
 *    a few pixels right at each button's rounded corners, not a
 *    functional gap. A future milestone could add a rounded-rect test
 *    (cairo_region_t doesn't have one built in, would need a small
 *    corner-radius helper) without changing this struct's shape.
 *  - Sliders (TMoveZone) are ported here as plain horizontal 0..1 value
 *    sliders - MoveVol/MoveProgr/MoveWin are all horizontal in the
 *    original too (confirmed from their actual Create() call sites,
 *    MainWin.pas:3331-3334), so this isn't a simplification of
 *    orientation, only of the thumb's exact drawn shape (the original's
 *    TMoveZone.AddBitmaps composites a separate thumb bitmap on top of
 *    the track; this port draws a plain filled rectangle thumb for M1).
 */
#ifndef GUI_ZONES_H
#define GUI_ZONES_H

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>

typedef void (*gui_zone_click_cb)(void* userdata);

/* TButtZone equivalent (MainWin.pas:115-133). dest rect is where it's
 * drawn on the main window; src_normal/src_pushed are the top-left
 * corners of the two same-sized source rows in the skin bitmap
 * (MainWin.pas:3787-3798's own x1,y1 / x2,y2 pairs). */
typedef struct gui_button {
  int x, y, w, h;               /* on-screen (destination) rect */
  int src_normal_x, src_normal_y; /* source rect top-left, "up" state */
  int src_pushed_x, src_pushed_y; /* source rect top-left, "pushed" state */
  bool is_on;     /* toggle-style buttons (Loop/Mixer/Tools/List) stay
                    * visually pushed while on; momentary buttons
                    * (Play/Stop/Pause/Open/Min/Close) don't use this */
  bool is_pushed; /* true while the mouse is physically held down on it */
  gui_zone_click_cb on_click;
  void* userdata;
} gui_button;

bool gui_button_hit_test(const gui_button* b, int mx, int my);
/* Draws the button's current-state source rect at its dest rect. */
void gui_button_draw(const gui_button* b, cairo_t* cr, GdkPixbuf* skin);

/* TLedZone equivalent (MainWin.pas:138-148) - draw-only, no click. */
typedef struct gui_led {
  int x, y, w, h;
  int src_off_x, src_off_y;
  int src_on_x, src_on_y;
  bool state;
} gui_led;

void gui_led_draw(const gui_led* led, cairo_t* cr, GdkPixbuf* skin);

/* TMoveZone equivalent (MainWin.pas:153-168), simplified to a plain
 * horizontal slider (see file comment). `value` is 0.0-1.0. Callers
 * update `value` from pointer-motion events via
 * gui_hslider_value_from_x while `dragging` is true (set on button-press
 * inside the track, cleared on button-release - the caller's GTK event
 * handlers own that state transition, this struct just holds it). */
typedef struct gui_hslider {
  int x, y, w, h; /* track rect */
  double value;   /* 0.0-1.0 */
  bool dragging;
  gui_zone_click_cb on_change; /* called on every value update while
                                 * dragging, same as the original's
                                 * Action/DoMovingVol/DoMovingProgr
                                 * per-motion-event callback shape */
  void* userdata;
} gui_hslider;

bool gui_hslider_hit_test(const gui_hslider* s, int mx, int my);
/* Clamps to [0,1]; caller is responsible for calling on_change if it
 * wants one fired (gui_hslider itself never calls it, so a read-only
 * slider like M1's progress display can reuse this for hit-testing
 * without accidentally becoming draggable - see mainwin.c). */
double gui_hslider_value_from_x(const gui_hslider* s, int mx);
void gui_hslider_draw(const gui_hslider* s, cairo_t* cr);

#endif /* GUI_ZONES_H */
