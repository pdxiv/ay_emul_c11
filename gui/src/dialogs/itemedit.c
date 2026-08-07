#include "gui/dialogs/itemedit.h"

#include <stdlib.h>
#include <string.h>

#include "gui/dialogs/mxhelper.h"

/* Mixer.pas: AY_Chip/YM_Chip radio pair, plus a third "none" state this
 * dialog needs that the Mixer window doesn't (a per-item override can
 * be entirely absent - gui_playlist_overrides::has_chip_type). */
typedef struct itemedit_widgets {
  GtkWidget* rb_chip_none, *rb_chip_ay, *rb_chip_ym;

  GtkWidget* rb_mode_none, *rb_mode_preset, *rb_mode_manual;
  GtkWidget* combo_preset;
  GtkWidget* entry_al, *entry_ar, *entry_bl, *entry_br, *entry_cl, *entry_cr;

  GtkWidget* cb_ay_freq;
  GtkWidget* entry_ay_freq;
  GtkWidget* cb_int_freq;
  GtkWidget* entry_int_freq;

  GtkWidget* entry_title, *entry_author, *entry_program, *entry_tracker;
  GtkWidget* entry_computer, *entry_date;
  GtkWidget* text_comment;
} itemedit_widgets;

/* ItemEdit.pas's ChansRG preset list, same 13 entries/order as
 * mxhelper.c's own PRESET_LABELS (mode 0-6 x chip type folded into a
 * single combo here, since this dialog's chip-type override is a
 * separate control already). */
static const char* const MODE_LABELS[7] = {
    "Mono", "ABC Stereo", "ACB Stereo", "BAC Stereo",
    "BCA Stereo", "CAB Stereo", "CBA Stereo",
};

static GtkWidget* labeled_entry(GtkWidget* grid, int row, const char* label,
                                 const char* initial) {
  GtkWidget* lbl = gtk_label_new(label);
  gtk_misc_set_alignment(GTK_MISC(lbl), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(grid), lbl, 0, 1, row, row + 1, GTK_FILL,
                    GTK_FILL, 4, 2);
  GtkWidget* entry = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(entry), initial ? initial : "");
  gtk_table_attach(GTK_TABLE(grid), entry, 1, 2, row, row + 1,
                    GTK_EXPAND | GTK_FILL, GTK_FILL, 4, 2);
  return entry;
}

static void int_to_str(int v, char* out, size_t cap) {
  if (v < 0) {
    out[0] = '\0';
  } else {
    snprintf(out, cap, "%d", v);
  }
}

static GtkWidget* build_metadata_page(itemedit_widgets* iw,
                                       const gui_playlist_entry* e) {
  GtkWidget* grid = gtk_table_new(7, 2, FALSE);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 6);

  const char* title =
      e->overrides.title[0] ? e->overrides.title : e->title;
  const char* author =
      e->overrides.author[0] ? e->overrides.author : e->author;

  iw->entry_title = labeled_entry(grid, 0, "Title:", title);
  iw->entry_author = labeled_entry(grid, 1, "Author:", author);
  iw->entry_program = labeled_entry(grid, 2, "Program:", e->overrides.program);
  iw->entry_tracker = labeled_entry(grid, 3, "Tracker:", e->overrides.tracker);
  iw->entry_computer =
      labeled_entry(grid, 4, "Computer:", e->overrides.computer);
  iw->entry_date = labeled_entry(grid, 5, "Date:", e->overrides.date);

  GtkWidget* clabel = gtk_label_new("Comment:");
  gtk_misc_set_alignment(GTK_MISC(clabel), 0.0, 0.0);
  gtk_table_attach(GTK_TABLE(grid), clabel, 0, 1, 6, 7, GTK_FILL, GTK_FILL, 4,
                    2);
  iw->text_comment = gtk_text_view_new();
  gtk_widget_set_size_request(iw->text_comment, -1, 60);
  GtkTextBuffer* buf =
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(iw->text_comment));
  gtk_text_buffer_set_text(buf, e->overrides.comment, -1);
  gtk_table_attach(GTK_TABLE(grid), iw->text_comment, 1, 2, 6, 7,
                    GTK_EXPAND | GTK_FILL, GTK_FILL, 4, 2);

  return grid;
}

