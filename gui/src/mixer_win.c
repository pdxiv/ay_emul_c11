#include "gui/mixer_win.h"

#include <string.h>

#include "ay_engine/player.h"
#include "mxhelper.h"

/* Applies AL/AR/BL/BR/CL/CR and chip_type on every slider/radio change,
 * matching MainWin.pas's Set_Mode_Manual ("assign fields, then
 * Calculate_Level_Tables2") - see player_ay_engine's own comment on the
 * benign cross-thread data race this shares with gui_playback's
 * `volume` (the playback thread only ever reads the resulting fixed-
 * size level_al[32]/etc. int arrays, never reallocated, so a torn read
 * mid-recalculation is, at worst, one glitchy sample - not a crash). */
static void apply_and_recalc(gui_mixer_win* w) {
  if (!w->playback->loaded) return;
  ay_engine* e = player_ay_engine(&w->playback->p);
  e->index_al = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_al));
  e->index_ar = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_ar));
  e->index_bl = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_bl));
  e->index_br = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_br));
  e->index_cl = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_cl));
  e->index_cr = (uint8_t)gtk_range_get_value(GTK_RANGE(w->scale_cr));
  e->chip_type = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->rb_ay))
                     ? AY_CHIP_TYPE_AY
                     : AY_CHIP_TYPE_YM;
  ay_engine_calculate_level_tables(e);
}

static void on_scale_changed(GtkRange* range, gpointer data) {
  (void)range;
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (w->syncing) return; /* programmatic update from on_sync_timer, not
                            * a real user change - see mixer_win.h */
  apply_and_recalc(w);
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

/* Re-syncs every control to the currently-loaded file's actual values
 * (see mixer_win.h's own comment on why - a fresh file load resets
 * ay_engine to its own defaults). Sets w->syncing so the change
 * handlers above skip apply_and_recalc for these programmatic updates,
 * instead of GLib's g_signal_handlers_block_by_func (see mixer_win.h's
 * own comment on why that's avoided here). */
static gboolean on_sync_timer(gpointer data) {
  gui_mixer_win* w = (gui_mixer_win*)data;
  if (!w->playback->loaded) return TRUE;
  ay_engine* e = player_ay_engine(&w->playback->p);

  w->syncing = true;
  gtk_range_set_value(GTK_RANGE(w->scale_al), e->index_al);
  gtk_range_set_value(GTK_RANGE(w->scale_ar), e->index_ar);
  gtk_range_set_value(GTK_RANGE(w->scale_bl), e->index_bl);
  gtk_range_set_value(GTK_RANGE(w->scale_br), e->index_br);
  gtk_range_set_value(GTK_RANGE(w->scale_cl), e->index_cl);
  gtk_range_set_value(GTK_RANGE(w->scale_cr), e->index_cr);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_ay),
                                e->chip_type == AY_CHIP_TYPE_AY);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->rb_ym),
                                e->chip_type == AY_CHIP_TYPE_YM);
  w->syncing = false;

  return TRUE; /* keep firing */
}

static GtkWidget* make_channel_slider(gui_mixer_win* w, GtkWidget* box,
                                       const char* label_text,
                                       int initial) {
  GtkWidget* vbox = gtk_vbox_new(FALSE, 2);
  GtkWidget* label = gtk_label_new(label_text);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

  GtkWidget* scale =
      gtk_vscale_new_with_range(0.0, 255.0, 1.0);
  gtk_range_set_inverted(GTK_RANGE(scale), TRUE); /* TTrackBar-like: max
                                                     * at the top */
  gtk_range_set_value(GTK_RANGE(scale), initial);
  gtk_widget_set_size_request(scale, -1, 120);
  g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_changed), w);
  gtk_box_pack_start(GTK_BOX(vbox), scale, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(box), vbox, TRUE, TRUE, 0);
  return scale;
}

static void on_helper_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_mixer_win* w = (gui_mixer_win*)data;
  gui_mxhelper_show(GTK_WINDOW(w->window), w->playback);
}

void gui_mixer_win_create(gui_mixer_win* w, GtkWindow* parent,
                           gui_playback* playback) {
  memset(w, 0, sizeof(*w));
  w->playback = playback;

  w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(w->window), "Mixer");
  if (parent) gtk_window_set_transient_for(GTK_WINDOW(w->window), parent);
  g_signal_connect(w->window, "delete-event",
                    G_CALLBACK(gtk_widget_hide_on_delete), NULL);

  GtkWidget* vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
  gtk_container_add(GTK_CONTAINER(w->window), vbox);

  /* Mixer.pas: GBChAmp ("Channel amplitude") - AL/AR/BL/BR/CL/CR, in
   * that order (MainWin.pas:1847-1852's own SetChan2 call order). */
  GtkWidget* amp_frame = gtk_frame_new("Channel amplitude");
  GtkWidget* amp_hbox = gtk_hbox_new(TRUE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(amp_hbox), 4);
  gtk_container_add(GTK_CONTAINER(amp_frame), amp_hbox);
  w->scale_al = make_channel_slider(w, amp_hbox, "A-L", 255);
  w->scale_ar = make_channel_slider(w, amp_hbox, "A-R", 13);
  w->scale_bl = make_channel_slider(w, amp_hbox, "B-L", 170);
  w->scale_br = make_channel_slider(w, amp_hbox, "B-R", 170);
  w->scale_cl = make_channel_slider(w, amp_hbox, "C-L", 13);
  w->scale_cr = make_channel_slider(w, amp_hbox, "C-R", 255);
  gtk_box_pack_start(GTK_BOX(vbox), amp_frame, TRUE, TRUE, 0);

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
  gtk_box_pack_start(GTK_BOX(vbox), type_frame, FALSE, FALSE, 0);

  /* Mixer.pas: SBHelper - opens mxhelper.pas's preset picker. */
  GtkWidget* helper_btn = gtk_button_new_with_label("Presets...");
  g_signal_connect(helper_btn, "clicked", G_CALLBACK(on_helper_clicked), w);
  gtk_box_pack_start(GTK_BOX(vbox), helper_btn, FALSE, FALSE, 0);

  /* Mixer.pas: CheckBox1/CBChTypeLst/CBChFrqLst/CBIntFrqLst - all four
   * "Get from list" (default Checked=True, Mixer.pas:901-904), gating
   * ItemEdit.pas's per-item overrides (MIG-0088). Grouped into their
   * own frame here since this port doesn't reproduce the surrounding
   * GBChFrq/GBIntFrq current-value display panels those two checkboxes
   * live inside in the original (out of scope per the approved
   * "Mixer's AY-relevant tab only" answer) - only the gate itself is
   * needed, read directly by gui/src/mainwin.c's do_load_song. */
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
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cb_use_chip_type_list),
                                TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cb_use_channel_mode_list),
                                TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cb_use_ay_freq_list),
                                TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->cb_use_int_freq_list),
                                TRUE);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_chip_type_list, TRUE, TRUE,
                      0);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_channel_mode_list, TRUE,
                      TRUE, 0);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_ay_freq_list, TRUE, TRUE,
                      0);
  gtk_box_pack_start(GTK_BOX(list_vbox), w->cb_use_int_freq_list, TRUE, TRUE,
                      0);
  gtk_box_pack_start(GTK_BOX(vbox), list_frame, FALSE, FALSE, 0);

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
