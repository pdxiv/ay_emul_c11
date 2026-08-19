#include "gui/tools_win.h"

#include <stdio.h>
#include <string.h>

#include "gui/mainwin.h"

static void refresh_skin_label(gui_tools_win* w) {
  char text[600];
  const char* author = w->mw->skin.author && w->mw->skin.author[0]
                            ? w->mw->skin.author
                            : "(unknown)";
  const char* comment = w->mw->skin.comment && w->mw->skin.comment[0]
                             ? w->mw->skin.comment
                             : "";
  snprintf(text, sizeof(text), "Author: %s\nComment: %s", author, comment);
  gtk_label_set_text(GTK_LABEL(w->label_skin_info), text);
}

/* Tools.pas: EMFolderEditingDone (~431-437) - only accepts an existing
 * directory, silently ignored otherwise (matches `if DirectoryExists(s)
 * then FrmMain.DefaultDirectory := s` - no error dialog on rejection).
 * Fires on Enter ("activate"), not on focus-out like the original's own
 * EditingDone - matching this port's established GtkEntry convention
 * (no other dialog in this codebase hooks focus-out-event, which also
 * has a different callback signature than "activate" and would need
 * its own wrapper). */
static void on_folder_changed(GtkWidget* widget, gpointer data) {
  gui_tools_win* w = (gui_tools_win*)data;
  const char* text = gtk_entry_get_text(GTK_ENTRY(widget));
  if (g_file_test(text, G_FILE_TEST_IS_DIR)) {
    strncpy(w->mw->default_dir, text, sizeof(w->mw->default_dir) - 1);
    w->mw->default_dir[sizeof(w->mw->default_dir) - 1] = '\0';
  }
}

static void on_browse_folder_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_tools_win* w = (gui_tools_win*)data;
  GtkWidget* dlg = gtk_file_chooser_dialog_new(
      "Default folder", GTK_WINDOW(w->window),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char* dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    strncpy(w->mw->default_dir, dir, sizeof(w->mw->default_dir) - 1);
    w->mw->default_dir[sizeof(w->mw->default_dir) - 1] = '\0';
    gtk_entry_set_text(GTK_ENTRY(w->entry_folder), dir);
    g_free(dir);
  }
  gtk_widget_destroy(dlg);
}

/* Tools.pas: EVisPeriodEditingDone (~440-450) - invalid input is
 * ignored and the entry snaps back to the last accepted value, exactly
 * matching the original's own try/except + `finally EVisPeriod.Text :=
 * IntToStr(VisTimerPeriod)`. */
static void on_vis_period_changed(GtkWidget* widget, gpointer data) {
  gui_tools_win* w = (gui_tools_win*)data;
  const char* text = gtk_entry_get_text(GTK_ENTRY(widget));
  char* end;
  long v = strtol(text, &end, 10);
  if (end != text && *end == '\0') {
    gui_mainwin_set_vis_period(w->mw, (int)v);
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", w->mw->vis_period_ms);
  gtk_entry_set_text(GTK_ENTRY(widget), buf);
}

/* Tools.pas: BChSkinClick (~615-640) - a GtkFileChooserDialog picking a
 * `.ays` file, loaded via gui_skin_load_file (previously implemented
 * but unused - see gui/include/gui/skin.h's own file comment). Loads
 * into a temporary gui_skin first so a malformed/unreadable file
 * leaves the currently-displayed skin untouched rather than blanking
 * the window (a real failure mode a raw in-place swap would risk). */
static void on_change_skin_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_tools_win* w = (gui_tools_win*)data;
  GtkWidget* dlg = gtk_file_chooser_dialog_new(
      "Load skin", GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN,
      GTK_RESPONSE_ACCEPT, NULL);
  GtkFileFilter* filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "AY Emulator Skin (*.ays)");
  gtk_file_filter_add_pattern(filter, "*.ays");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    gui_skin new_skin;
    if (gui_skin_load_file(&new_skin, path)) {
      gui_skin_free(&w->mw->skin);
      w->mw->skin = new_skin;
      refresh_skin_label(w);
      gtk_widget_queue_draw(w->mw->area);
    } else {
      GtkWidget* msg = gtk_message_dialog_new(
          GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
          GTK_BUTTONS_OK, "Could not load skin from '%s'", path);
      gtk_dialog_run(GTK_DIALOG(msg));
      gtk_widget_destroy(msg);
    }
    g_free(path);
  }
  gtk_widget_destroy(dlg);
}