static GtkWidget* build_chip_page(itemedit_widgets* iw,
                                   const gui_playlist_entry* e) {
  GtkWidget* vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);

  /* Mixer.pas: RBChTypeAY/RBChTypeYM, plus "none" for this dialog's own
   * has_chip_type sentinel. */
  GtkWidget* type_frame = gtk_frame_new("Chip type");
  GtkWidget* type_hbox = gtk_hbox_new(TRUE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(type_hbox), 4);
  gtk_container_add(GTK_CONTAINER(type_frame), type_hbox);
  iw->rb_chip_none = gtk_radio_button_new_with_label(NULL, "(unchanged)");
  iw->rb_chip_ay = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(iw->rb_chip_none), "AY-3-8910");
  iw->rb_chip_ym = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(iw->rb_chip_none), "YM2149F");
  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(e->overrides.has_chip_type
                             ? (e->overrides.chip_type == AY_CHIP_TYPE_AY
                                    ? iw->rb_chip_ay
                                    : iw->rb_chip_ym)
                             : iw->rb_chip_none),
      TRUE);
  gtk_box_pack_start(GTK_BOX(type_hbox), iw->rb_chip_none, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(type_hbox), iw->rb_chip_ay, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(type_hbox), iw->rb_chip_ym, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), type_frame, FALSE, FALSE, 0);

  /* ItemEdit.pas: ChansRG (preset) + CustomChAlloc (manual AL..CR) -
   * same channel-allocation choice as Mixer's own "Presets..." helper
   * (mxhelper.c), plus "(unchanged)"/manual options this dialog needs
   * that the live Mixer window doesn't. */
  GtkWidget* mode_frame = gtk_frame_new("Channel amplitude");
  GtkWidget* mode_vbox = gtk_vbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(mode_vbox), 4);
  gtk_container_add(GTK_CONTAINER(mode_frame), mode_vbox);

  GtkWidget* mode_hbox = gtk_hbox_new(FALSE, 4);
  iw->rb_mode_none = gtk_radio_button_new_with_label(NULL, "(unchanged)");
  iw->rb_mode_preset = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(iw->rb_mode_none), "Preset:");
  iw->rb_mode_manual = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(iw->rb_mode_none), "Manual AL/AR/BL/BR/CL/CR:");
  gtk_box_pack_start(GTK_BOX(mode_hbox), iw->rb_mode_none, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(mode_vbox), mode_hbox, FALSE, FALSE, 0);

  GtkWidget* preset_hbox = gtk_hbox_new(FALSE, 4);
  gtk_box_pack_start(GTK_BOX(preset_hbox), iw->rb_mode_preset, FALSE, FALSE,
                      0);
  iw->combo_preset = gtk_combo_box_text_new();
  for (int i = 0; i < 7; i++) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(iw->combo_preset),
                                    MODE_LABELS[i]);
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(iw->combo_preset),
                            e->overrides.channel_mode >= 0 &&
                                    e->overrides.channel_mode <= 6
                                ? e->overrides.channel_mode
                                : 0);
  gtk_box_pack_start(GTK_BOX(preset_hbox), iw->combo_preset, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(mode_vbox), preset_hbox, FALSE, FALSE, 0);

  GtkWidget* manual_hbox = gtk_hbox_new(FALSE, 2);
  gtk_box_pack_start(GTK_BOX(manual_hbox), iw->rb_mode_manual, FALSE, FALSE,
                      0);
  char al[8], ar[8], bl[8], br[8], cl[8], cr[8];
  int_to_str(e->overrides.channel_mode == -2 ? e->overrides.al : -1, al,
             sizeof(al));
  int_to_str(e->overrides.channel_mode == -2 ? e->overrides.ar : -1, ar,
             sizeof(ar));
  int_to_str(e->overrides.channel_mode == -2 ? e->overrides.bl : -1, bl,
             sizeof(bl));
  int_to_str(e->overrides.channel_mode == -2 ? e->overrides.br : -1, br,
             sizeof(br));
  int_to_str(e->overrides.channel_mode == -2 ? e->overrides.cl : -1, cl,
             sizeof(cl));
  int_to_str(e->overrides.channel_mode == -2 ? e->overrides.cr : -1, cr,
             sizeof(cr));
  iw->entry_al = gtk_entry_new();
  iw->entry_ar = gtk_entry_new();
  iw->entry_bl = gtk_entry_new();
  iw->entry_br = gtk_entry_new();
  iw->entry_cl = gtk_entry_new();
  iw->entry_cr = gtk_entry_new();
  GtkWidget* manual_entries[6] = {iw->entry_al, iw->entry_ar, iw->entry_bl,
                                   iw->entry_br, iw->entry_cl, iw->entry_cr};
  const char* manual_vals[6] = {al, ar, bl, br, cl, cr};
  for (int i = 0; i < 6; i++) {
    gtk_entry_set_text(GTK_ENTRY(manual_entries[i]), manual_vals[i]);
    gtk_entry_set_width_chars(GTK_ENTRY(manual_entries[i]), 4);
    gtk_box_pack_start(GTK_BOX(manual_hbox), manual_entries[i], FALSE, FALSE,
                        0);
  }
  gtk_box_pack_start(GTK_BOX(mode_vbox), manual_hbox, FALSE, FALSE, 0);

  GtkWidget* active_mode_rb = iw->rb_mode_none;
  if (e->overrides.channel_mode == -2) {
    active_mode_rb = iw->rb_mode_manual;
  } else if (e->overrides.channel_mode >= 0) {
    active_mode_rb = iw->rb_mode_preset;
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(active_mode_rb), TRUE);

  gtk_box_pack_start(GTK_BOX(vbox), mode_frame, FALSE, FALSE, 0);

  /* ItemEdit.pas: CBChFreq/EChFrOther (sound-chip frequency) and
   * CBIntFreq/EPlrFrOther (interrupt/VBL frequency) - collapsed to a
   * single checkbox + raw-Hz entry each here (see itemedit.h's own
   * comment on why the original's named-preset combo isn't
   * reproduced). */
  GtkWidget* freq_frame = gtk_frame_new("Frequency overrides");
  GtkWidget* freq_table = gtk_table_new(2, 2, FALSE);
  gtk_container_set_border_width(GTK_CONTAINER(freq_table), 4);
  gtk_container_add(GTK_CONTAINER(freq_frame), freq_table);

  iw->cb_ay_freq = gtk_check_button_new_with_label("Sound chip frequency (Hz):");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(iw->cb_ay_freq),
                                e->overrides.ay_freq != -1);
  gtk_table_attach(GTK_TABLE(freq_table), iw->cb_ay_freq, 0, 1, 0, 1,
                    GTK_FILL, GTK_FILL, 4, 2);
  iw->entry_ay_freq = gtk_entry_new();
  {
    char buf[16];
    int_to_str(e->overrides.ay_freq != -1 ? e->overrides.ay_freq : 1773400,
               buf, sizeof(buf));
    gtk_entry_set_text(GTK_ENTRY(iw->entry_ay_freq), buf);
  }
  gtk_table_attach(GTK_TABLE(freq_table), iw->entry_ay_freq, 1, 2, 0, 1,
                    GTK_EXPAND | GTK_FILL, GTK_FILL, 4, 2);

  iw->cb_int_freq =
      gtk_check_button_new_with_label("Interrupt frequency (Hz x1000):");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(iw->cb_int_freq),
                                e->overrides.int_freq != -1);
  gtk_table_attach(GTK_TABLE(freq_table), iw->cb_int_freq, 0, 1, 1, 2,
                    GTK_FILL, GTK_FILL, 4, 2);
  iw->entry_int_freq = gtk_entry_new();
  {
    char buf[16];
    int_to_str(e->overrides.int_freq != -1 ? e->overrides.int_freq : 50000,
               buf, sizeof(buf));
    gtk_entry_set_text(GTK_ENTRY(iw->entry_int_freq), buf);
  }
  gtk_table_attach(GTK_TABLE(freq_table), iw->entry_int_freq, 1, 2, 1, 2,
                    GTK_EXPAND | GTK_FILL, GTK_FILL, 4, 2);

  gtk_box_pack_start(GTK_BOX(vbox), freq_frame, FALSE, FALSE, 0);

  return vbox;
}

