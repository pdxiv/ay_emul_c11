/* C11/GTK2 port of MainWin.pas's spectrum/amplitude visualizer
 * (MIG-0094) - `SensSpa`/`SensAmp`'s two display areas within the main
 * skinned window (the oscilloscope/`SensTime` time-display-mode toggle
 * is NOT ported here, see migration_debt.yaml - it's tied to a whole
 * separate time-digit-strip rendering feature this port doesn't have
 * yet, unrelated to the spectrum/amplitude visualizer itself).
 *
 * Traced end-to-end before implementing (per the user's explicit
 * request): despite the name, this is NOT a real signal-domain FFT
 * spectrum analyzer for AY-chip playback - RedrawVisSpectrum buckets
 * each of the 3 AY channels' own TONE-PERIOD register into a
 * log-scale bar position (CalculateSpectrumPoints/Spa_points) and uses
 * the channel's AMPLITUDE register as that bar's height, with an
 * envelope-aware amplitude/tone derivation (AYVisualisation's Calc
 * block) applied once per sampled point. BASS_DATA_FFT8192 only
 * appears in the separate BASS-decoded-stream path this port doesn't
 * have. The underlying sampling mechanism (AY.pas: FillVis/VisPoints,
 * a tick-driven historical ring buffer hooked into the synthesis loop
 * so the display time-aligns with the currently-audible sample) is
 * ported for real in engine/include/ay_engine/ay.h's ay_vis_point/
 * ay_engine_init_vis/ay_engine_get_vis_point, per the user's explicit
 * "implement this in the same way as the original PAS code does it"
 * answer - not approximated with a live-register-state read.
 */
#ifndef GUI_VISUALIZER_H
#define GUI_VISUALIZER_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>

#include "ay_engine/ay.h"

/* MainWin.pas:43-54. */
#define GUI_VIS_SPA_NUM 63 /* 91 - 26 - 2 */
#define GUI_VIS_SPA_WIDTH (GUI_VIS_SPA_NUM + 2)
#define GUI_VIS_SPA_HEIGHT 20
#define GUI_VIS_SPA_X 26
#define GUI_VIS_SPA_Y 34

#define GUI_VIS_AMP_WIDTH 17
#define GUI_VIS_AMP_HEIGHT 15
#define GUI_VIS_AMP_X 50
#define GUI_VIS_AMP_Y 18

typedef struct gui_visualizer {
  bool spectrum_checked; /* MainWin.pas: SpectrumChecked, default True */
  bool amp_checked;      /* MainWin.pas: IndicatorChecked, default True */

  /* MainWin.pas: Spa_points - log-scale bucket-boundary table
   * (CalculateSpectrumPoints), computed once from a fixed AY_FreqDef
   * (1773400, settings.pas) rather than each loaded file's own actual
   * chip clock - a documented simplification (this only affects the
   * cosmetic frequency-axis bar positions, see gui_visualizer_init's
   * own comment). */
  int spa_points[GUI_VIS_SPA_NUM + 1];

  /* MainWin.pas: Spa_prev (via PSpa_prev) - the decaying peak-marker
   * state, persists across the whole session (not reset per song
   * load, matching the original's own global). */
  int spa_prev[GUI_VIS_SPA_NUM];

  /* Per-frame render state, recomputed by gui_visualizer_tick and
   * consumed by gui_visualizer_draw - split out from spa_prev above
   * because RedrawVisSpectrum draws the BAR at the fresh instantaneous
   * value (spa_bar) while the separate falling PEAK MARKER dot
   * (spa_marker, only drawn when spa_has_marker) uses the not-yet-
   * decremented previous peak - see gui/src/visualizer.c's own
   * comment on this distinction, easy to get backwards from a
   * surface reading of the original's in-place PSpa_Piks reuse. */
  int spa_bar[GUI_VIS_SPA_NUM];
  int spa_marker[GUI_VIS_SPA_NUM];
  bool spa_has_marker[GUI_VIS_SPA_NUM];

  /* MainWin.pas: RedrawVisChannels' ca/cb/cc (post-Calc "visual"
   * amplitude, TSMode's max-across-chips collapsed away - see this
   * port's own established single-chip scope, MIG-0007). */
  int amp_a, amp_b, amp_c;
} gui_visualizer;

/* Computes spa_points and resets spa_piks/spa_prev/spectrum_checked/
 * amp_checked to their defaults. Call once at startup. */
void gui_visualizer_init(gui_visualizer* v);

/* MainWin.pas: AYVisualisation + RedrawVisChannels/RedrawVisSpectrum's
 * peak-hold-decay bookkeeping - call once per visualizer timer tick
 * (~30ms, MainWin.pas: VisTimerPeriod) with `engine` (player_ay_engine
 * of the currently-loaded player) and `smp` (the cumulative output
 * sample count, gui_playback's own frames_played) to advance
 * spa_piks/spa_prev. `engine` may be NULL (nothing loaded) - matches
 * AYVisualisation running with CP=nil (Spa_piks decays toward silence
 * exactly as when nothing new is being played). */
void gui_visualizer_tick(gui_visualizer* v, ay_engine* engine, uint32_t smp);

/* Draws the amplitude bars (if amp_checked) and spectrum bars (if
 * spectrum_checked) at their fixed skin-window coordinates - call from
 * the main window's expose/draw handler, after the base skin bitmap
 * has been redrawn (there's no separate background buffer to restore
 * here, since this port always redraws the whole skin fresh each
 * frame - see gui/src/mainwin.c's own draw_base_skin). */
void gui_visualizer_draw(const gui_visualizer* v, cairo_t* cr);

/* Hit-tests (mx,my) against the spectrum/amplitude display areas and
 * toggles spectrum_checked/amp_checked respectively (MainWin.pas:
 * ButSpaClick/ButAmpClick) if hit. Returns true if either was
 * toggled (so the caller knows to queue a redraw). */
bool gui_visualizer_handle_click(gui_visualizer* v, int mx, int my);

#endif /* GUI_VISUALIZER_H */