/* Tools.pas: BStdSkinClick (~641-...) - reverts to the embedded
 * default skin. */
static void on_default_skin_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_tools_win* w = (gui_tools_win*)data;
  gui_skin new_skin;
  if (gui_skin_load_default(&new_skin)) {
    gui_skin_free(&w->mw->skin);
    w->mw->skin = new_skin;
    refresh_skin_label(w);
    gtk_widget_queue_draw(w->mw->area);
  }
}

/* Tools.pas: CBDoubleSzClick (~324-334) - `NewS := Ord(CBDoubleSz.
 * Checked) + 1; if NewS <> Scale then begin Scale := NewS; FrmMain.
 * RecreateRgn; end;`. gui_mainwin_set_scale already has the same
 * `if NewS <> Scale` no-op guard built in, so this just translates the
 * checkbox state to 1/2 and calls it. */
static void on_double_sz_toggled(GtkWidget* widget, gpointer data) {
  gui_tools_win* w = (gui_tools_win*)data;
  bool checked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  gui_mainwin_set_scale(w->mw, checked ? 2 : 1);
}

/* Tools.pas: "Icon on system tray" radio group (RadioButton8/9/10) -
 * GTK fires "toggled" for BOTH the button losing state and the one
 * gaining it in a radio group, so every handler below only acts when
 * gtk_toggle_button_get_active is true for ITS OWN button (matching
 * the original's own `if not RadioButtonN.Checked then exit`-style
 * single-direction guard used throughout this codebase's own radio-
 * button handlers, e.g. Mixer.pas's RBResamAvgClick/RBResamFIRClick). */
static void on_tray_never_toggled(GtkWidget* widget, gpointer data) {
  gui_tools_win* w = (gui_tools_win*)data;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
    gui_mainwin_set_tray_mode(w->mw, 0);
}

static void on_tray_minimize_toggled(GtkWidget* widget, gpointer data) {
  gui_tools_win* w = (gui_tools_win*)data;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
    gui_mainwin_set_tray_mode(w->mw, 1);
}

static void on_tray_always_toggled(GtkWidget* widget, gpointer data) {
  gui_tools_win* w = (gui_tools_win*)data;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
    gui_mainwin_set_tray_mode(w->mw, 2);
}

/* Applies (fg, bg) to a single preview swatch - NULL means "leave that
 * property untouched", so an unset gui_playlist_color (see its own
 * header comment) shows the swatch's inherited theme color, same
 * "unset = native" convention gui_playlist_win_refresh_colors's own
 * cell-data-func uses for the real playlist rows. */
static void style_swatch(GtkWidget* label, const GdkColor* fg,
                          const GdkColor* bg) {
  GtkWidget* box = gtk_widget_get_parent(label);
  gtk_widget_modify_fg(label, GTK_STATE_NORMAL, fg);
  gtk_widget_modify_bg(box, GTK_STATE_NORMAL, bg);
}

/* Tools.pas: FormShow's own Label1..Label17.Color/Font.Color assignment
 * (836-849) - re-applied after every color pick, since a Back/BkSel
 * change affects TWO swatches at once (the Text-role and Back-role
 * labels both preview the SAME pair - see this window's own header
 * comment). */
