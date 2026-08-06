/* Started as tools/lfm_gen/lfm_gen.py's generated skeleton from
 * ay_emul/ProgBox.lfm (see MIG-0066), since hand-completed with real
 * wiring (see gui/dialogs/progbox.h and migration_debt.yaml) - no
 * longer pure generator output, kept in gui/dialogs/ since it's still
 * the same dialog. Uses a GtkVBox (idiomatic GTK2 layout), not the
 * generator's default GtkFixed - ProgBox isn't part of MainWin's skin,
 * so there's no literal-coordinate-fidelity reason to keep GtkFixed
 * here once the dialog is hand-wired.
 */
#include "progbox.h"

static void on_abort_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  ((gui_prbox*)data)->aborted = true;
}

void gui_prbox_create(gui_prbox* d, GtkWindow* parent, const char* text) {
  d->aborted = false;

  d->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(d->window), "Searching for tunes");
  gtk_window_set_default_size(GTK_WINDOW(d->window), 360, -1);
  gtk_window_set_modal(GTK_WINDOW(d->window), TRUE);
  if (parent) gtk_window_set_transient_for(GTK_WINDOW(d->window), parent);
  gtk_container_set_border_width(GTK_CONTAINER(d->window), 8);

  GtkWidget* vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_add(GTK_CONTAINER(d->window), vbox);

  d->label = gtk_label_new(text);
  gtk_misc_set_alignment(GTK_MISC(d->label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vbox), d->label, FALSE, FALSE, 0);

  d->progress = gtk_progress_bar_new();
  gtk_box_pack_start(GTK_BOX(vbox), d->progress, FALSE, FALSE, 0);

  GtkWidget* abort_btn = gtk_button_new_with_label("Abort current operation");
  g_signal_connect(abort_btn, "clicked", G_CALLBACK(on_abort_clicked), d);
  gtk_box_pack_start(GTK_BOX(vbox), abort_btn, FALSE, FALSE, 0);

  gtk_widget_show_all(d->window);
  while (gtk_events_pending()) gtk_main_iteration();
}

void gui_prbox_pulse(gui_prbox* d) {
  gtk_progress_bar_pulse(GTK_PROGRESS_BAR(d->progress));
  while (gtk_events_pending()) gtk_main_iteration();
}

void gui_prbox_destroy(gui_prbox* d) {
  gtk_widget_destroy(d->window);
}