static int parse_int_or(GtkWidget* entry, int fallback) {
  const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
  char* end;
  long v = strtol(text, &end, 10);
  if (end == text) return fallback;
  return (int)v;
}

static void copy_field(char* dst, size_t cap, GtkWidget* entry) {
  strncpy(dst, gtk_entry_get_text(GTK_ENTRY(entry)), cap - 1);
  dst[cap - 1] = '\0';
}

void gui_itemedit_show(GtkWindow* parent, gui_playlist_entry* e) {
  itemedit_widgets iw;
  memset(&iw, 0, sizeof(iw));

  GtkWidget* dlg = gtk_dialog_new_with_buttons(
      "Playlist Item Adjusting", parent,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, NULL, 0, NULL);
  gtk_dialog_add_button(GTK_DIALOG(dlg), "OK", GTK_RESPONSE_ACCEPT);
  gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancel", GTK_RESPONSE_CANCEL);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
  gtk_window_set_default_size(GTK_WINDOW(dlg), 420, 420);

  GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
  GtkWidget* notebook = gtk_notebook_new();
  gtk_container_add(GTK_CONTAINER(content), notebook);

  GtkWidget* meta_page = build_metadata_page(&iw, e);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), meta_page,
                            gtk_label_new("Metadata"));
  GtkWidget* chip_page = build_chip_page(&iw, e);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), chip_page,
                            gtk_label_new("Sound chip"));

  gtk_widget_show_all(dlg);

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    gui_playlist_overrides* ov = &e->overrides;

    copy_field(ov->title, sizeof(ov->title), iw.entry_title);
    copy_field(ov->author, sizeof(ov->author), iw.entry_author);
    copy_field(ov->program, sizeof(ov->program), iw.entry_program);
    copy_field(ov->tracker, sizeof(ov->tracker), iw.entry_tracker);
    copy_field(ov->computer, sizeof(ov->computer), iw.entry_computer);
    copy_field(ov->date, sizeof(ov->date), iw.entry_date);
    {
      GtkTextBuffer* buf =
          gtk_text_view_get_buffer(GTK_TEXT_VIEW(iw.text_comment));
      GtkTextIter start, end_it;
      gtk_text_buffer_get_bounds(buf, &start, &end_it);
      char* text = gtk_text_buffer_get_text(buf, &start, &end_it, FALSE);
      strncpy(ov->comment, text, sizeof(ov->comment) - 1);
      ov->comment[sizeof(ov->comment) - 1] = '\0';
      g_free(text);
    }

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(iw.rb_chip_none))) {
      ov->has_chip_type = false;
    } else {
      ov->has_chip_type = true;
      ov->chip_type = gtk_toggle_button_get_active(
                           GTK_TOGGLE_BUTTON(iw.rb_chip_ay))
                          ? AY_CHIP_TYPE_AY
                          : AY_CHIP_TYPE_YM;
    }

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(iw.rb_mode_none))) {
      ov->channel_mode = -1;
    } else if (gtk_toggle_button_get_active(
                   GTK_TOGGLE_BUTTON(iw.rb_mode_manual))) {
      ov->channel_mode = -2;
      ov->al = (uint8_t)parse_int_or(iw.entry_al, 255);
      ov->ar = (uint8_t)parse_int_or(iw.entry_ar, 255);
      ov->bl = (uint8_t)parse_int_or(iw.entry_bl, 255);
      ov->br = (uint8_t)parse_int_or(iw.entry_br, 255);
      ov->cl = (uint8_t)parse_int_or(iw.entry_cl, 255);
      ov->cr = (uint8_t)parse_int_or(iw.entry_cr, 255);
    } else {
      ov->channel_mode =
          gtk_combo_box_get_active(GTK_COMBO_BOX(iw.combo_preset));
    }

    ov->ay_freq =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(iw.cb_ay_freq))
            ? parse_int_or(iw.entry_ay_freq, 1773400)
            : -1;
    ov->int_freq =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(iw.cb_int_freq))
            ? parse_int_or(iw.entry_int_freq, 50000)
            : -1;

    gui_playlist_entry_refresh_display(e);
  }

  gtk_widget_destroy(dlg);
}