static void refresh_pl_swatches(gui_tools_win* w) {
  gui_playlist_win* pl = &w->mw->plwin;
  const GdkColor* text = pl->text.set ? &pl->text.color : NULL;
  const GdkColor* back = pl->back.set ? &pl->back.color : NULL;
  const GdkColor* sel_text = pl->sel_text.set ? &pl->sel_text.color : NULL;
  const GdkColor* sel_back = pl->sel_back.set ? &pl->sel_back.color : NULL;
  const GdkColor* play_text =
      pl->play_text.set ? &pl->play_text.color : NULL;
  const GdkColor* play_back =
      pl->play_back.set ? &pl->play_back.color : NULL;
  const GdkColor* play_sel_text =
      pl->play_sel_text.set ? &pl->play_sel_text.color : NULL;
  const GdkColor* err_text = pl->err_text.set ? &pl->err_text.color : NULL;
  const GdkColor* err_sel_text =
      pl->err_sel_text.set ? &pl->err_sel_text.color : NULL;

  style_swatch(w->pl_swatch_text, text, back);
  style_swatch(w->pl_swatch_back, text, back);
  style_swatch(w->pl_swatch_sel_text, sel_text, sel_back);
  style_swatch(w->pl_swatch_sel_back, sel_text, sel_back);
  style_swatch(w->pl_swatch_play_text, play_text, play_back);
  style_swatch(w->pl_swatch_play_back, play_text, play_back);
  style_swatch(w->pl_swatch_play_sel, play_sel_text, sel_back);
  /* Mixer.pas:850-853 - Label18/19 preview using PLColorBk/PLColorBkSel
   * (the NORMAL/selected backgrounds, not a separate "error back" -
   * real Pascal has none, MIG-0126). */
  style_swatch(w->pl_swatch_err, err_text, back);
  style_swatch(w->pl_swatch_err_sel, err_sel_text, sel_back);
}

/* Tools.pas: ChangePLColor (1078-1088) - `ColorDialog1.Color := Wanted
 * Color; Result := ColorDialog1.Execute; if Result then WantedColor :=
 * ColorDialog1.Color;`. Seeds the dialog from `target`'s current color
 * if set, otherwise the tree view's own current (theme) text/base
 * color - a reasonable starting point for "pick a color" when nothing
 * has been customized yet, closer to what the row actually looks like
 * right now than an arbitrary fixed seed would be. */
static void pick_pl_color(gui_tools_win* w, gui_playlist_color* target,
                           bool is_background) {
  GdkColor seed;
  if (target->set) {
    seed = target->color;
  } else {
    GtkStyle* style = gtk_widget_get_style(w->mw->plwin.tree_view);
    seed = is_background ? style->base[GTK_STATE_NORMAL]
                          : style->text[GTK_STATE_NORMAL];
  }

  GtkWidget* dlg = gtk_color_selection_dialog_new("Choose a color");
  GtkColorSelection* sel = GTK_COLOR_SELECTION(
      gtk_color_selection_dialog_get_color_selection(
          GTK_COLOR_SELECTION_DIALOG(dlg)));
  gtk_color_selection_set_current_color(sel, &seed);
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
    gtk_color_selection_get_current_color(sel, &target->color);
    target->set = true;
    refresh_pl_swatches(w);
    gui_playlist_win_refresh_colors(&w->mw->plwin);
  }
  gtk_widget_destroy(dlg);
}

static void on_pl_text_clicked(GtkWidget* widget, GdkEventButton* event,
                                gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.text, false);
}
static void on_pl_back_clicked(GtkWidget* widget, GdkEventButton* event,
                                gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.back, true);
}
static void on_pl_sel_text_clicked(GtkWidget* widget, GdkEventButton* event,
                                    gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.sel_text, false);
}
static void on_pl_sel_back_clicked(GtkWidget* widget, GdkEventButton* event,
                                    gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.sel_back, true);
}
static void on_pl_play_text_clicked(GtkWidget* widget, GdkEventButton* event,
                                     gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.play_text, false);
}
static void on_pl_play_back_clicked(GtkWidget* widget, GdkEventButton* event,
                                     gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.play_back, true);
}
static void on_pl_play_sel_clicked(GtkWidget* widget, GdkEventButton* event,
                                    gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.play_sel_text, false);
}
static void on_pl_err_clicked(GtkWidget* widget, GdkEventButton* event,
                               gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.err_text, false);
}
static void on_pl_err_sel_clicked(GtkWidget* widget, GdkEventButton* event,
                                   gpointer data) {
  (void)widget;
  (void)event;
  gui_tools_win* w = (gui_tools_win*)data;
  pick_pl_color(w, &w->mw->plwin.err_sel_text, false);
}

