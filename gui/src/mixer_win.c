#include "gui/mixer_win.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ay_engine/player.h"
#include "ay_player/alsa_output.h"
#include "gui/alsa_mixer.h"
#include "gui/dialogs/mxhelper.h"
#include "gui/mainwin.h"
/* MIG-0132: build-time-generated widget CONSTRUCTION for GBChAmp's 9
 * amplitude sliders (build/generated/mixer_win_gen.h, regenerated from
 * the real ay_emul/Mixer.lfm every build via tools/lfm_gen/lfm_gen.py
 * --widgets-only - see that script's own header comment and gui/
 * Makefile's GEN_SRC comment). This is the pilot slice for a wider
 * "generate widget construction from .lfm at build time" effort
 * (migration_debt.yaml MIG-0132) - only GBChAmp is converted so far,
 * every other tab/groupbox in this file is still hand-written
 * gtk_*_new() calls, unchanged. The generated struct's fields are named
 * after Mixer.lfm's own object names verbatim (TBAmpAL, EAmpAL, ...) -
 * only widget TYPE/orientation/range/caption/initial-value/radio-
 * grouping come from it; ALL packing (gtk_box_pack_start into this
 * file's own idiomatic rows) and ALL signal wiring
 * (g_signal_connect) stay 100% hand-written below, exactly as before
 * this existed - see mixer_win_gen.h's own generated comment for why
 * that split is deliberate. */
#include "mixer_win_gen.h"

/* Applies AL/AR/BL/BR/CL/CR and chip_type on every slider/radio change,
 * matching MainWin.pas's Set_Mode_Manual ("assign fields, then
 * Calculate_Level_Tables2") - see player_ay_engine's own comment on the
 * benign cross-thread data race this shares with gui_playback's
 * `volume` (the playback thread only ever reads the resulting fixed-
 * size level_al[32]/etc. int arrays, never reallocated, so a torn read
 * mid-recalculation is, at worst, one glitchy sample - not a crash). */
static void apply_and_recalc(gui_mixer_win* w) {
  if (!w->playback->loaded) return;
  ay_engine* e = player_ay_engine(&w->playback->pair.primary);
  e->index_al = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_al));
  e->index_ar = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_ar));
  e->index_bl = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_bl));
  e->index_br = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_br));
  e->index_cl = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_cl));
  e->index_cr = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_cr));
  e->beeper_max = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_bpr));
  e->atari_dma_max = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_dma));
  /* Mixer.pas: TBPreAmp/EPreAmp (MIG-0131) - `pre_amp_max` is left
   * untouched here, matching real Pascal: PreAmpMax is never reassigned
   * outside init (AY.pas:153). */
  e->pre_amp = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_preamp));
  e->chip_type = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_ay))
                     ? AY_CHIP_TYPE_AY
                     : AY_CHIP_TYPE_YM;
  ay_engine_calculate_level_tables(e);
}

/* Mixer.pas: EAmpAL/AR/BL/BR/CL/CR/EAmpBpr/EAmpDMA/EPreAmp - keeps each
 * typed-entry box showing the same value as its paired slider, after
 * either one changes (MIG-0131; see mixer_win.h's own comment on why
 * one shared function instead of 9 near-identical ones). */
static void sync_amp_entries(gui_mixer_win* w) {
  struct {
    GtkWidget* scale;
    GtkWidget* entry;
  } pairs[] = {
      {w->scale_al, w->entry_al},     {w->scale_ar, w->entry_ar},
      {w->scale_bl, w->entry_bl},     {w->scale_br, w->entry_br},
      {w->scale_cl, w->entry_cl},     {w->scale_cr, w->entry_cr},
      {w->scale_bpr, w->entry_bpr},   {w->scale_dma, w->entry_dma},
      {w->scale_preamp, w->entry_preamp},
  };
  char buf[8];
  for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
    snprintf(buf, sizeof(buf), "%d",
             (int)gtk_range_get_value(GTK_RANGE(pairs[i].scale)));
    gtk_entry_set_text(GTK_ENTRY(pairs[i].entry), buf);
  }
}

/* Mixer.pas: RBResamAvgClick/RBResamFIRClick calling
 * FrmMain.SetFilter(0)/SetFilter(1) - re-derives the FIR coefficients
 * for the newly-chosen quality at the file's CURRENT AY clock (unlike
 * apply_and_recalc's fields, filter_quality doesn't feed
 * ay_engine_calculate_level_tables; it needs a real ay_engine_set_filter
 * call, exactly like MainWin.pas's Set_Chip_Frq performs after every
 * quality change - see player_set_chip_freq's own comment). */
static void apply_filter_quality(gui_mixer_win* w) {
  if (!w->playback->loaded) return;
  ay_engine* e = player_ay_engine(&w->playback->pair.primary);
  int quality = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_filt_fir))
                    ? 1
                    : 0;
  e->filter_quality = quality;
  ay_engine_set_filter(e, quality, player_get_ay_freq(&w->playback->pair.primary),
                        w->playback->sample_rate);
}

/* Mixer.pas: RBZ80FrqZXClick/RBZ80FrqPntClick/RBZ80FrqOtherClick/
 * EZ80FrqOtherEditingDone (MIG-0120) - reads whichever Z80-clock radio is
 * active (ZX=3494400, Pentagon=3500000, Other=EZ80FrqOther's text) and
 * applies it via player_set_frq_z80 (a no-op for every format except AY,
 * see its own doc comment - so this silently does nothing for a loaded
 * non-.ay file, matching a disabled control rather than erroring, same
 * convention as apply_and_recalc's own playback->loaded guard). */
static void apply_z80(gui_mixer_win* w) {
  if (!w->playback->loaded) return;
  int freq;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_z80_zx))) {
    freq = 3494400;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_z80_pnt))) {
    freq = 3500000;
  } else {
    freq = atoi(gtk_entry_get_text(GTK_ENTRY(w->entry_z80_other)));
  }
  player_set_frq_z80(&w->playback->pair.primary, freq);
}

/* Mixer.pas: RBMCFrqSTClick/RBMCFrqOtherClick/EMCFrqOtherEditingDone
 * (MIG-0120) - see apply_z80's own comment; player_set_mc68000_freq is a
 * no-op for every format except SNDH. */
static void apply_mc(gui_mixer_win* w) {
  if (!w->playback->loaded) return;
  double freq;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_mc_st))) {
    freq = 8000000.0;
  } else {
    freq = atof(gtk_entry_get_text(GTK_ENTRY(w->entry_mc_other)));
  }
  player_set_mc68000_freq(&w->playback->pair.primary, freq);
}

/* Mixer.pas: RBMFPFrq1613Click/RBMFPFrqSTClick/RBMFPFrqOtherClick/
 * EMFPFrqOtherEditingDone (MIG-0120) - see apply_z80's own comment;
 * player_set_mfp_freq is a no-op for every format except SNDH. Mode 0
 * (Auto) ignores its `freq` argument (player_set_mfp_freq's own
 * comment), so any value works there. */
static void apply_mfp(gui_mixer_win* w) {
  if (!w->playback->loaded) return;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_mfp_auto))) {
    player_set_mfp_freq(&w->playback->pair.primary, 0, 0);
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_mfp_st))) {
    player_set_mfp_freq(&w->playback->pair.primary, 1, 2457600);
  } else {
    int freq = atoi(gtk_entry_get_text(GTK_ENTRY(w->entry_mfp_other)));
    player_set_mfp_freq(&w->playback->pair.primary, 1, freq);
  }
}

/* Mixer.pas: RBChFrqZX/Pnt/ST/CPCClick/RBChFrqOtherClick/
 * EChFrqOtherEditingDone (MIG-0124) - a LIVE AY-chip clock override
 * (player_set_chip_freq, MainWin.pas:1534-1552's own Set_Chip_Frq),
 * unlike apply_z80/apply_mc/apply_mfp above which are all load-time-
 * only. Applies to every format (player_set_chip_freq's own universal
 * delay_in_tiks/tik_re recompute), with format-specific derived-field
 * updates for AY/VTX/YM handled inside player_set_chip_freq itself. */
static void apply_chip_freq(gui_mixer_win* w) {
  if (!w->playback->loaded) return;
  int freq;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_zx))) {
    freq = 1773400;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_pnt))) {
    freq = 1750000;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_st))) {
    freq = 2000000;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_cpc))) {
    freq = 1000000;
  } else {
    freq = atoi(gtk_entry_get_text(GTK_ENTRY(w->entry_chfrq_other)));
  }
  player_set_chip_freq(&w->playback->pair.primary, freq,
                        w->playback->sample_rate);
}

/* Mixer.pas: RBEIntFrqZXClick/RBIntFrqPntClick/RBIntFrqOtherClick/
 * EIntFrqOtherEditingDone (MIG-0124) - a LIVE VBL/interrupt-frequency
 * override (player_set_player_freq, MainWin.pas:2043-2062's own
 * Set_Player_Frq2/Set_Player_Frq) - YM/VTX only, a no-op for every
 * other format (player_set_player_freq's own doc comment). Values are
 * plain Hz-x1000 integers, matching this window's own pre-existing
 * EIntFrqCur/label_int_freq_cur "(Hz x1000)" display convention (see
 * mixer_win.h's own struct comment for why this doesn't reintroduce
 * real Pascal's fractional-kHz EIntFrqOther text format). */
static void apply_player_freq(gui_mixer_win* w) {
  if (!w->playback->loaded) return;
  int freq;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_intfrq_zx))) {
    freq = 50000;
  } else if (gtk_toggle_button_get_active(
                 GTK_TOGGLE_BUTTON(w->rb_intfrq_pnt))) {
    freq = 48828;
  } else {
    freq = atoi(gtk_entry_get_text(GTK_ENTRY(w->entry_intfrq_other)));
  }
  player_set_player_freq(&w->playback->pair.primary, freq);
}

static void on_z80_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  if (!gtk_toggle_button_get_active(toggle)) return;
  apply_z80(w);
}
static void on_z80_entry_activate(GtkEntry* entry, gpointer data) {
  (void)entry;
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_z80_other), TRUE);
  apply_z80(w); /* rb_z80_other was already active: the toggled handler
                  * above won't re-fire, so apply explicitly here,
                  * matching EZ80FrqOtherEditingDone's own unconditional
                  * apply-then-Set_Z80Frqs shape */
}

