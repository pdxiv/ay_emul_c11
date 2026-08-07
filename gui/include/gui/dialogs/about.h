/* C11/GTK2 port of About.pas's TAboutBox - MainWin.pas's ButAbout opens
 * this, a fully skinned window of its own (a SEPARATE embedded bitmap,
 * ay_emul/About.bmp/.bmc, and a separate irregular window shape,
 * ay_emul/rgn2.inc - both distinct from the main window's own skin/
 * rgn.inc), not the plain GtkAboutDialog previously shown here
 * (MIG-0068's own documented simplification, superseded now - MIG-0090).
 *
 * Ported:
 *  - The embedded About.bmp sprite sheet (gui/src/about_bitmap.c) and
 *    the base-image crop (343x346 from source rect (1,2)-(344,348),
 *    About.pas:FormCreate).
 *  - The irregular window shape (gui_apply_about_window_shape,
 *    gui/src/regions.c's rgn2.inc port).
 *  - Two clickable zones: OK (closes the dialog, source sub-images at
 *    (177,252)/normal and (323,252)/pushed, both 83x82, drawn at
 *    (176,250)) and the logo/"Help" zone (source sub-images at
 *    (244,2)/normal and (345,2)/pushed, both 100x142, drawn at
 *    (243,0)) - both use gui_button's existing rectangular hit-test
 *    (gui/src/zones.c), a documented simplification of the original's
 *    real rounded-region PtInRegion hit-testing (same precedent as
 *    gui_hslider_draw's own simplified thumb).
 *  - The centered "3.0" VersionString text overlay (About.pas's
 *    ButAboutClick, MainWin.pas:2574-2577 - Linux branch's Font.Height
 *    34, not Windows' 46).
 *
 * NOT ported: the logo/"Help" zone's real action (About.pas's own
 * FormMouseUp calls FrmMain.CallHelp, which opens a Windows .chm help
 * file this port doesn't have - see gui/src/mainwin.c's own comment on
 * why CallHelp itself isn't ported) - clicking it here shows an
 * informational GtkMessageDialog instead of silently doing nothing,
 * matching CallHelp's own graceful-failure behavior when the .chm file
 * is genuinely missing (`if not OpenDocument(h) then ShowMessage(...)`).
 */
#ifndef GUI_DIALOGS_ABOUT_H
#define GUI_DIALOGS_ABOUT_H

#include <gtk/gtk.h>

void gui_about_show(GtkWindow* parent);

#endif /* GUI_DIALOGS_ABOUT_H */
