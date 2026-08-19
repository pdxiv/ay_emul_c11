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
 *  - CBForceLoop (Force_Loop) IS now ported (MIG-0114, closing this
 *    entry's own prior exclusion - TSMode/Turbosound pairing itself
 *    landed later, MIG-0109/MIG-0112, making this genuinely reachable):
 *    lets a mismatched-length Turbosound pair's SHORTER voice keep
 *    audibly looping its own pattern data past its own natural end
 *    instead of freezing, while the longer voice keeps playing - see
 *    check_force_loop below and player_pair_set_force_loop
 *    (engine/include/ay_engine/player.h).
 *  - CBDoubleSz (window Scale 1x/2x) IS now ported (MIG-0119, closing
 *    this entry's own prior exclusion): see check_double_sz below and
 *    gui_mainwin_set_scale (gui/include/gui/mainwin.h) for the full
 *    picture - resizes the actual window/drawing-area, rescales
 *    drawing via cairo_scale, rescales the window-shape region, and
 *    converts incoming mouse coordinates back to skin-space for hit-
 *    testing. The skin bitmap itself is never pre-scaled (matching the
 *    original: Canvas.CopyRect stretches the SAME 1x bitmap into a
 *    Scale-multiplied destination rect).
 *  - SearchTool (recursive tracker-file scan with per-format
 *    checkboxes) - functionally superseded by the playlist's own
 *    "Add Folder" (gui_playlist_add_directory, MIG-0071/0085), which
 *    already scans and adds every recognized format; a second,
 *    separate scan-only tool would be redundant.
 *  - Mixer.lfm's VolumeSheet tab (BVolCtrlSelect/BVolCtrlDetect/
 *    EVolCtrl/CBLnScale/CBSvVolPos) previously lived here (a session-
 *    specific file-ownership split, explicitly flagged at the time as
 *    "not a claim that this is where the real Pascal form lives") -
 *    MOVED to gui/src/mixer_win.c's own new "Volume" notebook tab
 *    (MIG-0129), restoring the real tab structure (VolumeSheet is a
 *    Mixer.pas tab, not a Tools.pas one). See gui/include/gui/
 *    mixer_win.h for the current home.
 *
 * MIG-0129 update: this window itself has NO real tab structure to
 * restore (Tools.pas's own 5 tabs are almost entirely out of scope -
 * only GenTools' portable subset and PListOpts survive at all, see
 * above - so there's no second in-scope tab to split this into the
 * way Mixer's AY-Emulation/Volume split was). Everything below stays
 * in one flat vbox.
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
 *  - Tools.pas's CheckBox40/AutoSaveWindowsPos ("Save windows
 *    position") and the "Icon on system tray" radio group
 *    (RadioButton8/9/10, TrayMode) - both gui/include/gui/settings.h-
 *    backed.
 *
 * Like gui/src/mixer_win.c, this window is created once and persists
 * for the process lifetime; "closing" it just hides it.
 */
#ifndef GUI_TOOLS_WIN_H
#define GUI_TOOLS_WIN_H

#include <stdbool.h>

#include <gtk/gtk.h>

#include "gui/playlist_win.h" /* gui_playlist_color, MIG-0125 */

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
  GtkWidget* check_force_loop; /* Tools.pas: CBForceLoop - MIG-0114 */
  GtkWidget* check_double_sz;  /* Tools.pas: CBDoubleSz - MIG-0119 */

  /* Tools.pas: CheckBox40 - read via accessor at save time (no live
   * side effect needed, matching AutoSaveWindowsPos's own role: only
   * gates what a FUTURE save writes). */
  GtkWidget* check_auto_save_windows_pos;

  /* Tools.pas: "Icon on system tray" radio group (RadioButton8/9/10) -
   * unlike the above, this has an immediate effect (gui_mainwin_set_
   * tray_mode) so its "toggled" handlers call into mainwin.c directly
   * rather than being read lazily. */
  GtkWidget* radio_tray_never;
  GtkWidget* radio_tray_minimize;
  GtkWidget* radio_tray_always;

  /* Tools.pas: PListOpts tab's "Playlist colors and font" GroupBox
   * (Label1/12/13/14/15/16/17 - MIG-0125) - each a clickable preview
   * swatch (GtkEventBox+GtkLabel, styled from mw->plwin's own current
   * text/back pair, matching Label1.Color:=PLColorBk;Label1.Font.
   * Color:=PLColor's own "both labels preview the SAME pair, click
   * either to change ITS OWN one property" real behavior) opening a
   * GtkColorSelectionDialog on click. See gui/src/playlist_win.h's own
   * gui_playlist_color for the field these write into. */
  GtkWidget* pl_swatch_text, *pl_swatch_back;
  GtkWidget* pl_swatch_sel_text, *pl_swatch_sel_back;
  GtkWidget* pl_swatch_play_text, *pl_swatch_play_back;
  GtkWidget* pl_swatch_play_sel;
  GtkWidget* pl_swatch_err, *pl_swatch_err_sel; /* MIG-0126 */
} gui_tools_win;

void gui_tools_win_create(gui_tools_win* w, GtkWindow* parent,
                           gui_mainwin* mw);
void gui_tools_win_toggle_visible(gui_tools_win* w);
void gui_tools_win_destroy(gui_tools_win* w);

/* MIG-0114: current state of the Force_Loop checkbox - read by
 * gui/src/mainwin.c's do_load_song right after a (possibly paired)
 * load, the same pattern gui_mixer_win's own use_*_list checkboxes
 * already use (see apply_item_overrides) rather than needing this
 * window to "push" its state into every fresh player_pair itself. */
bool gui_tools_win_force_loop(const gui_tools_win* w);

/* Tools.pas: CheckBox40/AutoSaveWindowsPos - current checkbox state,
 * read by gui_mainwin_save_settings at save time (same lazily-read
 * pattern this port already established for e.g. Force_Loop above - a
 * live field push isn't needed since this checkbox has no immediate
 * effect of its own, only a future-save one). */
bool gui_tools_win_auto_save_windows_pos(const gui_tools_win* w);

#endif /* GUI_TOOLS_WIN_H */