static void on_mc_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  if (!gtk_toggle_button_get_active(toggle)) return;
  apply_mc(w);
}
static void on_mc_entry_activate(GtkEntry* entry, gpointer data) {
  (void)entry;
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mc_other), TRUE);
  apply_mc(w);
}

static void on_mfp_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  if (!gtk_toggle_button_get_active(toggle)) return;
  apply_mfp(w);
}
static void on_mfp_entry_activate(GtkEntry* entry, gpointer data) {
  (void)entry;
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mfp_other), TRUE);
  apply_mfp(w);
}

static void on_chfrq_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  if (!gtk_toggle_button_get_active(toggle)) return;
  apply_chip_freq(w);
}
static void on_chfrq_entry_activate(GtkEntry* entry, gpointer data) {
  (void)entry;
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_other), TRUE);
  apply_chip_freq(w);
}

/* Mixer.pas: EOUTStPerFrmEditingDone -> FrmMain.Set_N_TactS (MIG-0131) -
 * see player_set_n_tact's own doc comment: a LIVE MaxTStates override,
 * silently no-op'd (by player_set_n_tact itself) if out of range or the
 * loaded format isn't AY. */
static void on_n_tact_activate(GtkEntry* entry, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (!w->playback->loaded) return;
  int val = atoi(gtk_entry_get_text(entry));
  player_set_n_tact(&w->playback->pair.primary, val);
}

/* Mixer.pas: EIntOffsEditingDone (MIG-0131) - `Val(EIntOffs.Text, Temp1,
 * Temp2); if (Temp2=0) and (Temp1>=0) and (Temp1<MaxTStates) then
 * IntOffset := Temp1;`. See mixer_win.h's own comment for why this has
 * no backing player/mainwin state - clamped and echoed back into the
 * entry itself. */
static void on_int_offset_activate(GtkEntry* entry, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  int n_tact = w->playback->loaded
                   ? player_get_n_tact(&w->playback->pair.primary)
                   : 0;
  int val = atoi(gtk_entry_get_text(entry));
  if (val < 0) val = 0;
  if (n_tact > 0 && val >= n_tact) val = n_tact - 1;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", val);
  gtk_entry_set_text(entry, buf);
}

static void on_intfrq_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  if (!gtk_toggle_button_get_active(toggle)) return;
  apply_player_freq(w);
}
static void on_intfrq_entry_activate(GtkEntry* entry, gpointer data) {
  (void)entry;
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_intfrq_other), TRUE);
  apply_player_freq(w);
}

static void on_scale_changed(GtkRange* range, gpointer data) {
  (void)range;
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return; /* programmatic update from on_sync_timer, not
                            * a real user change - see mixer_win.h */
  apply_and_recalc(w);
  sync_amp_entries(w);
}

/* Mixer.pas: EAmpAL/AR/BL/BR/CL/CR/EAmpBpr/EAmpDMA/EPreAmp EditingDone
 * (MIG-0131) - `Val(EAmpAL.Text, A, Cde); if (Cde=0) and (A in [0..255])
 * then FrmMain.SetChan2(A, 0)`. Clamps to [0,255] rather than rejecting
 * out-of-range/unparseable text outright (atoi's own 0-on-failure
 * fallback already lands in range) - simpler than replicating Val's
 * exact error-code semantics for a text box whose own paired slider
 * immediately shows the user what was actually applied. Setting the
 * slider re-fires on_scale_changed above, which re-applies and re-syncs
 * every entry (including this one, idempotently). */
static void on_amp_entry_activate(GtkEntry* entry, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  GtkWidget* scale = GTK_WIDGET(g_object_get_data(G_OBJECT(entry), "scale"));
  int val = atoi(gtk_entry_get_text(entry));
  if (val < 0) val = 0;
  if (val > 255) val = 255;
  gtk_range_set_value(GTK_RANGE(scale), val);
}

static void on_chip_type_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  if (!gtk_toggle_button_get_active(toggle)) return; /* fires for both the
                                                        * newly-active and
                                                        * newly-inactive
                                                        * radio in a group -
                                                        * only act once */
  apply_and_recalc(w);
}

static void on_filter_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  if (!gtk_toggle_button_get_active(toggle)) return; /* same single-fire
                                                        * guard as
                                                        * on_chip_type_
                                                        * toggled above */
  apply_filter_quality(w);
}

/* Mixer.pas: Set_Z80Frqs (734-745) - selects the matching preset radio
 * for the file's CURRENT frq_z80 (player_get_frq_z80, 0 for every
 * non-AY format), or "Other" with its value filled into the entry for
 * anything else - exactly the original's own case/else shape. Called
 * from on_sync_timer under the `syncing` guard, so this also re-syncs
 * the radio selection back to a freshly-loaded file's own default right
 * after Open/Next/Prev (matching the sibling amplitude-slider resync
 * this window already does). */
static void set_z80_frqs(gui_mixer_win* w) {
  int freq = player_get_frq_z80(&w->playback->pair.primary);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", freq);
  gtk_label_set_text(GTK_LABEL(w->label_z80_cur), buf);
  if (freq == 3494400) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_z80_zx), TRUE);
  } else if (freq == 3500000) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_z80_pnt), TRUE);
  } else {
    gtk_entry_set_text(GTK_ENTRY(w->entry_z80_other), buf);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_z80_other), TRUE);
  }
}

/* Mixer.pas: Set_MC68KFrqs (747-756) - same shape as set_z80_frqs above. */
static void set_mc_frqs(gui_mixer_win* w) {
  double freq = player_get_mc68000_freq(&w->playback->pair.primary);
  char buf[32];
  snprintf(buf, sizeof(buf), "%.0f", freq);
  gtk_label_set_text(GTK_LABEL(w->label_mc_cur), buf);
  if (freq == 8000000.0) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mc_st), TRUE);
  } else {
    gtk_entry_set_text(GTK_ENTRY(w->entry_mc_other), buf);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mc_other), TRUE);
  }
}

/* Mixer.pas: Set_MFPFrqs (719-732) - mode-first, THEN value (unlike
 * set_z80_frqs/set_mc_frqs's pure value-based case): MFPTimerMode=0
 * always selects "Auto" regardless of the numeric value; only mode=1
 * falls into the value-based ST/Other split. player_get_mfp_freq alone
 * can't distinguish Auto from a manual override that happens to equal
 * the Auto-computed value, so this mirrors Mixer.pas's own two-field
 * (mode, freq) read by reading atari.mfp_timer_mode directly - the ONE
 * place in this file that reaches past the player.h accessor layer,
 * since no player_get_mfp_mode exists (this readout is the only
 * caller that would ever need it). */
static void set_mfp_frqs(gui_mixer_win* w) {
  double freq = player_get_mfp_freq(&w->playback->pair.primary);
  char buf[32];
  snprintf(buf, sizeof(buf), "%.0f", freq);
  gtk_label_set_text(GTK_LABEL(w->label_mfp_cur), buf);
  bool is_auto =
      w->playback->pair.primary.format == PLAYER_FORMAT_SNDH &&
      w->playback->pair.primary.as.sndh.atari.mfp_timer_mode == 0;
  if (is_auto) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mfp_auto), TRUE);
  } else if (freq == 2457600.0) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mfp_st), TRUE);
  } else {
    gtk_entry_set_text(GTK_ENTRY(w->entry_mfp_other), buf);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mfp_other), TRUE);
  }
}

/* Mixer.pas: Set_Frqs (757-770) - same value-based case/else shape as
 * set_z80_frqs above, four presets instead of two. */
static void set_chip_frqs(gui_mixer_win* w) {
  int freq = player_get_ay_freq(&w->playback->pair.primary);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", freq);
  if (freq == 1773400) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_zx), TRUE);
  } else if (freq == 1750000) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_pnt), TRUE);
  } else if (freq == 2000000) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_st), TRUE);
  } else if (freq == 1000000) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_cpc), TRUE);
  } else {
    gtk_entry_set_text(GTK_ENTRY(w->entry_chfrq_other), buf);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_other), TRUE);
  }
}

/* Mixer.pas: Set_Pl_Frqs (770-780) - same shape as set_chip_frqs above,
 * two presets. */
static void set_player_frqs(gui_mixer_win* w) {
  int freq = player_get_int_freq(&w->playback->pair.primary);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", freq);
  if (freq == 50000) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_intfrq_zx), TRUE);
  } else if (freq == 48828) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_intfrq_pnt), TRUE);
  } else {
    gtk_entry_set_text(GTK_ENTRY(w->entry_intfrq_other), buf);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_intfrq_other),
                                  TRUE);
  }
}

/* Mixer.pas: EOUTStPerFrm's own periodic refresh (MIG-0131) - no preset
 * radio group here (unlike set_z80_frqs etc above), just a plain
 * always-refresh-to-current-value display/edit box, 0 for any non-AY
 * format (player_get_n_tact's own doc comment). */
static void set_n_tact_entry(gui_mixer_win* w) {
  int nt = w->playback->loaded ? player_get_n_tact(&w->playback->pair.primary)
                                : 0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", nt);
  gtk_entry_set_text(GTK_ENTRY(w->entry_n_tact), buf);
}

/* Re-syncs every control to the currently-loaded file's actual values
 * (see mixer_win.h's own comment on why - a fresh file load resets
 * ay_engine to its own defaults). Sets w->syncing so the change
 * handlers above skip apply_and_recalc for these programmatic updates,
 * instead of GLib's g_signal_handlers_block_by_func (see mixer_win.h's
 * own comment on why that's avoided here). */
