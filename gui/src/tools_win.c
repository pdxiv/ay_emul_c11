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

  refresh_skin_label(w);
  char vis_buf[16];
  snprintf(vis_buf, sizeof(vis_buf), "%d", mw->vis_period_ms);
  gtk_entry_set_text(GTK_ENTRY(w->entry_vis_period), vis_buf);
}

void gui_tools_win_toggle_visible(gui_tools_win* w) {
  if (gtk_widget_get_visible(w->window)) {
    gtk_widget_hide(w->window);
  } else {
    gtk_widget_show_all(w->window);
  }
}

void gui_tools_win_destroy(gui_tools_win* w) { gtk_widget_destroy(w->window); }
