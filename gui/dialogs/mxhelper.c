#include "mxhelper.h"

#include "ay_engine/player.h"

static const char* const PRESET_LABELS[13] = {
    "AY ABC Stereo", "AY ACB Stereo", "AY BAC Stereo", "AY BCA Stereo",
    "AY CAB Stereo", "AY CBA Stereo", "YM ABC Stereo", "YM ACB Stereo",
    "YM BAC Stereo", "YM BCA Stereo", "YM CAB Stereo", "YM CBA Stereo",
    "Mono",
};

typedef struct mode_coefs {
  uint8_t al, ar, bl, br, cl, cr;
} mode_coefs;

/* MainWin.pas: CalcModeCoefs (1953-2027) - the DMA/TS/BeeperMax outputs
 * are dropped here (see mxhelper.h's own comment on why). `echo` is 85
 * for AY_Chip, 13 for YM_Chip (CalcModeCoefs:1963-1965). */
static mode_coefs calc_mode_coefs(int mode, int echo) {
  mode_coefs c = {255, 255, 255, 255, 255, 255}; /* Mode 0: Mono */
  switch (mode) {
    case 1:
      c = (mode_coefs){255, (uint8_t)echo, 170, 170, (uint8_t)echo, 255};
      break;
    case 2:
      c = (mode_coefs){255, (uint8_t)echo, (uint8_t)echo, 255, 170, 170};
      break;
    case 3:
      c = (mode_coefs){170, 170, 255, (uint8_t)echo, (uint8_t)echo, 255};
      break;
    case 4:
      c = (mode_coefs){(uint8_t)echo, 255, 255, (uint8_t)echo, 170, 170};
      break;
    case 5:
      c = (mode_coefs){170, 170, (uint8_t)echo, 255, 255, (uint8_t)echo};
      break;
    case 6:
      c = (mode_coefs){(uint8_t)echo, 255, 170, 170, 255, (uint8_t)echo};
      break;
    default:
      break; /* Mode 0 (Mono) already set above */
  }
  return c;
}

void mxhelper_calc_mode_coefs(int mode, int echo, uint8_t* al, uint8_t* ar,
                               uint8_t* bl, uint8_t* br, uint8_t* cl,
                               uint8_t* cr) {
  mode_coefs c = calc_mode_coefs(mode, echo);
  *al = c.al;
  *ar = c.ar;
  *bl = c.bl;
  *br = c.br;
  *cl = c.cl;
  *cr = c.cr;
}

/* Mixer.pas: SBHelperClick's ChansRG.ItemIndex -> (Mode, ChType)
 * mapping (601-613). */
static void apply_preset(gui_playback* pb, int item_index) {
  int i = item_index + 1;
  ay_chip_type chip_type;
  if (i > 6) {
    i -= 6;
    if (i == 7) i = 0; /* Mono (item_index 12) */
    chip_type = AY_CHIP_TYPE_YM;
  } else {
    chip_type = AY_CHIP_TYPE_AY;
  }

  int echo = (chip_type == AY_CHIP_TYPE_AY) ? 85 : 13;
  mode_coefs c = calc_mode_coefs(i, echo);

  ay_engine* e = player_ay_engine(&pb->p);
  e->chip_type = chip_type;
  e->index_al = c.al;
  e->index_ar = c.ar;
  e->index_bl = c.bl;
  e->index_br = c.br;
  e->index_cl = c.cl;
  e->index_cr = c.cr;
  ay_engine_calculate_level_tables(e);
}

void gui_mxhelper_show(GtkWindow* parent, gui_playback* playback) {
  if (!playback->loaded) return;

  GtkWidget* dlg = gtk_dialog_new_with_buttons(
      "Mixer Amplifier Helper", parent,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, NULL, 0, NULL);
  gtk_dialog_add_button(GTK_DIALOG(dlg), "Set", GTK_RESPONSE_ACCEPT);
  gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancel", GTK_RESPONSE_CANCEL);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

  GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
  GtkWidget* frame = gtk_frame_new("AY-3-8910/YM2149F Channels Allocation");
  gtk_container_set_border_width(GTK_CONTAINER(content), 6);
  gtk_container_add(GTK_CONTAINER(content), frame);

  GtkWidget* grid_vbox = gtk_vbox_new(TRUE, 2);
  gtk_container_set_border_width(GTK_CONTAINER(grid_vbox), 6);
  gtk_container_add(GTK_CONTAINER(frame), grid_vbox);

  /* mxhelper.lfm: ChansRG.ItemIndex = 6 default ("YM ABC Stereo"). */
  GtkWidget* radios[13];
  radios[0] = gtk_radio_button_new_with_label(NULL, PRESET_LABELS[0]);
  gtk_box_pack_start(GTK_BOX(grid_vbox), radios[0], FALSE, FALSE, 0);
  for (int i = 1; i < 13; i++) {
    radios[i] = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(radios[0]), PRESET_LABELS[i]);
    gtk_box_pack_start(GTK_BOX(grid_vbox), radios[i], FALSE, FALSE, 0);
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radios[6]), TRUE);

  gtk_widget_show_all(dlg);

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    for (int i = 0; i < 13; i++) {
      if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radios[i]))) {
        apply_preset(playback, i);
        break;
      }
    }
  }
  gtk_widget_destroy(dlg);
}