static gboolean on_sync_timer(gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;

  /* Mixer.pas: GBSRate/GBChans/GBBRate/GBBuffs/GBDevice are all
   * disabled while playing (MainWin.pas:1073-1080 `PlayCurrent`) and
   * re-enabled on stop (MainWin.pas:1023-1030 `RestoreControls`) -
   * `IsPlaying` stays true across a pause, so pausing does NOT
   * re-enable these (matching gui_playback_is_paused's own doc
   * comment on that same real semantic, used elsewhere in mainwin.c). */
  bool ds_locked = w->mw->file_loaded && w->playback->thread_started &&
                    !gui_playback_is_finished(w->playback);
  gtk_widget_set_sensitive(w->ds_frame_srate, !ds_locked);
  gtk_widget_set_sensitive(w->ds_frame_chans, !ds_locked);
  gtk_widget_set_sensitive(w->ds_frame_brate, !ds_locked);
  gtk_widget_set_sensitive(w->ds_frame_buffs, !ds_locked);
  gtk_widget_set_sensitive(w->ds_frame_device, !ds_locked);

  if (!w->playback->loaded) {
    /* No file loaded - no output, so no overflow is possible (matches
     * Mixer.lfm's own default-hidden Visible property, before the first
     * Calculate_Level_Tables2 call ever runs). */
    gtk_widget_hide(w->label_ay_ovfl);
    gtk_widget_hide(w->label_ts_ovfl);
    gtk_widget_hide(w->label_dma_ovfl);
    return TRUE;
  }
  ay_engine* e = player_ay_engine(&w->playback->pair.primary);

  w->syncing = true;
  gtk_range_set_value(GTK_RANGE(w->scale_al), e->index_al);
  gtk_range_set_value(GTK_RANGE(w->scale_ar), e->index_ar);
  gtk_range_set_value(GTK_RANGE(w->scale_bl), e->index_bl);
  gtk_range_set_value(GTK_RANGE(w->scale_br), e->index_br);
  gtk_range_set_value(GTK_RANGE(w->scale_cl), e->index_cl);
  gtk_range_set_value(GTK_RANGE(w->scale_cr), e->index_cr);
  gtk_range_set_value(GTK_RANGE(w->scale_bpr), e->beeper_max);
  gtk_range_set_value(GTK_RANGE(w->scale_dma), e->atari_dma_max);
  gtk_range_set_value(GTK_RANGE(w->scale_preamp), e->pre_amp);
  sync_amp_entries(w);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_ay),
                                e->chip_type == AY_CHIP_TYPE_AY);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_ym),
                                e->chip_type == AY_CHIP_TYPE_YM);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_filt_fir),
                                e->filter_quality != 0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_filt_avg),
                                e->filter_quality == 0);

  /* Mixer.pas: Calculate_Level_Tables2 (MainWin.pas:1799-1816, MIG-0131)
   * - clip-warning label visibility, computed fresh every tick since the
   * underlying level tables can change from any of the slider handlers
   * above (or a fresh file load resetting them). */
  {
    int max_level = ay_engine_get_max_level(e);
    /* Real ay_engine::sample_bits (NOT w->playback->bits_per_sample, the
     * user's own REQUESTED Digital-Sound-tab setting) - these two can
     * differ: player_pair_make_buffer unconditionally forces sample_
     * bits=16 for genuinely TS-paired/TS-mode playback regardless of the
     * requested bit depth (see player.h's own doc comment on that
     * already-known, already-accepted constraint) - matching real
     * Pascal's own Calculate_Level_Tables2, which reads the SAME
     * SampleBit that Calculate_Level_Tables itself just used for `r`,
     * not a separately-tracked "requested" value. */
    int max_l = (e->sample_bits == 8) ? 127 : 32767;
    gtk_widget_set_visible(w->label_ay_ovfl, max_level > max_l);
    gtk_widget_set_visible(w->label_ts_ovfl, max_level * 2 > max_l);
    int dma_adjusted = max_level + e->atari_dma_level;
    if (e->number_of_channels == 1) dma_adjusted += e->atari_dma_level;
    gtk_widget_set_visible(
        w->label_dma_ovfl, e->atari_dma_level != 0 && dma_adjusted > max_l);
  }

  /* Mixer.pas: GBChFrq/EChFrqCur, GBIntFrq/EIntFrqCur - read-only, so no
   * `syncing`-guarded change handler is needed the way the interactive
   * controls above have, but refreshed on the same timer since both
   * values can change out from under this window (a fresh file load,
   * or an ItemEdit override taking effect) exactly like everything
   * else here. */
  char freq_buf[32];
  snprintf(freq_buf, sizeof(freq_buf), "%d",
           player_get_ay_freq(&w->playback->pair.primary));
  gtk_label_set_text(GTK_LABEL(w->label_ay_freq_cur), freq_buf);
  snprintf(freq_buf, sizeof(freq_buf), "%d",
           player_get_int_freq(&w->playback->pair.primary));
  gtk_label_set_text(GTK_LABEL(w->label_int_freq_cur), freq_buf);

  set_z80_frqs(w);
  set_mc_frqs(w);
  set_mfp_frqs(w);
  set_chip_frqs(w);
  set_player_frqs(w);
  set_n_tact_entry(w);

  w->syncing = false;

  return TRUE; /* keep firing */
}

/* Mixer.pas: TBAmpAL etc are real HORIZONTAL TTrackBars (Mixer.lfm:
 * Width=146 Height=21, no Orientation override - trHorizontal is
 * TTrackBar's own default) - a real, visible mistake in this file's
 * pre-MIG-0131 history (GtkVScale, "TTrackBar-like: max at the top"),
 * corrected here: one horizontal row per channel ([label][slider,
 * expands][entry]) instead of 9 side-by-side vertical columns, matching
 * both the real widget orientation and the real per-channel-row layout
 * (each TBAmpXX/EAmpXX pair sits on its own Top-anchored row in the
 * .lfm, not in vertical columns). */
/* MIG-0132: `scale`/`entry` are already fully CONSTRUCTED (correct
 * type/orientation/range/.lfm-Position, see mixer_win_gen.h's own
 * comment) by mixer_win_gen_create - this function only packs them into
 * this file's own idiomatic row layout and wires signals/entry text,
 * exactly the "layout and signal-wiring stay 100% hand-written" split
 * documented at this file's own #include "mixer_win_gen.h" above.
 * `override_initial` overrides the generated widget's own .lfm-Position
 * value (255 for TBAmpBpr/TBPreAmp/TBAmpDMA, a Lazarus form-editor
 * leftover - see this session's own migration_debt.yaml note on .lfm
 * design-time values vs real settings.pas runtime defaults) with the
 * real runtime default; pass -1 to keep the generated value as-is (the
 * 6 AL/AR/BL/BR/CL/CR sliders' own .lfm Position values already exactly
 * match their real runtime defaults, confirmed by direct comparison). */
static void wire_channel_row(gui_mixer_win* w, GtkWidget* box,
                              const char* label_text, GtkWidget* scale,
                              GtkWidget* entry, int override_initial) {
  GtkWidget* row = gtk_hbox_new(FALSE, 6);
  GtkWidget* label = gtk_label_new(label_text);
  gtk_widget_set_size_request(label, 50, -1);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);

  gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
  if (override_initial >= 0) {
    gtk_range_set_value(GTK_RANGE(scale), override_initial);
  }
  g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_changed), w);
  gtk_box_pack_start(GTK_BOX(row), scale, TRUE, TRUE, 0);

  /* Mixer.pas: EAmpAL etc - typed exact-value entry (MIG-0131, see
   * mixer_win.h's own comment). */
  gtk_entry_set_width_chars(GTK_ENTRY(entry), 3);
  gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", (int)gtk_range_get_value(GTK_RANGE(scale)));
  gtk_entry_set_text(GTK_ENTRY(entry), buf);
  g_object_set_data(G_OBJECT(entry), "scale", scale);
  g_signal_connect(entry, "activate", G_CALLBACK(on_amp_entry_activate), w);
  gtk_box_pack_start(GTK_BOX(row), entry, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
}

/* Mixer.pas: OpenMixer's own `EVolCtrl.Text := s` (mixerctl_title) -
 * refreshes the read-only "currently open control" label from mw-
 * >sysvol's real state (or "(none)" if no control is open - see gui/
 * include/gui/alsa_mixer.h's own file comment for when that happens on
 * this platform). MIG-0129: moved here from gui/src/tools_win.c, see
 * this file's own gui_mixer_win.h header comment for why. */
static void refresh_volctrl_label(gui_mixer_win* w) {
  char text[128];
  snprintf(text, sizeof(text), "Current control: %s",
           w->mw->sysvol ? gui_alsa_mixer_selem_name(w->mw->sysvol)
                         : "(none - no ALSA mixer control available)");
  gtk_label_set_text(GTK_LABEL(w->label_volctrl), text);
}

/* Mixer.pas: BVolCtrlSelectClick, adapted per this session's own
 * platform-substitution note (gui/include/gui/alsa_mixer.h) - a flat
 * GtkComboBoxText of every real ALSA control with a playback-volume
 * range replaces the original's 3-level device/subdevice/control
 * picker dialog (ALSA has no such 3-level concept, see that header's
 * own comment). Selecting an entry reopens mw->sysvol on it
 * immediately (gui_mainwin_sysvol_reopen), same live-effect timing as
 * the original's own OpenMixer call from inside BVolCtrlSelectClick. */
static void on_volctrl_changed(GtkWidget* widget, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  char* name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
  if (name) {
    gui_mainwin_sysvol_reopen(w->mw, name);
    g_free(name);
    refresh_volctrl_label(w);
  }
}

/* Mixer.pas: BVolCtrlDetectClick - `if not OpenMixer('', '', '') then
 * ShowMessage(Mes_SystemVolCtrlsNotDetected);` - gui_mainwin_sysvol_
 * reopen's own NULL/"" auto-detect path is exactly OpenMixer('','','')'s
 * real equivalent (see gui_alsa_mixer_open's own comment for the
 * Master/PCM/first-found preference order this uses). */
static void on_volctrl_detect_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_mixer_win* w = (gui_mixer_win*)data;
  gui_mainwin_sysvol_reopen(w->mw, NULL);
  refresh_volctrl_label(w);
  if (!w->mw->sysvol) {
    GtkWidget* msg = gtk_message_dialog_new(
        GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK,
        "No system volume control could be detected (Mes_SystemVolCtrlsNot"
        "Detected).");
    gtk_dialog_run(GTK_DIALOG(msg));
    gtk_widget_destroy(msg);
  }
}

/* Mixer.pas: CBLnScaleClick - `VolLinear := CBLnScale.Checked;
 * GetSysVolume;` - see gui_mainwin_set_sysvol_linear's own comment. */
static void on_ln_scale_toggled(GtkWidget* widget, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  bool checked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  gui_mainwin_set_sysvol_linear(w->mw, checked);
}

