/* C11/GTK2 port of MainWin.pas's PrepareRgn (main-window irregular shape
 * only - see gui/src/regions.c's file comment for exactly what's ported
 * vs deferred).
 */
#ifndef GUI_REGIONS_H
#define GUI_REGIONS_H

#include <gdk/gdk.h>

/* Builds the main window's irregular (non-rectangular) silhouette region
 * from the static rgn.inc span table (MainWin.pas:3288-3298) and applies
 * it to `window` via gdk_window_shape_combine_region - the C11/GDK
 * equivalent of the original's CreateRectRgn+CombineRgn(RGN_OR) loop
 * followed by SetWindowRgn. Call after the window has a realized
 * GdkWindow (i.e. from a "realize" signal handler or after
 * gtk_widget_show, not before). No DPI/`Scale` support yet (see file
 * comment) - always applies the table at 1:1 pixel scale. */
void gui_apply_main_window_shape(GdkWindow* window);

/* Same idea as gui_apply_main_window_shape, but for About.pas's own
 * 343x346 window silhouette (ay_emul/rgn2.inc's separate 300-span
 * table, nrects2=299) - the shape gui/src/regions.c's file comment
 * already flagged as belonging to a future About-dialog milestone. */
void gui_apply_about_window_shape(GdkWindow* window);

#endif /* GUI_REGIONS_H */