/* Tools.pas: SBFontClick (~1186-1191) - `FontDialog1.Font :=
 * PLArea.Font; if FontDialog1.Execute then PLArea.Font :=
 * FontDialog1.Font;`. GtkFontSelectionDialog is this port's own
 * idiomatic equivalent of TFontDialog - see gui/include/gui/
 * playlist_win.h's own struct comment for why the result is stored as
 * one PangoFontDescription rather than 4 separate Name/Size/Bold/
 * Italic fields. */
static void on_pl_font_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_tools_win* w = (gui_tools_win*)data;
  GtkWidget* dlg = gtk_font_selection_dialog_new("Choose a font");
  if (w->mw->plwin.font) {
    char* name = pango_font_description_to_string(w->mw->plwin.font);
    gtk_font_selection_dialog_set_font_name(GTK_FONT_SELECTION_DIALOG(dlg),
                                             name);
    g_free(name);
  }
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
    const char* name = gtk_font_selection_dialog_get_font_name(
        GTK_FONT_SELECTION_DIALOG(dlg));
    if (w->mw->plwin.font) pango_font_description_free(w->mw->plwin.font);
    w->mw->plwin.font = pango_font_description_from_string(name);
    gui_playlist_win_refresh_colors(&w->mw->plwin);
  }
  gtk_widget_destroy(dlg);
}

/* Builds one clickable preview swatch (GtkEventBox+GtkLabel, matching
 * Tools.pas's own Label1..Label17 - `Cursor = crHandPoint` there, an
 * EventBox here since a bare GtkLabel has no window of its own to
 * receive button-press-event). */
static GtkWidget* make_pl_swatch(const char* caption,
                                  GCallback on_clicked, gui_tools_win* w,
                                  GtkWidget** out_label) {
  GtkWidget* box = gtk_event_box_new();
  GtkWidget* label = gtk_label_new(caption);
  gtk_misc_set_padding(GTK_MISC(label), 4, 2);
  gtk_container_add(GTK_CONTAINER(box), label);
  g_signal_connect(box, "button-press-event", on_clicked, w);
  *out_label = label;
  return box;
}

