#include "gui/visualizer.h"

#include <math.h>
#include <string.h>

/* MainWin.pas: RedrawVisSpectrum's MaxVal argument (5412) and
 * RedrawVisChannels' mh argument (5406) respectively - fixed scale
 * constants, not derived from anything runtime. */
#define VIS_SPA_MAX_VAL 31
#define VIS_AMP_MH 30

/* settings.pas: AY_FreqDef - see visualizer.h's own comment on why a
 * fixed default stands in for each file's actual chip clock here. */
#define VIS_AY_FREQ_DEF 1773400.0

void gui_visualizer_init(gui_visualizer* v) {
  memset(v, 0, sizeof(*v));
  v->spectrum_checked = true;
  v->amp_checked = true;

  /* MainWin.pas:603-608, CalculateSpectrumPoints. */
  v->spa_points[0] = 0xFFF;
  double k = -log(16.0 * 22050.0 * 0xFFF / VIS_AY_FREQ_DEF);
  for (int i = 1; i <= GUI_VIS_SPA_NUM; i++) {
    v->spa_points[i] =
        (int)(0xFFF * exp(k * (double)i / (double)GUI_VIS_SPA_NUM) + 0.5);
  }
}

void gui_visualizer_tick(gui_visualizer* v, ay_engine* engine, uint32_t smp) {
  const ay_vis_point* cp = engine ? ay_engine_get_vis_point(engine, smp) : NULL;

  if (cp) {
    v->amp_a = cp->amp_a;
    v->amp_b = cp->amp_b;
    v->amp_c = cp->amp_c;
  } else {
    v->amp_a = v->amp_b = v->amp_c = 0;
  }

  /* MainWin.pas:820-876, RedrawVisSpectrum. Bucket each channel's own
   * tone-period register into the log-scale bar its period falls
   * into, taking the max amplitude among channels that land in the
   * same bar (spa_points[i+1] < Tn <= spa_points[i], i.e. bucket
   * boundaries run from high tone-period/low-frequency at index 0
   * down to low tone-period/high-frequency at index spa_num - a
   * higher AY tone PERIOD means a LOWER audible pitch). */
  int fresh[GUI_VIS_SPA_NUM];
  memset(fresh, 0, sizeof(fresh));
  if (cp) {
    for (int i = 0; i < GUI_VIS_SPA_NUM; i++) {
      int lo = v->spa_points[i + 1], hi = v->spa_points[i];
      if (cp->tn_a > lo && cp->tn_a <= hi && fresh[i] < cp->amp_a)
        fresh[i] = cp->amp_a;
      if (cp->tn_b > lo && cp->tn_b <= hi && fresh[i] < cp->amp_b)
        fresh[i] = cp->amp_b;
      if (cp->tn_c > lo && cp->tn_c <= hi && fresh[i] < cp->amp_c)
        fresh[i] = cp->amp_c;
    }
  }

  for (int i = 0; i < GUI_VIS_SPA_NUM; i++) {
    v->spa_bar[i] = fresh[i]; /* the bar itself always tracks the
                                * instantaneous value, no smoothing */
    if (v->spa_prev[i] > fresh[i]) {
      /* The old decaying peak is still above the fresh value - draw a
       * marker dot at its (pre-decrement) height this frame, then
       * decay it for next frame (MaxVal+1 div 16 = 2 steps/frame,
       * unclamped - matches Pascal's plain Dec(), which can go
       * negative; a negative value just means "no marker" once it
       * drops below any future fresh[i], same as zero would). */
      v->spa_marker[i] = v->spa_prev[i];
      v->spa_has_marker[i] = true;
      v->spa_prev[i] -= (VIS_SPA_MAX_VAL + 1) / 16;
    } else {
      v->spa_has_marker[i] = false;
      v->spa_prev[i] = fresh[i];
    }
  }
}

void gui_visualizer_draw(const gui_visualizer* v, cairo_t* cr) {
  cairo_save(cr);
  cairo_set_source_rgb(cr, 0, 0, 0); /* MainWin.pas: default TCanvas.Pen -
                                       * see visualizer.c's own header
                                       * comment on why this is black */
  cairo_set_line_width(cr, 1);

  if (v->amp_checked) {
    /* MainWin.pas:791-816, RedrawVisChannels - columns 1/8/15 within
     * the amp display box for channels A/B/C. */
    static const int cols[3] = {1, 8, 15};
    int amps[3] = {v->amp_a, v->amp_b, v->amp_c};
    for (int ch = 0; ch < 3; ch++) {
      if (amps[ch] <= 0) continue;
      double x = GUI_VIS_AMP_X + cols[ch] + 0.5;
      double y_top = GUI_VIS_AMP_Y + GUI_VIS_AMP_HEIGHT + 1 -
                     (double)amps[ch] * GUI_VIS_AMP_HEIGHT / VIS_AMP_MH;
      double y_bottom = GUI_VIS_AMP_Y + GUI_VIS_AMP_HEIGHT;
      cairo_move_to(cr, x, y_bottom);
      cairo_line_to(cr, x, y_top);
      cairo_stroke(cr);
    }
  }

  if (v->spectrum_checked) {
    /* MainWin.pas:854-873, RedrawVisSpectrum's drawing half (the
     * bucketing/decay half already ran in gui_visualizer_tick). */
    for (int i = 0; i < GUI_VIS_SPA_NUM; i++) {
      if (v->spa_bar[i] > 0) {
        double x = GUI_VIS_SPA_X + i + 1 + 0.5;
        double y_top = GUI_VIS_SPA_Y + (double)(VIS_SPA_MAX_VAL - v->spa_bar[i]) *
                                            GUI_VIS_SPA_HEIGHT / VIS_SPA_MAX_VAL;
        double y_bottom = GUI_VIS_SPA_Y + GUI_VIS_SPA_HEIGHT;
        cairo_move_to(cr, x, y_bottom);
        cairo_line_to(cr, x, y_top);
        cairo_stroke(cr);
      }
      if (v->spa_has_marker[i] && v->spa_marker[i] > 0) {
        int j = GUI_VIS_SPA_Y + (VIS_SPA_MAX_VAL - v->spa_marker[i]) *
                                     GUI_VIS_SPA_HEIGHT / VIS_SPA_MAX_VAL;
        cairo_save(cr);
        cairo_set_source_rgb(cr, 10.0 / 255.0, 10.0 / 255.0, 10.0 / 255.0);
        cairo_rectangle(cr, GUI_VIS_SPA_X + i, j, 3, 1);
        cairo_fill(cr);
        cairo_restore(cr);
      }
    }
  }

  cairo_restore(cr);
}

bool gui_visualizer_handle_click(gui_visualizer* v, int mx, int my) {
  if (mx >= GUI_VIS_SPA_X && mx < GUI_VIS_SPA_X + GUI_VIS_SPA_WIDTH &&
      my >= GUI_VIS_SPA_Y && my < GUI_VIS_SPA_Y + GUI_VIS_SPA_HEIGHT) {
    v->spectrum_checked = !v->spectrum_checked; /* MainWin.pas: ButSpaClick */
    return true;
  }
  if (mx >= GUI_VIS_AMP_X && mx < GUI_VIS_AMP_X + GUI_VIS_AMP_WIDTH &&
      my >= GUI_VIS_AMP_Y && my < GUI_VIS_AMP_Y + GUI_VIS_AMP_HEIGHT) {
    v->amp_checked = !v->amp_checked; /* MainWin.pas: ButAmpClick */
    return true;
  }
  return false;
}