/* Mixer.pas: RBSR48kClick/.../RBSROtherClick -> Set_Sample_Rate
 * (MainWin.pas:1732-1744, MIG-0130) - a LOAD-TIME-ONLY output sample
 * rate change (real Pascal: `if IsPlaying then exit`); this port has
 * no persistent-across-loads player to apply it to directly, so it
 * just writes mw->sample_rate (read by gui/src/mainwin.c's do_load_song
 * at the next load, same convention as every other Digital-Sound-tab
 * setting here). */
static void apply_srate(gui_mixer_win* w) {
  int freq;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_sr48k))) {
    freq = 48000;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_sr44k))) {
    freq = 44100;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_sr22k))) {
    freq = 22050;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_sr11k))) {
    freq = 11025;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_sr96k))) {
    freq = 96000;
  } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_sr192k))) {
    freq = 192000;
  } else {
    freq = atoi(gtk_entry_get_text(GTK_ENTRY(w->entry_sr_other)));
  }
  /* MainWin.pas:1734 `if not ((SR >= 8000) and (SR < 300000)) then exit;` */
  if (freq >= 8000 && freq < 300000) w->mw->sample_rate = freq;
}

static void set_srate_radios(gui_mixer_win* w) {
  int freq = w->mw->sample_rate;
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", freq);
  w->syncing = true;
  switch (freq) {
    case 48000: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_sr48k), TRUE); break;
    case 44100: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_sr44k), TRUE); break;
    case 22050: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_sr22k), TRUE); break;
    case 11025: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_sr11k), TRUE); break;
    case 96000: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_sr96k), TRUE); break;
    case 192000: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_sr192k), TRUE); break;
    default:
      gtk_entry_set_text(GTK_ENTRY(w->entry_sr_other), buf);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_sr_other), TRUE);
      break;
  }
  w->syncing = false;
}

static void on_srate_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  if (!gtk_toggle_button_get_active(toggle)) return;
  apply_srate(w);
}
static void on_srate_entry_activate(GtkEntry* entry, gpointer data) {
  (void)entry;
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_sr_other), TRUE);
  apply_srate(w);
}

/* Mixer.pas: SBSRAYby8Click (1111-1115, MIG-0130) - `Set_Sample_Rate(
 * round(FrqAYTemp / 8))`. Uses the loaded file's CURRENT AY clock if
 * one is loaded (player_get_ay_freq), else PLAYER_AY_FREQ_DEF (1773400,
 * matching FrqAYTemp's own startup default before any file is ever
 * loaded, settings.pas:28's AY_FreqDef). */
static void on_srate_ay8_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_mixer_win* w = (gui_mixer_win*)data;
  int ay_freq = w->playback->loaded
                    ? player_get_ay_freq(&w->playback->pair.primary)
                    : PLAYER_AY_FREQ_DEF;
  w->mw->sample_rate = (ay_freq + 4) / 8; /* round(x/8) */
  set_srate_radios(w);
}

/* Mixer.pas: RBChStereoClick/RBChMonoClick -> Set_Stereo (MIG-0130) -
 * see gui/include/gui/mainwin.h's own default_channels field comment
 * for why this is the REAL final fallback resolve_number_of_channels
 * uses, not a hardcoded 2. */
static void on_ds_stereo_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing || !gtk_toggle_button_get_active(toggle)) return;
  w->mw->default_channels = 2;
}
static void on_ds_mono_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing || !gtk_toggle_button_get_active(toggle)) return;
  w->mw->default_channels = 1;
}

/* Mixer.pas: RBBt16Click/RBBt8Click -> Set_Sample_Bit (MIG-0130) - see
 * player_set_sample_bits's own doc comment for the real 8-bit-WIDTH
 * PCM this now produces (not just a smaller amplitude scale). */
static void on_ds_bit16_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing || !gtk_toggle_button_get_active(toggle)) return;
  w->mw->sample_bits = 16;
}
static void on_ds_bit8_toggled(GtkToggleButton* toggle, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing || !gtk_toggle_button_get_active(toggle)) return;
  w->mw->sample_bits = 8;
}

/* Mixer.pas: UpdateBuffLables (1127-1134, MIG-0130) - `LNumBuf.Caption
 * := IntToStr(NumberOfBuffers); LBufLen.Caption := IntToStr(BufLen_ms)
 * + ' ms'; LTotLen.Caption := IntToStr(BufLen_ms * NumberOfBuffers) +
 * ' ms';`. */
static void update_buff_labels(gui_mixer_win* w) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", w->mw->num_buffers);
  gtk_label_set_text(GTK_LABEL(w->label_num_buf), buf);
  snprintf(buf, sizeof(buf), "%d ms", w->mw->buf_len_ms);
  gtk_label_set_text(GTK_LABEL(w->label_buf_len), buf);
  snprintf(buf, sizeof(buf), "%d ms", w->mw->buf_len_ms * w->mw->num_buffers);
  gtk_label_set_text(GTK_LABEL(w->label_tot_len), buf);
}

/* Mixer.pas: TBBufLenChange/TBNumBufChange -> TFrmMain.SetBuffers
 * (MainWin.pas:4806-4817, MIG-0130) - `if (num<2)or(num>10) then exit;
 * if (len<5)or(len>2000) then exit;` (Mixer.lfm's own TBBufLen/TBNumBuf
 * Min/Max already enforce this at the widget level, see their own
 * creation below, so no extra clamping is needed here). */
static void on_buf_len_changed(GtkRange* range, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  w->mw->buf_len_ms = (int)gtk_range_get_value(range);
  update_buff_labels(w);
}
static void on_num_buf_changed(GtkRange* range, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  w->mw->num_buffers = (int)gtk_range_get_value(range);
  update_buff_labels(w);
}

/* Mixer.pas: cbWODeviceChange (1276-1279, MIG-0130) - `digsoundDevice :=
 * cbWODevice.ItemIndex;` - this port stores the device NAME directly
 * instead of an index (see mainwin.h's own output_device field
 * comment). combo_device's first entry is always a synthetic "(system
 * default)" mapping to "" (ALSA's own "default" - alsa_output_open's
 * own NULL/"" convention), since alsa_output_enumerate_devices' own
 * real ALSA hint list may not always literally include a "default"
 * entry (digsound.pas's own prepare_device_list DOES always guarantee
 * one, via its own `add_device('default')` fallback path - this
 * synthetic first entry is the equivalent guarantee here without
 * depending on the live hint list happening to contain it verbatim). */
static void on_device_changed(GtkWidget* widget, gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return;
  char* name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
  if (!name) return;
  if (strcmp(name, "(system default)") == 0) {
    w->mw->output_device[0] = '\0';
  } else {
    strncpy(w->mw->output_device, name, sizeof(w->mw->output_device) - 1);
    w->mw->output_device[sizeof(w->mw->output_device) - 1] = '\0';
  }
  g_free(name);
}

/* Mixer.pas: SBStopClick -> StopAndFreeAll (MIG-0130) - see gui/include/
 * gui/mainwin.h's own gui_mainwin_stop comment. */
static void on_ds_stop_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_mixer_win* w = (gui_mixer_win*)data;
  gui_mainwin_stop(w->mw);
}

static void on_helper_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_mixer_win* w = (gui_mixer_win*)data;
  gui_mxhelper_show(GTK_WINDOW(w->window), w->playback);
}

