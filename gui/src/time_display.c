#include "gui/time_display.h"

#include <math.h>
#include <stdio.h>

/* MainWin.pas:71-72 (non-Windows branch - this port has no Windows
 * build). RedrawTime switches to the smaller font once the H:MM:SS
 * form would be too wide for the fixed-width box (TmS >= 60*60). */
#define TIME_FONTHEIGHT 19
#define TIME_FONTHEIGHT_SMALL 13

/* MainWin.pas:3375: BMP_Time.Canvas.Font.Color := $464646 - equal RGB
 * channels (Delphi's $BBGGRR order is moot here), same convention as
 * ticker.c's own TICKER_COLOR comment. */
#define TIME_COLOR (0x46 / 255.0)

void gui_time_display_init(gui_time_display* td) { td->mode = 0; }

void gui_time_display_click(gui_time_display* td) {
  td->mode = (td->mode + 1) % 3;
}

bool gui_time_display_handle_click(gui_time_display* td, int mx, int my) {
  if (mx >= GUI_TIME_X && mx < GUI_TIME_X + GUI_TIME_WIDTH && my >= GUI_TIME_Y &&
      my < GUI_TIME_Y + GUI_TIME_HEIGHT) {
    gui_time_display_click(td);
    return true;
  }
  return false;
}

/* PlayList.pas: TimeSToStr (digit-by-digit in the original) -
 * reimplemented via snprintf, same convention/output shape as
 * gui/src/dialogs/jmptime.c's own format_time: "M:SS" normally, or
 * "H:MM:SS" once there's a whole hour or more. */
static void format_time_str(int total_seconds, char* out, size_t cap) {
  if (total_seconds < 0) total_seconds = 0;
  int h = total_seconds / 3600;
  int m = (total_seconds % 3600) / 60;
  int s = total_seconds % 60;
  if (h > 0) {
    snprintf(out, cap, "%d:%02d:%02d", h, m, s);
  } else {
    snprintf(out, cap, "%d:%02d", m, s);
  }
}

/* Same GDI-Height-to-Cairo-em-size conversion as ticker.c's own
 * ticker_font_size (see its comment for the full rationale) - this
 * font/weight combination's actual rendered cell height, not its
 * nominal em-size, is what needs to hit target_height pixels. Kept as
 * its own small self-contained helper (not shared with ticker.c)
 * matching this project's own established per-file self-containment
 * convention (e.g. pt1_file.c/pt3_file.c's duplicated note tables). */
static double font_size_for_height(double target_height) {
  const double trial_size = 100.0;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(surf);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, trial_size);
  cairo_font_extents_t fe;
  cairo_font_extents(cr, &fe);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return trial_size * target_height / fe.height;
}

void gui_time_display_draw(const gui_time_display* td, cairo_t* cr,
                            const gui_playback* pb) {
  if (!pb->loaded) return;
  double duration = gui_playback_duration_seconds(pb);
  /* MainWin.pas: `if Time_ms <> 0 then ...` - Time_ms=0 (no known
   * duration) leaves the box showing just its plain background, same
   * effect as ClearTimeInd. */
  if (duration <= 0.0) return;

  double position = gui_playback_position_seconds(pb);
  bool negative = false;
  double temp_d;
  switch (td->mode) {
    case 1: { /* MainWin.pas: TimeMode=1, remaining */
      double rem = duration - position;
      if (rem < 0.0) rem = 0.0;
      temp_d = rem;
      negative = true;
      break;
    }
    case 2: /* TimeMode=2, total */
      temp_d = duration;
      break;
    default: /* TimeMode=0, elapsed */
      temp_d = position;
      break;
  }
  if (temp_d < 0.0) temp_d = 0.0;
  int temp = (int)lround(temp_d);

  char digits[32];
  char text[40];
  format_time_str(temp, digits, sizeof(digits));
  if (negative) {
    snprintf(text, sizeof(text), "-%s", digits);
  } else {
    snprintf(text, sizeof(text), "%s", digits);
  }

  cairo_save(cr);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  /* MainWin.pas:580-583: `if TmS < 60*60 then ... time_fontheight else
   * ... time_fontheightsmall` - TmS is abs(TimeShown), i.e. the raw
   * unsigned seconds value before the sign/formatting above. */
  double target_height =
      temp < 3600 ? TIME_FONTHEIGHT : TIME_FONTHEIGHT_SMALL;
  cairo_set_font_size(cr, font_size_for_height(target_height));
  cairo_set_source_rgb(cr, TIME_COLOR, TIME_COLOR, TIME_COLOR);

  cairo_text_extents_t ext;
  cairo_text_extents(cr, text, &ext);
  cairo_font_extents_t fe;
  cairo_font_extents(cr, &fe);

  /* MainWin.pas:584-585: `CurTimeJ := time_width - TextWidth; if
   * CurTimeJ > 0 then CurTimeJ := CurTimeJ div 2;` - centers text that
   * fits; text too wide to fit is right-aligned-overflow instead (the
   * un-halved negative offset), not clipped or left-aligned. */
  double j = (double)GUI_TIME_WIDTH - ext.x_advance;
  if (j > 0.0) j /= 2.0;
  /* MainWin.pas:586-590: `(time_height - TextHeight) div 2 {+1 non-
   * Windows}` - TextOut's Y is the text's TOP edge, not baseline, so
   * fe.ascent is added to reach the baseline Cairo's own move_to/
   * show_text expect (same conversion ticker.c's draw_and_masked
   * already uses for its own vertical centering). */
  double h = (GUI_TIME_HEIGHT - fe.height) / 2.0 + 1.0;

  cairo_move_to(cr, GUI_TIME_X + j, GUI_TIME_Y + h + fe.ascent);
  cairo_show_text(cr, text);
  cairo_restore(cr);
}
