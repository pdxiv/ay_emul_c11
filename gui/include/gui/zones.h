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
 *    MainWin.pas:3331-3334). The thumb is now the real skin bitmap
 *    (TMoveZone.AddBitmaps, MainWin.pas:3813-3814 - color-keyed on the
 *    source rect's own (0,0) pixel, matching TransparentMode=tmFixed),
 *    and press/drag now replicate FormMouseDown/FormMouseMove's actual
 *    two-mode behavior (MIG-0099): clicking directly on the thumb
 *    starts a relative/delta drag that follows the mouse from wherever
 *    it was grabbed; clicking elsewhere in the track jumps the thumb to
 *    be centered under the click point first (MainWin.pas:2140-2144's
 *    `OfsR := X - zx - bm1w div 2`). Travel range is the real
 *    `zw - Bm1w` (0 to track-width-minus-handle-width), not an assumed
 *    fixed thumb width. Not replicated: the original's own OldX/Delt
 *    edge-clamp quirk (MainWin.pas:2294-2303's `p1^.OldX := p1^.Delt`
 *    on hitting the low clamp) - a standard delta-clamp is used
 *    instead, which is behaviorally equivalent for normal dragging and
 *    only differs in an obscure edge case (dragging past the track
 *    edge and back without releasing).
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

/* TMoveZone equivalent (MainWin.pas:153-168). `value` is 0.0-1.0 (the
 * TMoveZone::PosX pixel offset, normalized by travel range `w -
 * thumb_w`) and remains the single source of truth callers (volume/
 * seek) read - see file comment for the drag semantics this now
 * replicates and the one deliberate simplification kept. */
typedef struct gui_hslider {
  int x, y, w, h; /* track rect (zx,zy,zw,zh) */
  int thumb_w, thumb_h;         /* Bm1w/Bm1h - handle bitmap size */
  int thumb_src_x, thumb_src_y; /* handle bitmap's source rect top-left
                                  * in the skin (AddBitmaps' x1,y1) */
  double value;   /* 0.0-1.0 */
  bool dragging;
  int drag_anchor_x; /* MainWin.pas: OldX - local x of the last press/
                       * motion event, for the next frame's delta */
  gui_zone_click_cb on_change; /* called on every value update while
                                 * dragging, same as the original's
                                 * Action/DoMovingVol/DoMovingProgr
                                 * per-motion-event callback shape */
  void* userdata;
} gui_hslider;

/* Whole-track hit test (TMoveZone.Touche, minus its narrower zy1/zh1
 * vertical sub-band - kept as a deliberately more forgiving simplification,
 * unchanged from before this entry). Gates whether a press interacts
 * with this slider at all. */
bool gui_hslider_hit_test(const gui_hslider* s, int mx, int my);

/* MainWin.pas: FormMouseDown's MoveZoneRoot walk (2126-2164). Call once
 * gui_hslider_hit_test has already confirmed the press is inside this
 * slider's track. Starts a drag; if the press landed on the handle
 * itself, it's a relative follow-the-mouse drag from here, otherwise
 * the handle immediately jumps to be centered under the click (matching
 * the original exactly) before the drag starts. */
void gui_hslider_press(gui_hslider* s, int mx);

/* MainWin.pas: FormMouseMove's MoveZoneRoot walk (2285-2321). Call on
 * every motion event while `dragging` is true. */
void gui_hslider_drag(gui_hslider* s, int mx);

void gui_hslider_draw(const gui_hslider* s, cairo_t* cr, GdkPixbuf* skin);

#endif /* GUI_ZONES_H */