void gui_mixer_win_create(gui_mixer_win* w, GtkWindow* parent,
                           gui_mainwin* mw) {
  memset(w, 0, sizeof(*w));
  w->mw = mw;
  w->playback = &mw->playback;

  /* MIG-0132: constructs every widget build/generated/mixer_win_gen.h
   * declares (currently used for GBChAmp's 9 sliders only - see this
   * file's own #include comment). `gen` is a local, not stored on `w` -
   * every field this function actually needs gets aliased into `w`'s
   * own existing fields below, exactly as if it had been hand-built
   * in place; the rest of `gen`'s ~190 other fields (every other tab's
   * widgets, not yet migrated) are simply unused here. */
  mixer_win_gen gen;
  mixer_win_gen_create(&gen);

  w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(w->window), "Mixer");
  if (parent) gtk_window_set_transient_for(GTK_WINDOW(w->window), parent);
  g_signal_connect(w->window, "delete-event",
                    G_CALLBACK(gtk_widget_hide_on_delete), NULL);

  /* MIG-0129: real Mixer.pas is a TPageControl - see this file's own
   * gui_mixer_win.h header comment for the full tab-structure trace.
   * "AY Emulation" (AYEmuSheet) below, "Volume" (VolumeSheet) further
   * down. */
  GtkWidget* notebook = gtk_notebook_new();
  gtk_container_add(GTK_CONTAINER(w->window), notebook);

  GtkWidget* vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox,
                            gtk_label_new("AY Emulation"));

  /* AYEmuSheet's own real groupboxes sit in a 2-column grid - approximated
   * here as two GtkVBox columns side by side (not a pixel-exact replica
   * of Mixer.lfm's own coordinates, same "idiomatic GTK2, faithful in
   * spirit" convention as the rest of this file). GBChAmp (the 8-slider
   * amplitude row) stays full-width above the columns - too wide to
   * usefully halve. */
  GtkWidget* columns_hbox = gtk_hbox_new(TRUE, 8);
  GtkWidget* col_left = gtk_vbox_new(FALSE, 6);
  GtkWidget* col_right = gtk_vbox_new(FALSE, 6);
  gtk_box_pack_start(GTK_BOX(columns_hbox), col_left, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(columns_hbox), col_right, TRUE, TRUE, 0);

  /* Mixer.pas: GBChAmp ("Channel amplitude") - AL/AR/BL/BR/CL/CR, in
   * that order (MainWin.pas:1847-1852's own SetChan2 call order). */
  GtkWidget* amp_frame = gtk_frame_new("Channel amplitude");
  GtkWidget* amp_outer_vbox = gtk_vbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(amp_frame), amp_outer_vbox);
  /* Real GBChAmp lays AL/AR/BL/BR/CL/CR out as a 2-column (Left/Right)
   * x 3-row (A/B/C) grid, with Beeper/DMA/Preamp as 3 further full-
   * width rows below - this uses a single stacked-rows column instead
   * (same "faithful in spirit, not pixel-exact" simplification already
   * established throughout this file, e.g. GBChFrq/GBIntFrq's own
   * single-frame merge), but each row is now a real HORIZONTAL slider
   * (see wire_channel_row's own comment), matching the actual
   * TTrackBar orientation this file got wrong before MIG-0131. */
  GtkWidget* amp_rows_vbox = gtk_vbox_new(FALSE, 2);
  gtk_container_set_border_width(GTK_CONTAINER(amp_rows_vbox), 4);
  gtk_box_pack_start(GTK_BOX(amp_outer_vbox), amp_rows_vbox, TRUE, TRUE, 0);
  /* AL/AR/BL/BR/CL/CR: no override_initial needed - each one's real
   * Mixer.lfm Position value already exactly matches its real runtime
   * default (255/13/170/170/13/255), confirmed by direct comparison
   * against tools/lfm_analyze/lfm_analyze.py's own ground-truth dump. */
  w->scale_al = gen.TBAmpAL;
  w->entry_al = gen.EAmpAL;
  wire_channel_row(w, amp_rows_vbox, "A-L", gen.TBAmpAL, gen.EAmpAL, -1);
  w->scale_ar = gen.TBAmpAR;
  w->entry_ar = gen.EAmpAR;
  wire_channel_row(w, amp_rows_vbox, "A-R", gen.TBAmpAR, gen.EAmpAR, -1);
  w->scale_bl = gen.TBAmpBL;
  w->entry_bl = gen.EAmpBL;
  wire_channel_row(w, amp_rows_vbox, "B-L", gen.TBAmpBL, gen.EAmpBL, -1);
  w->scale_br = gen.TBAmpBR;
  w->entry_br = gen.EAmpBR;
  wire_channel_row(w, amp_rows_vbox, "B-R", gen.TBAmpBR, gen.EAmpBR, -1);
  w->scale_cl = gen.TBAmpCL;
  w->entry_cl = gen.EAmpCL;
  wire_channel_row(w, amp_rows_vbox, "C-L", gen.TBAmpCL, gen.EAmpCL, -1);
  w->scale_cr = gen.TBAmpCR;
  w->entry_cr = gen.EAmpCR;
  wire_channel_row(w, amp_rows_vbox, "C-R", gen.TBAmpCR, gen.EAmpCR, -1);
  /* Mixer.pas: TBAmpBpr/LAmpBpr ("Beeper") - the 7th slider in the SAME
   * GBChAmp groupbox as AL..CR above (MIG-0106). Mixer.lfm's own
   * Position for this one is 255 (a Lazarus form-editor leftover, see
   * wire_channel_row's own comment) - overridden to the real runtime
   * default, BeeperMaxDef (146). */
  w->scale_bpr = gen.TBAmpBpr;
  w->entry_bpr = gen.EAmpBpr;
  wire_channel_row(w, amp_rows_vbox, "Beeper", gen.TBAmpBpr, gen.EAmpBpr, 146);
  /* Mixer.pas: TBAmpDMA/LAmpDMA ("DMA") - the 8th slider, right after
   * Beeper (MIG-0123; MainWin.pas:3811-3814's own AddBitmaps order).
   * 146 initial value matches Atari_DMAMaxDef (settings.pas:31) - the
   * real interactive app's own startup default (MainWin.pas:907),
   * which gui/src/mainwin.c's do_load_song re-applies on every fresh
   * load (see its own comment for why the shared engine/ library
   * default itself stays 0 - the oracle-diff test harness's own
   * deliberate zeroing convention, unrelated to this GUI-only value).
   * Same .lfm-Position-vs-runtime-default override as Beeper above. */
  w->scale_dma = gen.TBAmpDMA;
  w->entry_dma = gen.EAmpDMA;
  wire_channel_row(w, amp_rows_vbox, "DMA", gen.TBAmpDMA, gen.EAmpDMA, 146);
  /* Mixer.pas: TBPreAmp/EPreAmp/LPreAmp ("Preamp", MIG-0131) - the 9th
   * slider, same GBChAmp groupbox. 127 matches PreAmpDef (AY.pas:39) =
   * ay_engine_init's own e->pre_amp default (engine/src/hw/ay.c:477).
   * Same .lfm-Position-vs-runtime-default override as Beeper/DMA above. */
  w->scale_preamp = gen.TBPreAmp;
  w->entry_preamp = gen.EPreAmp;
  wire_channel_row(w, amp_rows_vbox, "Preamp", gen.TBPreAmp, gen.EPreAmp, 127);

  /* Mixer.pas: LAYOvfl/LTSOvfl/LDMAOvfl (MIG-0131) - clip-warning
   * labels, hidden by default (Mixer.lfm's own Visible property isn't
   * set on any of the three, defaulting to False); on_sync_timer below
   * shows them per Calculate_Level_Tables2's own conditions. */
  GtkWidget* ovfl_hbox = gtk_hbox_new(FALSE, 8);
  w->label_ay_ovfl = gtk_label_new("AY!");
  gtk_widget_set_tooltip_text(
      w->label_ay_ovfl,
      "Warning: AY-3-8910/YM2149F output levels can overflow");
  w->label_ts_ovfl = gtk_label_new("TS!");
  gtk_widget_set_tooltip_text(w->label_ts_ovfl,
                               "Warning: Turbo Sound output levels can "
                               "overflow");
  w->label_dma_ovfl = gtk_label_new("DMA");
  gtk_widget_set_tooltip_text(
      w->label_dma_ovfl,
      "Warning: DMA-Sound+AY-3-8910/YM2149F output levels can overflow");
  gtk_box_pack_start(GTK_BOX(ovfl_hbox), w->label_ay_ovfl, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(ovfl_hbox), w->label_ts_ovfl, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(ovfl_hbox), w->label_dma_ovfl, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(amp_outer_vbox), ovfl_hbox, FALSE, FALSE, 0);
  /* Default-hidden (Mixer.lfm's own default Visible=False) - `no_show_
   * all` so gui_mixer_win_toggle_visible's gtk_widget_show_all doesn't
   * force these back on every time the window is (re)opened; on_sync_
   * timer's gtk_widget_set_visible calls below still work normally
   * (show/hide bypass no_show_all, only recursive show_all respects it). */
  gtk_widget_set_no_show_all(w->label_ay_ovfl, TRUE);
  gtk_widget_set_no_show_all(w->label_ts_ovfl, TRUE);
  gtk_widget_set_no_show_all(w->label_dma_ovfl, TRUE);
  gtk_widget_hide(w->label_ay_ovfl);
  gtk_widget_hide(w->label_ts_ovfl);
  gtk_widget_hide(w->label_dma_ovfl);

  gtk_box_pack_start(GTK_BOX(vbox), amp_frame, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), columns_hbox, TRUE, TRUE, 0);

  /* Mixer.pas: GBChType ("Chip type") - RBChTypeAY/RBChTypeYM. */
  GtkWidget* type_frame = gtk_frame_new("Chip type");
  GtkWidget* type_hbox = gtk_hbox_new(TRUE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(type_hbox), 4);
  gtk_container_add(GTK_CONTAINER(type_frame), type_hbox);
  w->rb_ay = gtk_radio_button_new_with_label(NULL, "AY");
  w->rb_ym = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(w->rb_ay), "YM");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_ym), TRUE);
  g_signal_connect(w->rb_ay, "toggled", G_CALLBACK(on_chip_type_toggled), w);
  g_signal_connect(w->rb_ym, "toggled", G_CALLBACK(on_chip_type_toggled), w);
  gtk_box_pack_start(GTK_BOX(type_hbox), w->rb_ay, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(type_hbox), w->rb_ym, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(col_left), type_frame, FALSE, FALSE, 0);

  /* Mixer.pas: GBChFrq/GBIntFrq's own EChFrqCur/EIntFrqCur - read-only
   * "current value" displays (a SEPARATE mechanism from ItemEdit.pas's
   * own per-item overrides, MIG-0088, which this doesn't replace).
   * Grouped into one frame here rather than two separate GBChFrq/
   * GBIntFrq GroupBoxes, since (unlike when this frame was first added)
   * both GroupBoxes' own real override sub-panels ARE now ported too -
   * see the two frames right after this one - just laid out as their
   * own separate frames rather than merged into GBChFrq/GBIntFrq's
   * real grouping, for consistency with GBZ80Frq/GBMCFrq/GBMFPFrq's
   * own already-established one-frame-per-GroupBox layout below. */
  GtkWidget* freq_frame = gtk_frame_new("Frequencies (current)");
  GtkWidget* freq_table = gtk_table_new(2, 2, FALSE);
  gtk_container_set_border_width(GTK_CONTAINER(freq_table), 4);
  gtk_container_add(GTK_CONTAINER(freq_frame), freq_table);
  GtkWidget* ay_freq_lbl = gtk_label_new("Sound chip frequency (Hz):");
  gtk_misc_set_alignment(GTK_MISC(ay_freq_lbl), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(freq_table), ay_freq_lbl, 0, 1, 0, 1, GTK_FILL,
                    GTK_FILL, 4, 2);
  w->label_ay_freq_cur = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w->label_ay_freq_cur), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(freq_table), w->label_ay_freq_cur, 1, 2, 0, 1,
                    GTK_EXPAND | GTK_FILL, GTK_FILL, 4, 2);
  GtkWidget* int_freq_lbl = gtk_label_new("Interrupt frequency (Hz x1000):");
  gtk_misc_set_alignment(GTK_MISC(int_freq_lbl), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(freq_table), int_freq_lbl, 0, 1, 1, 2, GTK_FILL,
                    GTK_FILL, 4, 2);
  w->label_int_freq_cur = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w->label_int_freq_cur), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(freq_table), w->label_int_freq_cur, 1, 2, 1, 2,
                    GTK_EXPAND | GTK_FILL, GTK_FILL, 4, 2);
  gtk_box_pack_start(GTK_BOX(col_left), freq_frame, FALSE, FALSE, 0);

  /* Mixer.pas: GBChFrq ("Sound chip frequency", MIG-0124) - RBChFrqZX/
   * RBChFrqPnt/RBChFrqST/RBChFrqCPC/RBChFrqOther/EChFrqOther. A LIVE
   * override (player_set_chip_freq's own doc comment) - unlike GBZ80Frq/
   * GBMCFrq/GBMFPFrq below, applies immediately, every format. */
  {
    GtkWidget* frame = gtk_frame_new("Sound chip frequency override (Hz)");
    GtkWidget* radio_hbox = gtk_hbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(radio_hbox), 4);
    gtk_container_add(GTK_CONTAINER(frame), radio_hbox);

    w->rb_chfrq_zx = gtk_radio_button_new_with_label(NULL, "ZX (1773400)");
    w->rb_chfrq_pnt = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_chfrq_zx), "Pentagon (1750000)");
    w->rb_chfrq_st = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_chfrq_zx), "ST (2000000)");
    w->rb_chfrq_cpc = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_chfrq_zx), "CPC (1000000)");
    w->rb_chfrq_other = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_chfrq_zx), "Other:");
    w->entry_chfrq_other = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry_chfrq_other), 8);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_chfrq_zx), TRUE);
    g_signal_connect(w->rb_chfrq_zx, "toggled", G_CALLBACK(on_chfrq_toggled),
                      w);
    g_signal_connect(w->rb_chfrq_pnt, "toggled", G_CALLBACK(on_chfrq_toggled),
                      w);
    g_signal_connect(w->rb_chfrq_st, "toggled", G_CALLBACK(on_chfrq_toggled),
                      w);
    g_signal_connect(w->rb_chfrq_cpc, "toggled", G_CALLBACK(on_chfrq_toggled),
                      w);
    g_signal_connect(w->rb_chfrq_other, "toggled",
                      G_CALLBACK(on_chfrq_toggled), w);
    g_signal_connect(w->entry_chfrq_other, "activate",
                      G_CALLBACK(on_chfrq_entry_activate), w);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_chfrq_zx, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_chfrq_pnt, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_chfrq_st, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_chfrq_cpc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_chfrq_other, FALSE, FALSE,
                        0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->entry_chfrq_other, FALSE,
                        FALSE, 0);

    gtk_box_pack_start(GTK_BOX(col_left), frame, FALSE, FALSE, 0);
  }

  /* Mixer.pas: GBOUTZXAY ("OUT, ZXAY, AY, AYM", MIG-0131) - see this
   * file's own mixer_win.h struct comment for entry_n_tact/entry_int_
   * offset's own scope notes. */
  {
    GtkWidget* frame = gtk_frame_new("OUT, ZXAY, AY, AYM");
    GtkWidget* grid = gtk_table_new(2, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
    gtk_container_add(GTK_CONTAINER(frame), grid);

    GtkWidget* tact_lbl = gtk_label_new("TStates per frame:");
    gtk_misc_set_alignment(GTK_MISC(tact_lbl), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(grid), tact_lbl, 0, 1, 0, 1, GTK_FILL,
                      GTK_FILL, 4, 2);
    w->entry_n_tact = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry_n_tact), 8);
    g_signal_connect(w->entry_n_tact, "activate",
                      G_CALLBACK(on_n_tact_activate), w);
    gtk_table_attach(GTK_TABLE(grid), w->entry_n_tact, 1, 2, 0, 1, GTK_FILL,
                      GTK_FILL, 4, 2);

    GtkWidget* offs_lbl = gtk_label_new("Interrupt offset:");
    gtk_misc_set_alignment(GTK_MISC(offs_lbl), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(grid), offs_lbl, 0, 1, 1, 2, GTK_FILL,
                      GTK_FILL, 4, 2);
    w->entry_int_offset = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry_int_offset), 8);
    gtk_entry_set_text(GTK_ENTRY(w->entry_int_offset), "0"); /* IntOffsetDef */
    g_signal_connect(w->entry_int_offset, "activate",
                      G_CALLBACK(on_int_offset_activate), w);
    gtk_table_attach(GTK_TABLE(grid), w->entry_int_offset, 1, 2, 1, 2,
                      GTK_FILL, GTK_FILL, 4, 2);

    gtk_box_pack_start(GTK_BOX(col_left), frame, FALSE, FALSE, 0);
  }

  /* Mixer.pas: GBIntFrq ("Interrupt frequency", MIG-0124) - RBEIntFrqZX/
   * RBIntFrqPnt/RBIntFrqOther/EIntFrqOther. A LIVE override
   * (player_set_player_freq's own doc comment) - YM/VTX only. */
  {
    GtkWidget* frame =
        gtk_frame_new("Interrupt frequency override (Hz x1000)");
    GtkWidget* radio_hbox = gtk_hbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(radio_hbox), 4);
    gtk_container_add(GTK_CONTAINER(frame), radio_hbox);

    w->rb_intfrq_zx = gtk_radio_button_new_with_label(NULL, "ZX (50000)");
    w->rb_intfrq_pnt = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_intfrq_zx), "Pentagon (48828)");
    w->rb_intfrq_other = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_intfrq_zx), "Other:");
    w->entry_intfrq_other = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry_intfrq_other), 8);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_intfrq_zx), TRUE);
    g_signal_connect(w->rb_intfrq_zx, "toggled",
                      G_CALLBACK(on_intfrq_toggled), w);
    g_signal_connect(w->rb_intfrq_pnt, "toggled",
                      G_CALLBACK(on_intfrq_toggled), w);
    g_signal_connect(w->rb_intfrq_other, "toggled",
                      G_CALLBACK(on_intfrq_toggled), w);
    g_signal_connect(w->entry_intfrq_other, "activate",
                      G_CALLBACK(on_intfrq_entry_activate), w);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_intfrq_zx, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_intfrq_pnt, FALSE, FALSE,
                        0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_intfrq_other, FALSE, FALSE,
                        0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->entry_intfrq_other, FALSE,
                        FALSE, 0);

    gtk_box_pack_start(GTK_BOX(col_left), frame, FALSE, FALSE, 0);
  }

  /* Mixer.pas: GBZ80Frq ("Z80 clock", MIG-0120) - RBZ80FrqZX/RBZ80FrqPnt/
   * RBZ80FrqOther/EZ80FrqOther/EZ80FrqCur. A load-time-only override
   * (player_set_frq_z80's own doc comment), AY-only - applying it while
   * a non-.ay file is loaded is a silent no-op, same as every other
   * control in this window when playback->loaded is false. */
  {
    GtkWidget* frame = gtk_frame_new("Z80 clock (Hz) - .ay files only");
    GtkWidget* outer = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 4);
    gtk_container_add(GTK_CONTAINER(frame), outer);

    GtkWidget* cur_hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(cur_hbox), gtk_label_new("Current:"), FALSE,
                        FALSE, 0);
    w->label_z80_cur = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(cur_hbox), w->label_z80_cur, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), cur_hbox, FALSE, FALSE, 0);

    GtkWidget* radio_hbox = gtk_hbox_new(FALSE, 4);
    w->rb_z80_zx = gtk_radio_button_new_with_label(NULL, "ZX (3494400)");
    w->rb_z80_pnt = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_z80_zx), "Pentagon (3500000)");
    w->rb_z80_other = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_z80_zx), "Other:");
    w->entry_z80_other = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry_z80_other), 8);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_z80_zx), TRUE);
    g_signal_connect(w->rb_z80_zx, "toggled", G_CALLBACK(on_z80_toggled), w);
    g_signal_connect(w->rb_z80_pnt, "toggled", G_CALLBACK(on_z80_toggled), w);
    g_signal_connect(w->rb_z80_other, "toggled", G_CALLBACK(on_z80_toggled),
                      w);
    g_signal_connect(w->entry_z80_other, "activate",
                      G_CALLBACK(on_z80_entry_activate), w);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_z80_zx, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_z80_pnt, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_z80_other, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->entry_z80_other, FALSE, FALSE,
                        0);
    gtk_box_pack_start(GTK_BOX(outer), radio_hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(col_right), frame, FALSE, FALSE, 0);
  }

  /* Mixer.pas: GBMCFrq ("68000 clock", MIG-0120) - RBMCFrqST/
   * RBMCFrqOther/EMCFrqOther/EMCFrqCur. SNDH-only (player_set_mc68000_
   * freq's own doc comment). */
  {
    GtkWidget* frame = gtk_frame_new("68000 clock (Hz) - SNDH files only");
    GtkWidget* outer = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 4);
    gtk_container_add(GTK_CONTAINER(frame), outer);

    GtkWidget* cur_hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(cur_hbox), gtk_label_new("Current:"), FALSE,
                        FALSE, 0);
    w->label_mc_cur = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(cur_hbox), w->label_mc_cur, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), cur_hbox, FALSE, FALSE, 0);

    GtkWidget* radio_hbox = gtk_hbox_new(FALSE, 4);
    w->rb_mc_st = gtk_radio_button_new_with_label(NULL, "ST (8000000)");
    w->rb_mc_other = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_mc_st), "Other:");
    w->entry_mc_other = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry_mc_other), 8);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mc_st), TRUE);
    g_signal_connect(w->rb_mc_st, "toggled", G_CALLBACK(on_mc_toggled), w);
    g_signal_connect(w->rb_mc_other, "toggled", G_CALLBACK(on_mc_toggled), w);
    g_signal_connect(w->entry_mc_other, "activate",
                      G_CALLBACK(on_mc_entry_activate), w);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_mc_st, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_mc_other, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->entry_mc_other, FALSE, FALSE,
                        0);
    gtk_box_pack_start(GTK_BOX(outer), radio_hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(col_right), frame, FALSE, FALSE, 0);
  }

  /* Mixer.pas: GBMFPFrq ("MFP timer frequency", MIG-0120) -
   * RBMFPFrq1613 ("Auto")/RBMFPFrqST/RBMFPFrqOther/EMFPFrqOther/
   * EMFPFrqCur. SNDH-only (player_set_mfp_freq's own doc comment). */
  {
    GtkWidget* frame =
        gtk_frame_new("MFP timer frequency (Hz) - SNDH files only");
    GtkWidget* outer = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 4);
    gtk_container_add(GTK_CONTAINER(frame), outer);

    GtkWidget* cur_hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(cur_hbox), gtk_label_new("Current:"), FALSE,
                        FALSE, 0);
    w->label_mfp_cur = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(cur_hbox), w->label_mfp_cur, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), cur_hbox, FALSE, FALSE, 0);

    GtkWidget* radio_hbox = gtk_hbox_new(FALSE, 4);
    w->rb_mfp_auto = gtk_radio_button_new_with_label(NULL, "Auto");
    w->rb_mfp_st = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_mfp_auto), "ST (2457600)");
    w->rb_mfp_other = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_mfp_auto), "Other:");
    w->entry_mfp_other = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry_mfp_other), 8);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_mfp_auto), TRUE);
    g_signal_connect(w->rb_mfp_auto, "toggled", G_CALLBACK(on_mfp_toggled),
                      w);
    g_signal_connect(w->rb_mfp_st, "toggled", G_CALLBACK(on_mfp_toggled), w);
    g_signal_connect(w->rb_mfp_other, "toggled", G_CALLBACK(on_mfp_toggled),
                      w);
    g_signal_connect(w->entry_mfp_other, "activate",
                      G_CALLBACK(on_mfp_entry_activate), w);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_mfp_auto, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_mfp_st, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->rb_mfp_other, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(radio_hbox), w->entry_mfp_other, FALSE, FALSE,
                        0);
    gtk_box_pack_start(GTK_BOX(outer), radio_hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(col_right), frame, FALSE, FALSE, 0);
  }

  /* Mixer.pas: GBSNDH ("Atari ST emulation (SNDH)", MIG-0121) -
   * STRB/STeRB (is_ste, default STe = Mixer.lfm's own Checked=True on
   * STeRB) plus AtariYMMonoChk/AtariMonoChk (SNDH-specific mono-output
   * fallback defaults, both default unchecked per Mixer.lfm - neither
   * has a Checked attribute). Read by gui/src/mainwin.c's do_load_song,
   * same load-time-only timing as every override above. */
  {
    GtkWidget* frame = gtk_frame_new("Atari ST emulation (SNDH)");
    GtkWidget* hbox = gtk_hbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_container_add(GTK_CONTAINER(frame), hbox);

    w->rb_atari_st = gtk_radio_button_new_with_label(NULL, "ST");
    w->rb_atari_ste = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_atari_st), "STe");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_atari_ste), TRUE);
    w->cb_atari_ym_mono = gtk_check_button_new_with_label("YM mono");
    w->cb_atari_mono = gtk_check_button_new_with_label("Mono");
    gtk_box_pack_start(GTK_BOX(hbox), w->rb_atari_st, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), w->rb_atari_ste, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), w->cb_atari_ym_mono, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), w->cb_atari_mono, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(col_right), frame, FALSE, FALSE, 0);
  }

  /* Mixer.pas: GBResamp ("Resampler") - RBResamAvg/RBResamFIR
   * (MIG-0106), controlling settings.pas's FilterQuality via
   * ay_engine_set_filter (see apply_filter_quality's own comment). */
  GtkWidget* filt_frame = gtk_frame_new("Resampler");
  GtkWidget* filt_hbox = gtk_hbox_new(TRUE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(filt_hbox), 4);
  gtk_container_add(GTK_CONTAINER(filt_frame), filt_hbox);
  w->rb_filt_avg = gtk_radio_button_new_with_label(NULL, "averager");
  w->rb_filt_fir = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(w->rb_filt_avg), "FIR-filter");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_filt_fir), TRUE);
  g_signal_connect(w->rb_filt_avg, "toggled", G_CALLBACK(on_filter_toggled),
                    w);
  g_signal_connect(w->rb_filt_fir, "toggled", G_CALLBACK(on_filter_toggled),
                    w);
  gtk_box_pack_start(GTK_BOX(filt_hbox), w->rb_filt_avg, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(filt_hbox), w->rb_filt_fir, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(col_right), filt_frame, FALSE, FALSE, 0);

  /* Mixer.pas: SBHelper - opens mxhelper.pas's preset picker. */
  GtkWidget* helper_btn = gtk_button_new_with_label("Presets...");
  g_signal_connect(helper_btn, "clicked", G_CALLBACK(on_helper_clicked), w);
  gtk_box_pack_start(GTK_BOX(vbox), helper_btn, FALSE, FALSE, 0);

  /* Mixer.pas: CheckBox1/CBChTypeLst/CBChFrqLst/CBIntFrqLst/CBChLst -
   * all five "Get from list" (default Checked=True, Mixer.pas:901-904),
   * gating ItemEdit.pas's per-item overrides (MIG-0088, CBChLst added by
   * MIG-0107). Grouped into their own frame here since this port doesn't
   * reproduce the surrounding GBChFrq/GBIntFrq current-value display
   * panels those two checkboxes live inside in the original (out of
   * scope per the approved "Mixer's AY-relevant tab only" answer) - only
   * the gate itself is needed, read directly by gui/src/mainwin.c's
   * do_load_song. */
  GtkWidget* list_frame = gtk_frame_new("Use playlist item overrides");
  GtkWidget* list_vbox = gtk_vbox_new(TRUE, 2);
  gtk_container_set_border_width(GTK_CONTAINER(list_vbox), 4);
  gtk_container_add(GTK_CONTAINER(list_frame), list_vbox);
  w->cb_use_chip_type_list = gtk_check_button_new_with_label("Chip type");
  w->cb_use_channel_mode_list =
      gtk_check_button_new_with_label("Channel amplitude");
  w->cb_use_ay_freq_list =
      gtk_check_button_new_with_label("Sound chip frequency");
  w->cb_use_int_freq_list =
      gtk_check_button_new_with_label("Interrupt frequency");
  w->cb_use_channel_count_list =
      gtk_check_button_new_with_label("Output channels (mono/stereo)");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cb_use_chip_type_list),
                                TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cb_use_channel_mode_list),
                                TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cb_use_ay_freq_list),
                                TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cb_use_int_freq_list),
                                TRUE);
  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(w->cb_use_channel_count_list), TRUE);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_chip_type_list, TRUE, TRUE,
                      0);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_channel_mode_list, TRUE,
                      TRUE, 0);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_ay_freq_list, TRUE, TRUE,
                      0);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_int_freq_list, TRUE, TRUE,
                      0);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_channel_count_list, TRUE,
                      TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), list_frame, FALSE, FALSE, 0);

  /* Mixer.pas: WOSheet ("Digital Sound" tab, MIG-0130) - see this
   * file's own gui_mixer_win.h header comment. Real tab order:
   * AY Emulation, Digital Sound, MIDIOut(unported), Global Volume
   * Control, BASS(unported) - Digital Sound is appended here, right
   * after AY Emulation, matching that same order. */
  GtkWidget* ds_page_vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(ds_page_vbox), 6);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), ds_page_vbox,
                            gtk_label_new("Digital Sound"));

  GtkWidget* ds_columns_hbox = gtk_hbox_new(TRUE, 8);
  GtkWidget* ds_col_left = gtk_vbox_new(FALSE, 6);
  GtkWidget* ds_col_right = gtk_vbox_new(FALSE, 6);
  gtk_box_pack_start(GTK_BOX(ds_columns_hbox), ds_col_left, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(ds_columns_hbox), ds_col_right, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(ds_page_vbox), ds_columns_hbox, TRUE, TRUE, 0);

  /* Mixer.pas: GBSRate ("Sample rate", MainWin.pas:1732-1744). */
  {
    w->ds_frame_srate = gtk_frame_new("Sample rate (Hz)");
    GtkWidget* outer = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 4);
    gtk_container_add(GTK_CONTAINER(w->ds_frame_srate), outer);

    GtkWidget* grid = gtk_table_new(4, 2, TRUE);
    w->rb_sr48k = gtk_radio_button_new_with_label(NULL, "48000");
    w->rb_sr44k = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_sr48k), "44100");
    w->rb_sr22k = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_sr48k), "22050");
    w->rb_sr11k = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_sr48k), "11025");
    w->rb_sr96k = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_sr48k), "96000");
    w->rb_sr192k = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_sr48k), "192000");
    w->rb_sr_other = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_sr48k), "Other:");
    w->entry_sr_other = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(w->entry_sr_other), 8);
    gtk_table_attach_defaults(GTK_TABLE(grid), w->rb_sr48k, 0, 1, 0, 1);
    gtk_table_attach_defaults(GTK_TABLE(grid), w->rb_sr44k, 1, 2, 0, 1);
    gtk_table_attach_defaults(GTK_TABLE(grid), w->rb_sr22k, 0, 1, 1, 2);
    gtk_table_attach_defaults(GTK_TABLE(grid), w->rb_sr11k, 1, 2, 1, 2);
    gtk_table_attach_defaults(GTK_TABLE(grid), w->rb_sr96k, 0, 1, 2, 3);
    gtk_table_attach_defaults(GTK_TABLE(grid), w->rb_sr192k, 1, 2, 2, 3);
    gtk_table_attach_defaults(GTK_TABLE(grid), w->rb_sr_other, 0, 1, 3, 4);
    gtk_table_attach_defaults(GTK_TABLE(grid), w->entry_sr_other, 1, 2, 3, 4);
    gtk_box_pack_start(GTK_BOX(outer), grid, FALSE, FALSE, 0);

    g_signal_connect(w->rb_sr48k, "toggled", G_CALLBACK(on_srate_toggled), w);
    g_signal_connect(w->rb_sr44k, "toggled", G_CALLBACK(on_srate_toggled), w);
    g_signal_connect(w->rb_sr22k, "toggled", G_CALLBACK(on_srate_toggled), w);
    g_signal_connect(w->rb_sr11k, "toggled", G_CALLBACK(on_srate_toggled), w);
    g_signal_connect(w->rb_sr96k, "toggled", G_CALLBACK(on_srate_toggled), w);
    g_signal_connect(w->rb_sr192k, "toggled", G_CALLBACK(on_srate_toggled), w);
    g_signal_connect(w->rb_sr_other, "toggled", G_CALLBACK(on_srate_toggled),
                      w);
    g_signal_connect(w->entry_sr_other, "activate",
                      G_CALLBACK(on_srate_entry_activate), w);

    GtkWidget* ay8_btn = gtk_button_new_with_label("AY / 8");
    g_signal_connect(ay8_btn, "clicked", G_CALLBACK(on_srate_ay8_clicked), w);
    gtk_box_pack_start(GTK_BOX(outer), ay8_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(ds_col_left), w->ds_frame_srate, FALSE, FALSE,
                        0);
  }

  /* Mixer.pas: GBChans ("Channels") - RBChStereo/RBChMono (CBChLst,
   * "Get from list", is already ported elsewhere - see this window's
   * own "Use playlist item overrides" frame above, cb_use_channel_
   * count_list. CheckBox6/CheckBox7 status lamps NOT ported, see this
   * file's own header comment). */
  {
    w->ds_frame_chans = gtk_frame_new("Channels");
    GtkWidget* hbox = gtk_hbox_new(TRUE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_container_add(GTK_CONTAINER(w->ds_frame_chans), hbox);
    w->rb_ds_stereo = gtk_radio_button_new_with_label(NULL, "Stereo");
    w->rb_ds_mono = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_ds_stereo), "Mono");
    g_signal_connect(w->rb_ds_stereo, "toggled",
                      G_CALLBACK(on_ds_stereo_toggled), w);
    g_signal_connect(w->rb_ds_mono, "toggled", G_CALLBACK(on_ds_mono_toggled),
                      w);
    gtk_box_pack_start(GTK_BOX(hbox), w->rb_ds_stereo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), w->rb_ds_mono, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ds_col_left), w->ds_frame_chans, FALSE, FALSE,
                        0);
  }

  /* Mixer.pas: GBBRate ("Bit rate") - RBBt16/RBBt8. */
  {
    w->ds_frame_brate = gtk_frame_new("Bit rate");
    GtkWidget* hbox = gtk_hbox_new(TRUE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_container_add(GTK_CONTAINER(w->ds_frame_brate), hbox);
    w->rb_ds_bit16 = gtk_radio_button_new_with_label(NULL, "16 bit");
    w->rb_ds_bit8 = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(w->rb_ds_bit16), "8 bit");
    g_signal_connect(w->rb_ds_bit16, "toggled",
                      G_CALLBACK(on_ds_bit16_toggled), w);
    g_signal_connect(w->rb_ds_bit8, "toggled", G_CALLBACK(on_ds_bit8_toggled),
                      w);
    gtk_box_pack_start(GTK_BOX(hbox), w->rb_ds_bit16, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), w->rb_ds_bit8, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ds_col_right), w->ds_frame_brate, FALSE, FALSE,
                        0);
  }

  /* Mixer.pas: GBBuffs ("Buffers") - TBBufLen(5-2000ms)/TBNumBuf(2-10). */
  {
    w->ds_frame_buffs = gtk_frame_new("Buffers");
    GtkWidget* outer = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 4);
    gtk_container_add(GTK_CONTAINER(w->ds_frame_buffs), outer);

    GtkWidget* len_hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(len_hbox), gtk_label_new("Buffer length:"),
                        FALSE, FALSE, 0);
    w->scale_buf_len = gtk_hscale_new_with_range(5.0, 2000.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(w->scale_buf_len), FALSE);
    g_signal_connect(w->scale_buf_len, "value-changed",
                      G_CALLBACK(on_buf_len_changed), w);
    gtk_box_pack_start(GTK_BOX(len_hbox), w->scale_buf_len, TRUE, TRUE, 0);
    w->label_buf_len = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(len_hbox), w->label_buf_len, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), len_hbox, FALSE, FALSE, 0);

    GtkWidget* num_hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(num_hbox), gtk_label_new("Number of buffers:"),
                        FALSE, FALSE, 0);
    w->scale_num_buf = gtk_hscale_new_with_range(2.0, 10.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(w->scale_num_buf), FALSE);
    g_signal_connect(w->scale_num_buf, "value-changed",
                      G_CALLBACK(on_num_buf_changed), w);
    gtk_box_pack_start(GTK_BOX(num_hbox), w->scale_num_buf, TRUE, TRUE, 0);
    w->label_num_buf = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(num_hbox), w->label_num_buf, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), num_hbox, FALSE, FALSE, 0);

    GtkWidget* tot_hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(tot_hbox), gtk_label_new("Total length:"),
                        FALSE, FALSE, 0);
    w->label_tot_len = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(tot_hbox), w->label_tot_len, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), tot_hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(ds_col_right), w->ds_frame_buffs, FALSE, FALSE,
                        0);
  }

  /* Mixer.pas: GBDevice ("Device") - cbWODevice, a real ALSA PCM device
   * picker on this platform (see this file's own header comment). */
  {
    w->ds_frame_device = gtk_frame_new("Device");
    GtkWidget* hbox = gtk_hbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_container_add(GTK_CONTAINER(w->ds_frame_device), hbox);
    w->combo_device = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->combo_device),
                                    "(system default)");
    int n_devs = 0;
    char** devs = alsa_output_enumerate_devices(&n_devs);
    int active_idx = 0;
    for (int i = 0; i < n_devs; i++) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->combo_device),
                                      devs[i]);
      if (mw->output_device[0] && strcmp(devs[i], mw->output_device) == 0)
        active_idx = i + 1;
    }
    alsa_output_free_device_names(devs, n_devs);
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->combo_device), active_idx);
    g_signal_connect(w->combo_device, "changed",
                      G_CALLBACK(on_device_changed), w);
    gtk_box_pack_start(GTK_BOX(hbox), w->combo_device, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ds_col_right), w->ds_frame_device, FALSE,
                        FALSE, 0);
  }

  w->button_ds_stop = gtk_button_new_with_label("Stop playing");
  g_signal_connect(w->button_ds_stop, "clicked",
                    G_CALLBACK(on_ds_stop_clicked), w);
  gtk_box_pack_start(GTK_BOX(ds_page_vbox), w->button_ds_stop, FALSE, FALSE,
                      0);

  /* Initial widget state from mw's own loaded/default settings. */
  w->syncing = true;
  set_srate_radios(w);
  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(mw->default_channels == 1 ? w->rb_ds_mono
                                                    : w->rb_ds_stereo),
      TRUE);
  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(mw->sample_bits == 8 ? w->rb_ds_bit8 : w->rb_ds_bit16),
      TRUE);
  gtk_range_set_value(GTK_RANGE(w->scale_buf_len), mw->buf_len_ms);
  gtk_range_set_value(GTK_RANGE(w->scale_num_buf), mw->num_buffers);
  w->syncing = false;
  update_buff_labels(w);

  /* Mixer.pas: VolumeSheet (MIG-0129, MOVED here from gui/src/
   * tools_win.c - see this file's own gui_mixer_win.h header comment). */
  GtkWidget* vol_page_vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vol_page_vbox), 6);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vol_page_vbox,
                            gtk_label_new("Volume"));

  GtkWidget* vol_frame = gtk_frame_new("System volume (ALSA mixer)");
  GtkWidget* vol_vbox = gtk_vbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(vol_vbox), 4);
  gtk_container_add(GTK_CONTAINER(vol_frame), vol_vbox);

  w->label_volctrl = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w->label_volctrl), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vol_vbox), w->label_volctrl, FALSE, FALSE, 0);

  GtkWidget* vol_sel_hbox = gtk_hbox_new(FALSE, 4);
  w->combo_volctrl = gtk_combo_box_text_new();
  int n_ctrls = 0;
  char** ctrls = gui_alsa_mixer_enumerate(NULL, &n_ctrls);
  const char* current_name =
      mw->sysvol ? gui_alsa_mixer_selem_name(mw->sysvol) : NULL;
  for (int i = 0; i < n_ctrls; i++) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->combo_volctrl),
                                    ctrls[i]);
    if (current_name && strcmp(ctrls[i], current_name) == 0)
      gtk_combo_box_set_active(GTK_COMBO_BOX(w->combo_volctrl), i);
  }
  gui_alsa_mixer_free_names(ctrls, n_ctrls);
  g_signal_connect(w->combo_volctrl, "changed",
                    G_CALLBACK(on_volctrl_changed), w);
  gtk_box_pack_start(GTK_BOX(vol_sel_hbox), w->combo_volctrl, TRUE, TRUE, 0);
  GtkWidget* detect_btn = gtk_button_new_with_label("Detect");
  g_signal_connect(detect_btn, "clicked",
                    G_CALLBACK(on_volctrl_detect_clicked), w);
  gtk_box_pack_start(GTK_BOX(vol_sel_hbox), detect_btn, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vol_vbox), vol_sel_hbox, FALSE, FALSE, 0);

  w->check_ln_scale = gtk_check_button_new_with_label("Linear volume scale");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->check_ln_scale),
                                mw->sysvol_linear);
  g_signal_connect(w->check_ln_scale, "toggled",
                    G_CALLBACK(on_ln_scale_toggled), w);
  gtk_box_pack_start(GTK_BOX(vol_vbox), w->check_ln_scale, FALSE, FALSE, 0);

  w->check_sv_vol_pos = gtk_check_button_new_with_label(
      "Remember volume level between sessions");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->check_sv_vol_pos),
                                mw->auto_save_volume_pos);
  gtk_box_pack_start(GTK_BOX(vol_vbox), w->check_sv_vol_pos, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(vol_page_vbox), vol_frame, FALSE, FALSE, 0);
  refresh_volctrl_label(w);

  w->sync_timer_id = g_timeout_add(500, on_sync_timer, w);
}

