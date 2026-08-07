/* C11/GTK2/Cairo port of MainWin.pas's scroll-text ticker (MIG-0098) -
 * the FULL system this time, not MIG-0097's static-text stand-in:
 *
 *  - AND-mask text rendering (MainWin.pas:3381's `BMP_VScroll.Canvas.
 *    Brush.Color := clWhite` + RedrawScroll's `CopyMode := cmSrcAnd`):
 *    the text is NOT drawn on a solid background - a white canvas with
 *    the text drawn onto it (in the real Font.Color, $606060) is
 *    bitwise-ANDed, channel by channel, against the skin's own
 *    background pixels at the scroll area. Since ANDing with white
 *    (0xFF) is the identity, the skin shows through completely except
 *    where letters are drawn, where it's darkened toward $606060 -
 *    the skin's own background art stays visible, tinted by the
 *    letter shapes, not covered by an opaque box.
 *  - Horizontal ticker-scroll for text wider than the display area
 *    (MainWin.pas:750-786's Scr_Left/HorScrl_Offset/Scr_Pause state
 *    machine - 1px per ~30ms tick, pausing ~1.5s at each end before
 *    reversing).
 *  - Vertical slide transition when a different playlist entry starts
 *    playing (MainWin.pas:663-736's Item_Displayed/Scroll_Distination/
 *    Scroll_Offset machinery) - simplified here to the common single-
 *    step (Prev/Next-adjacent) case, animated exactly like the
 *    original's own single-line transition; anything else (an
 *    arbitrary jump - Open, a distant playlist double-click, or a
 *    second transition arriving before the first finishes) snaps
 *    immediately instead of animating through, rather than
 *    reimplementing the original's full N-line/">16 away" jump-then-
 *    catch-up logic for cases this port's simpler single-song-at-a-
 *    time model rarely exercises the same way. See gui/src/ticker.c's
 *    own comments for the exact mapping.
 *  - Double-click to pause/resume the horizontal scroll (MainWin.pas:
 *    2092-2098's `Do_Scroll := not Do_Scroll`), and click-drag to
 *    manually scrub it (MoveScr/DoMovingScroll, MainWin.pas:2651-2663).
 */
#ifndef GUI_TICKER_H
#define GUI_TICKER_H

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>

/* MainWin.pas:60-64. */
#define GUI_TICKER_X 108
#define GUI_TICKER_Y 48
#define GUI_TICKER_WIDTH 197
#define GUI_TICKER_LINE_HEIGHT 24

typedef struct gui_ticker {
  bool has_content; /* MainWin.pas: Item_Displayed >= 0 */
  char text[600];   /* settled (non-transitioning) display text */
  double text_width; /* MainWin.pas: sw - cached via a scratch cairo
                       * context whenever `text` changes, so tick()/
                       * drag() don't need a live cairo_t of their own
                       * (GTK's motion/timeout callbacks don't have
                       * one to hand) */

  bool transitioning;
  char old_text[600];
  char new_text[600];
  int scroll_offset;  /* MainWin.pas: Scroll_Offset, 0..LINE_HEIGHT */
  int direction;       /* +1 = new text slides in from below (Next-like),
                         * -1 = from above (Prev-like) */

  int horiz_offset;    /* MainWin.pas: HorScrl_Offset */
  bool scroll_left;    /* MainWin.pas: Scr_Left */
  int scroll_pause;    /* MainWin.pas: Scr_Pause */
  bool do_scroll;      /* MainWin.pas: Do_Scroll - double-click toggle */

  bool dragging;       /* MainWin.pas: MoveScr.Clicked */
  int drag_old_x;       /* MainWin.pas: MoveScr.OldX (app-local, unscaled) */
} gui_ticker;

void gui_ticker_init(gui_ticker* t);

/* Call whenever a (possibly) new song starts playing, with the new
 * song's own display text and its immediate playlist neighbors' text
 * (NULL if there is no such neighbor) - mirrors PlayItem's `Scroll_
 * Distination := Index` (PlayList.pas:657) picking up ReprepareScroll's
 * neighbor pre-fetch (MainWin.pas:3440-3454). `is_next`/`is_prev`
 * tell the ticker whether `text` is adjacent to what's currently
 * displayed (enabling the animated single-step slide) - pass both
 * false for an arbitrary jump (snaps immediately, see file comment). */
void gui_ticker_set_target(gui_ticker* t, const char* text, bool is_next,
                            bool is_prev);

/* Call every visualizer-timer tick (~30ms, MainWin.pas: DoVisualisation
 * via VisTimerEvent) to advance the vertical slide and/or horizontal
 * scroll animation by one step. */
void gui_ticker_tick(gui_ticker* t);

/* Renders the ticker into its fixed skin-window rect (GUI_TICKER_X/Y/
 * WIDTH/LINE_HEIGHT) using the real AND-mask technique - `skin` is the
 * currently-loaded skin bitmap (gui_mainwin::skin.bitmap), read
 * directly for the background pixels under the ticker rather than
 * re-reading back from `cr` (draw_base_skin already painted the exact
 * same unscaled bitmap at (0,0) immediately before this runs, so
 * `skin`'s own pixel data at the ticker's rect IS what's already on
 * `cr` there). */
void gui_ticker_draw(const gui_ticker* t, cairo_t* cr, GdkPixbuf* skin);

/* MainWin.pas:2092-2098 - double-click anywhere in the ticker rect
 * toggles do_scroll. Caller does the rect hit-test and ssDouble check
 * (GDK's GDK_2BUTTON_PRESS) itself, same division of responsibility
 * as gui_visualizer_handle_click. */
void gui_ticker_toggle_scroll(gui_ticker* t);

/* MainWin.pas: MoveScr/DoMovingScroll - click-drag manual scrub, only
 * effective when settled (not mid vertical-transition) and the text
 * is actually wider than the display area (matching the original's
 * own `if sw <= scr_width then Exit; if Scroll_Distination <>
 * Item_Displayed then Exit;` guards). `dx` is the horizontal motion
 * delta since the last call (event->x - previous event->x), matching
 * DoMovingScroll's own PosX. */
void gui_ticker_drag(gui_ticker* t, int dx);

#endif /* GUI_TICKER_H */
