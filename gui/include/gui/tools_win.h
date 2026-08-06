/* C11/GTK2 port of Tools.pas's TFrmTools - MIG-0095. Full-trace-first
 * review of all 5 tabs (GenTools/FTypTools/SearchTool/FIDOTools/
 * PListOpts, ~1247 Pascal lines, the largest single dialog in the
 * original app) found the vast majority genuinely out of this port's
 * scope, not deferred on apparent complexity:
 *  - FTypTools (Windows file-type registry association), the
 *    Registration/CheckRegistration flow, GBMenu (Start Menu
 *    shortcuts), GBPrior (process priority), FIDOTools (Windows .lnk
 *    desktop-shortcut generation, EDeskFN/EDeskPfx/etc) - all
 *    Windows-registry/shell-integration features with no Linux
 *    equivalent in this port's scope.
 *  - CBStrPrescan (StreamPrescan) - BASS streaming-duration prescan,
 *    already out of scope (BASS).
 *  - CBForceLoop (Force_Loop) - traced to Players.pas:8730-8745's
 *    CheckLoopAndStop comment ("need for continue playback shorter
 *    module of TS-pair"): a TSMode/Turbosound-pair-only setting with
 *    zero effect on single-chip playback, this port's own established
 *    scope boundary (MIG-0007) already excludes TSMode entirely.
 *  - CBDoubleSz (window Scale 1x/2x) - real, but a substantial amount
 *    of additional work on its own (rescaling the skin bitmap, every
 *    button/LED/slider rect, and the window-shape region tables) -
 *    gui/src/regions.c's file comment already flagged "Scale" as a
 *    deferred item; not picked up here to keep this entry's scope
 *    bounded to what Tools.pas itself adds, not a separate rescaling
 *    project.
 *  - SearchTool (recursive tracker-file scan with per-format
 *    checkboxes) - functionally superseded by the playlist's own
 *    "Add Folder" (gui_playlist_add_directory, MIG-0071/0085), which
 *    already scans and adds every recognized format; a second,
 *    separate scan-only tool would be redundant.
 *
 * Ported (the genuinely portable subset):
 *  - EMFolder (default_dir): the initial directory for the main
 *    window's Open dialog (Tools.pas: FrmMain.DefaultDirectory).
 *  - EVisPeriod (VisTimerPeriod): the spectrum/amplitude visualizer's
 *    sample-timer interval (MIG-0094's own 30ms default, now
 *    user-adjustable).
 *  - GBSkin (BChSkin/BStdSkin): load a custom `.ays` skin file, or
 *    revert to the embedded default - wires up gui_skin_load_file
 *    (already implemented, previously unused - see gui/include/
 *    gui/skin.h) for real runtime skin switching, closing a gap
 *    mainwin.c's own on_drag_data_received comment had flagged.
 *
 * Like gui/src/mixer_win.c, this window is created once and persists
 * for the process lifetime; "closing" it just hides it.
 */
#ifndef GUI_TOOLS_WIN_H
#define GUI_TOOLS_WIN_H

#include <gtk/gtk.h>

/* Opaque here - gui/src/tools_win.c includes the real gui/mainwin.h
 * for the full definition; kept opaque in this header to avoid a
 * circular include (gui_mainwin embeds a gui_tools_win field). */
typedef struct gui_mainwin gui_mainwin;

typedef struct gui_tools_win {
  GtkWidget* window;
  gui_mainwin* mw; /* not owned - must outlive this window */

  GtkWidget* entry_folder;
  GtkWidget* entry_vis_period;
  GtkWidget* label_skin_info;
} gui_tools_win;

void gui_tools_win_create(gui_tools_win* w, GtkWindow* parent,
                           gui_mainwin* mw);
void gui_tools_win_toggle_visible(gui_tools_win* w);
void gui_tools_win_destroy(gui_tools_win* w);

#endif /* GUI_TOOLS_WIN_H */