void gui_mixer_win_toggle_visible(gui_mixer_win* w) {
  if (gtk_widget_get_visible(w->window)) {
    gtk_widget_hide(w->window);
  } else {
    gtk_widget_show_all(w->window);
  }
}

void gui_mixer_win_destroy(gui_mixer_win* w) {
  if (w->sync_timer_id) g_source_remove(w->sync_timer_id);
  gtk_widget_destroy(w->window);
}

bool gui_mixer_win_use_chip_type_list(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(w->cb_use_chip_type_list));
}

bool gui_mixer_win_use_channel_mode_list(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(w->cb_use_channel_mode_list));
}

bool gui_mixer_win_use_ay_freq_list(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(w->cb_use_ay_freq_list));
}

bool gui_mixer_win_use_int_freq_list(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(w->cb_use_int_freq_list));
}

bool gui_mixer_win_use_channel_count_list(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(w->cb_use_channel_count_list));
}

bool gui_mixer_win_is_ste(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_atari_ste));
}

bool gui_mixer_win_atari_mono(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->cb_atari_mono));
}

bool gui_mixer_win_atari_ym_mono(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(w->cb_atari_ym_mono));
}

bool gui_mixer_win_auto_save_volume_pos(const gui_mixer_win* w) {
  return gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(w->check_sv_vol_pos));
}