void gui_tools_win_create(gui_tools_win* w, GtkWindow* parent,
                           gui_mainwin* mw) {
  memset(w, 0, sizeof(*w));
  w->mw = mw;

  w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(w->window), "Tools");
  if (parent) gtk_window_set_transient_for(GTK_WINDOW(w->window), parent);
  g_signal_connect(w->window, "delete-event",
                    G_CALLBACK(gtk_widget_hide_on_delete), NULL);

  GtkWidget* vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
  gtk_container_add(GTK_CONTAINER(w->window), vbox);

  /* Tools.pas: GBMFolder ("Default folder"). */
  GtkWidget* folder_frame = gtk_frame_new("Default folder");
  GtkWidget* folder_hbox = gtk_hbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(folder_hbox), 4);
  gtk_container_add(GTK_CONTAINER(folder_frame), folder_hbox);
  w->entry_folder = gtk_entry_new();
  g_signal_connect(w->entry_folder, "activate",
                    G_CALLBACK(on_folder_changed), w);
  gtk_box_pack_start(GTK_BOX(folder_hbox), w->entry_folder, TRUE, TRUE, 0);
  GtkWidget* browse_btn = gtk_button_new_with_label("Browse...");
  g_signal_connect(browse_btn, "clicked",
                    G_CALLBACK(on_browse_folder_clicked), w);
  gtk_box_pack_start(GTK_BOX(folder_hbox), browse_btn, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), folder_frame, FALSE, FALSE, 0);

  /* Tools.pas: GenTools's "Visualisation period, ms" (EVisPeriod). */
  GtkWidget* vis_frame = gtk_frame_new("Visualizer sample period (10-100 ms)");
  GtkWidget* vis_hbox = gtk_hbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(vis_hbox), 4);
  gtk_container_add(GTK_CONTAINER(vis_frame), vis_hbox);
  w->entry_vis_period = gtk_entry_new();
  gtk_entry_set_width_chars(GTK_ENTRY(w->entry_vis_period), 6);
  g_signal_connect(w->entry_vis_period, "activate",
                    G_CALLBACK(on_vis_period_changed), w);
  gtk_box_pack_start(GTK_BOX(vis_hbox), w->entry_vis_period, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), vis_frame, FALSE, FALSE, 0);

  /* Tools.pas: GBSkin. */
  GtkWidget* skin_frame = gtk_frame_new("Skin");
  GtkWidget* skin_vbox = gtk_vbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(skin_vbox), 4);
  gtk_container_add(GTK_CONTAINER(skin_frame), skin_vbox);
  w->label_skin_info = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w->label_skin_info), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(skin_vbox), w->label_skin_info, FALSE, FALSE, 0);
  GtkWidget* skin_btn_hbox = gtk_hbox_new(TRUE, 4);
  GtkWidget* change_skin_btn = gtk_button_new_with_label("Load skin...");
  g_signal_connect(change_skin_btn, "clicked",
                    G_CALLBACK(on_change_skin_clicked), w);
  gtk_box_pack_start(GTK_BOX(skin_btn_hbox), change_skin_btn, TRUE, TRUE, 0);
  GtkWidget* default_skin_btn = gtk_button_new_with_label("Default skin");
  g_signal_connect(default_skin_btn, "clicked",
                    G_CALLBACK(on_default_skin_clicked), w);
  gtk_box_pack_start(GTK_BOX(skin_btn_hbox), default_skin_btn, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(skin_vbox), skin_btn_hbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), skin_frame, FALSE, FALSE, 0);

  /* Tools.pas: GenTools's CBForceLoop - MIG-0114. Only meaningful for a
   * Turbosound-paired session (see do_load_song's own comment on where
   * this gets applied), same real-world scope Force_Loop has always had
   * in the original - shown here unconditionally, matching the
   * original's own GenTools tab layout (the checkbox itself doesn't
   * know or care whether a pair happens to be loaded right now). */
  w->check_force_loop = gtk_check_button_new_with_label(
      "Force loop (keep shorter Turbosound voice playing)");
  gtk_box_pack_start(GTK_BOX(vbox), w->check_force_loop, FALSE, FALSE, 0);

  /* Tools.pas: GenTools's CBDoubleSz - MIG-0119. `CBDoubleSz.Checked :=
   * Scale <> 1;` at dialog-open time (Tools.pas:814) - mirrored here by
   * reading mw->scale, since this window is created once and persists
   * (see this file's own header comment), so it must reflect whatever
   * scale gui_mainwin_create already set. */
  w->check_double_sz = gtk_check_button_new_with_label("Double window size");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->check_double_sz),
                                mw->scale != 1);
  g_signal_connect(w->check_double_sz, "toggled",
                    G_CALLBACK(on_double_sz_toggled), w);
  gtk_box_pack_start(GTK_BOX(vbox), w->check_double_sz, FALSE, FALSE, 0);

  /* Tools.pas: CheckBox40 - "Save windows position" (its own real
   * Pascal caption, matched verbatim per this session's own brief). */
  w->check_auto_save_windows_pos = gtk_check_button_new_with_label(
      "Save windows position");
  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(w->check_auto_save_windows_pos),
      mw->auto_save_windows_pos);
  gtk_box_pack_start(GTK_BOX(vbox), w->check_auto_save_windows_pos, FALSE,
                      FALSE, 0);

  /* Tools.pas: "Icon on system tray" (RadioButton8/9/10 = never/
   * minimize/always - MainWin.pas:5007-5011). */
  GtkWidget* tray_frame = gtk_frame_new("Icon on system tray");
  GtkWidget* tray_vbox = gtk_vbox_new(FALSE, 2);
  gtk_container_set_border_width(GTK_CONTAINER(tray_vbox), 4);
  gtk_container_add(GTK_CONTAINER(tray_frame), tray_vbox);
  w->radio_tray_never = gtk_radio_button_new_with_label(NULL, "Never");
  w->radio_tray_minimize = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(w->radio_tray_never), "Minimize");
  w->radio_tray_always = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(w->radio_tray_never), "Always");
  switch (mw->tray_mode) {
    case 0:
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->radio_tray_never),
                                    TRUE);
      break;
    case 1:
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->radio_tray_minimize),
                                    TRUE);
      break;
    default:
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->radio_tray_always),
                                    TRUE);
      break;
  }
  g_signal_connect(w->radio_tray_never, "toggled",
                    G_CALLBACK(on_tray_never_toggled), w);
  g_signal_connect(w->radio_tray_minimize, "toggled",
                    G_CALLBACK(on_tray_minimize_toggled), w);
  g_signal_connect(w->radio_tray_always, "toggled",
                    G_CALLBACK(on_tray_always_toggled), w);
  gtk_box_pack_start(GTK_BOX(tray_vbox), w->radio_tray_never, FALSE, FALSE,
                      0);
  gtk_box_pack_start(GTK_BOX(tray_vbox), w->radio_tray_minimize, FALSE,
                      FALSE, 0);
  gtk_box_pack_start(GTK_BOX(tray_vbox), w->radio_tray_always, FALSE, FALSE,
                      0);
  gtk_box_pack_start(GTK_BOX(vbox), tray_frame, FALSE, FALSE, 0);

  /* Tools.pas: PListOpts's "Playlist colors and font" GroupBox2
   * (MIG-0125) - see this file's own on_pl_*_clicked/refresh_pl_
   * swatches comments above. */
  GtkWidget* pl_frame = gtk_frame_new("Playlist colors and font");
  GtkWidget* pl_vbox = gtk_vbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(pl_vbox), 4);
  gtk_container_add(GTK_CONTAINER(pl_frame), pl_vbox);

  GtkWidget* pl_swatch_hbox = gtk_hbox_new(FALSE, 2);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("Text", G_CALLBACK(on_pl_text_clicked), w,
                      &w->pl_swatch_text),
      FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("Back", G_CALLBACK(on_pl_back_clicked), w,
                      &w->pl_swatch_back),
      FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("SelText", G_CALLBACK(on_pl_sel_text_clicked), w,
                      &w->pl_swatch_sel_text),
      FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("SelBack", G_CALLBACK(on_pl_sel_back_clicked), w,
                      &w->pl_swatch_sel_back),
      FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("Play Text", G_CALLBACK(on_pl_play_text_clicked), w,
                      &w->pl_swatch_play_text),
      FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("Play Back", G_CALLBACK(on_pl_play_back_clicked), w,
                      &w->pl_swatch_play_back),
      FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("Play SelText", G_CALLBACK(on_pl_play_sel_clicked), w,
                      &w->pl_swatch_play_sel),
      FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("Error Text", G_CALLBACK(on_pl_err_clicked), w,
                      &w->pl_swatch_err),
      FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(pl_swatch_hbox),
      make_pl_swatch("Err SelText", G_CALLBACK(on_pl_err_sel_clicked), w,
                      &w->pl_swatch_err_sel),
      FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(pl_vbox), pl_swatch_hbox, FALSE, FALSE, 0);

  GtkWidget* pl_font_btn = gtk_button_new_with_label("Font...");
  g_signal_connect(pl_font_btn, "clicked", G_CALLBACK(on_pl_font_clicked), w);
  gtk_box_pack_start(GTK_BOX(pl_vbox), pl_font_btn, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(vbox), pl_frame, FALSE, FALSE, 0);
  refresh_pl_swatches(w);

  refresh_skin_label(w);
  char vis_buf[16];
  snprintf(vis_buf, sizeof(vis_buf), "%d", mw->vis_period_ms);
  gtk_entry_set_text(GTK_ENTRY(w->entry_vis_period), vis_buf);
}

bool gui_tools_win_force_loop(const gui_tools_win* w) {
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->check_force_loop));
}

bool gui_tools_win_auto_save_windows_pos(const gui_tools_win* w) {
  return gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(w->check_auto_save_windows_pos));
}

void gui_tools_win_toggle_visible(gui_tools_win* w) {
  if (gtk_widget_get_visible(w->window)) {
    gtk_widget_hide(w->window);
  } else {
    gtk_widget_show_all(w->window);
  }
}

void gui_tools_win_destroy(gui_tools_win* w) { gtk_widget_destroy(w->window); }
