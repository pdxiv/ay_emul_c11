/* Real wiring for the ProgBox.lfm-generated skeleton (see
 * tools/lfm_gen/lfm_gen.py and PORTING_TO_C11_LINUX.md §5.1) - used as
 * the folder-scan progress dialog for gui/src/playlist_win.c's
 * "Add Folder" button (ProgBox.pas: "Searching for tunes").
 *
 * ProgBox.pas has two buttons: "Abort current operation" and "Switch
 * off tunes finder" (a global auto-scan settings toggle). Only Abort is
 * wired here - this port has no background auto-scan feature to switch
 * off (see migration_debt.yaml), so that second button is omitted
 * rather than drawn as a dead control.
 */
#ifndef GUI_DIALOGS_PROGBOX_H
#define GUI_DIALOGS_PROGBOX_H

#include <gtk/gtk.h>
#include <stdbool.h>

typedef struct gui_prbox {
  GtkWidget* window;
  GtkWidget* progress;
  GtkWidget* label;
  bool aborted; /* set true by the Abort button; caller's scan-progress
                 * callback should check this and stop */
} gui_prbox;

void gui_prbox_create(gui_prbox* d, GtkWindow* parent, const char* text);
/* Advances the indeterminate progress bar and pumps pending GTK events
 * (so the window stays responsive and the Abort button remains
 * clickable during a synchronous directory scan) - call this from the
 * scan's own progress callback. */
void gui_prbox_pulse(gui_prbox* d);
void gui_prbox_destroy(gui_prbox* d);

#endif /* GUI_DIALOGS_PROGBOX_H */
