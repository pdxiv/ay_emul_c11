/* C11/GTK2 port of PlayList.pas's TFrmPLst window - scoped to what
 * gui/include/gui/playlist.h's data model covers (see its own file
 * comment for what's NOT carried over). Deliberately NOT built via
 * tools/lfm_gen/lfm_gen.py or GtkFixed: PlayList.pas was already scoped
 * as hand-design work from the start (it isn't skin-rendered, unlike
 * MainWin), so this uses idiomatic GTK2 widgets - GtkTreeView/
 * GtkListStore for the list, GtkVBox/GtkHBox for layout, standard
 * GtkFileChooserDialog for add-files/add-folder - rather than literal
 * pixel coordinates from PlayList.lfm.
 *
 * "Close" (the window-manager X / delete-event) hides rather than
 * destroys, matching MainWin.pas's ButList toggle semantics (Is_On):
 * the playlist and its window persist for the process lifetime, only
 * visibility toggles.
 */
#ifndef GUI_PLAYLIST_WIN_H
#define GUI_PLAYLIST_WIN_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "gui/playlist.h"

/* Called whenever the playlist window wants a specific entry played
 * (row double-click, Next/Prev, or a fresh Open/drag-drop replacing the
 * whole list) - the caller (gui/src/mainwin.c) owns the actual
 * gui_playback and does the real load/play call. */
typedef void (*gui_playlist_play_cb)(const char* path, int song_index,
                                      const gui_playlist_overrides* overrides,
                                      void* userdata);

/* One PLColor* slot (MIG-0125) - `set` false means "unset, use the
 * GtkTreeView's native theme color" (see gui_playlist_win's own struct
 * comment). Named separately (not an anonymous struct) so gui/src/
 * tools_win.c's own color-picker handlers can take a `gui_playlist_
 * color*` generically instead of one hand-written handler per field. */
typedef struct gui_playlist_color {
  GdkColor color;
  bool set;
} gui_playlist_color;

typedef struct gui_playlist_win {
  GtkWidget* window;
  GtkWidget* tree_view;
  GtkListStore* store;
  gui_playlist model;
  gui_playlist_play_cb on_play;
  void* userdata;
  int find_last_index; /* FindPLItem.pas: LastSelected - persists across
                         * find-dialog invocations, -1 initially */

  GtkWidget* label_total_time; /* PlayList.pas: LTotTime (MIG-0126) -
                                 * see gui/src/playlist_win.c's own
                                 * refresh_total_time comment for why
                                 * this port needs neither
                                 * CalculateTotalTime's Force-recalc
                                 * click handler nor its "+" partial-
                                 * total indicator. */

  /* PlayList.pas: PLDef_* playlist-wide fallback overrides (see gui/
   * include/gui/playlist.h's gui_playlist_defaults comment) - this
   * window is the natural home for them since, like PlayListItems
   * itself, they're global playlist-scoped state, not per-item. Session-
   * lifetime only (see gui_playlist_defaults's own comment on why no
   * file persistence). Initialized by gui_playlist_defaults_init in
   * gui_playlist_win_create, not a plain memset (0 isn't every field's
   * real "unset" sentinel). */
  gui_playlist_defaults defaults;

  /* Tools.pas: PListOpts tab's "Playlist colors and font" GroupBox
   * (MIG-0125) - PLColor/PLColorBk (normal text/back), PLColorSel/
   * PLColorBkSel (selected), PLColorPl/PLColorBkPl (currently-playing),
   * PLColorPlSel (playing AND selected - reuses PLColorBkSel for its
   * background, matching PlayList.pas's own RedrawItemRealy). `set`
   * flags whether a color was ever explicitly picked (via Tools' own
   * color-picker labels) or loaded from settings.ini - false means
   * "let the GtkTreeView render with its native theme colors", the
   * closest faithful equivalent to real Pascal's own GetSysColor(...)
   * defaults for PLColorBk/BkSel/Sel/PLColor (a Windows theme value
   * this port has no portable literal equivalent for - see gui/src/
   * tools_win.c's own comment). PLColorPl/PLColorPlSel DO have real
   * fixed-literal Pascal defaults (not system-derived) and are
   * pre-set accordingly in gui_playlist_win_create, `set=true` from
   * the start, unlike the other five. PLColorErr/PLColorErrSel (MIG-
   * 0126) round out the full 9-color set now that gui_playlist_entry::
   * load_error (gui/include/gui/playlist.h) gives them something real
   * to key off of - both fixed-literal Pascal defaults ($FF/$FFFF00),
   * pre-set `set=true` same as PLColorPl/PLColorPlSel. */
  gui_playlist_color text, back, sel_text, sel_back, play_text, play_back,
      play_sel_text, err_text, err_sel_text;

  /* Tools.pas: PLArea.Font (Name/Size/Bold/Italic, saved as 4 separate
   * keys) - consolidated into a single PangoFontDescription string here
   * (GTK/Pango's own idiomatic representation, e.g. "Sans Bold 10"),
   * matching this session's own established "platform-appropriate
   * substitution over literal structural mimicry" precedent (see e.g.
   * gui/include/gui/alsa_mixer.h's own file comment) rather than
   * reproducing 4 separate Name/Size/Bold/Italic fields. NULL means
   * "use the GtkTreeView's native/theme font", same "not customized
   * yet" convention as the colors above. */
  PangoFontDescription* font;

  /* PlayList.pas: SetDirection/CreatePlayOrder/SBLoop (MIG-0127) - the
   * playlist-WIDE play order Next/Prev walk, separate from MainWin.
   * pas's own Do_Loop (a same-song repeat, already ported). Real
   * Pascal cycles 4 button-states (0..3) but only 3 distinct
   * BEHAVIORS exist in CreatePlayOrder's own body (Direction=0:
   * reverse; Direction<>2, i.e. 1 or 3: forward; Direction=2: shuffle)
   * - collapsed to exactly those 3 real modes here (see the
   * GUI_PLAYLIST_DIRECTION_* enum below), a documented simplification
   * of the cosmetic 4th icon state, not a behavioral one. `looped`
   * (ListLooped/SBLoop.Down) wraps Next/Prev at the list boundary
   * instead of stopping there (PlayNextItem/PlayPreviousItem's own
   * `if Tmp >= Length(...) then if ListLooped ... Tmp := 0`) -
   * persisted via settings.ini's "PlayListLoop" key (MainWin.pas:4386/
   * 4621-4622), matching real Pascal; `direction` is session-only,
   * matching real Pascal's own apparent non-persistence of Direction
   * itself. */
  int direction;
  bool looped;
  int* shuffle_order; /* Only allocated/meaningful in GUI_PLAYLIST_
                        * DIRECTION_SHUFFLE - forward/reverse order is
                        * cheap to compute analytically from `index`/
                        * count each time (see gui/src/playlist_win.c's
                        * own play_order_position/play_order_index), so
                        * only shuffle needs a persisted permutation
                        * array (must stay STABLE across repeated Next/
                        * Prev calls, unlike a fresh reverse/forward
                        * computation which is trivially always
                        * correct). Rebuilt lazily (shuffle_count !=
                        * model.count) rather than at every one of
                        * CreatePlayOrder's ~10 real call sites (add/
                        * remove/sort/etc.) - a platform-appropriate
                        * simplification: always correct by the time
                        * it's actually used, just computed on demand
                        * instead of eagerly kept in sync. */
  int shuffle_count;

  GtkWidget* check_loop;   /* Tools.pas/PlayList.pas: SBLoop - exposed so
                             * gui/src/mainwin.c's settings-load can sync
                             * its visual state to a persisted `looped`
                             * value (gui_playlist_win_set_looped itself
                             * only touches the plain bool, matching
                             * every other lazily-read checkbox in this
                             * port - see gui_tools_win_force_loop's own
                             * precedent). */
  GtkWidget* button_direction; /* PlayList.pas: SBDirection - label text
                                 * is the icon substitute (see gui/src/
                                 * playlist_win.c's own direction_label). */
} gui_playlist_win;

/* PlayList.pas: SetDirection's own Direction values, collapsed to the
 * 3 real CreatePlayOrder behaviors - see gui_playlist_win's own struct
 * comment. */
enum {
  GUI_PLAYLIST_DIRECTION_FORWARD = 0,
  GUI_PLAYLIST_DIRECTION_REVERSE = 1,
  GUI_PLAYLIST_DIRECTION_SHUFFLE = 2,
};

/* Cycles Forward -> Reverse -> Shuffle -> Forward (SBDirectionClick's
 * own `(Direction + 1) and 3` collapsed to the 3 real modes) and
 * invalidates any cached shuffle_order so the next Next/Prev rebuilds
 * it fresh for the new mode. */
void gui_playlist_win_cycle_direction(gui_playlist_win* w);

/* Tools.pas/MainWin.pas: SBLoopClick - `ListLooped := SBLoop.Down;`. */
void gui_playlist_win_set_looped(gui_playlist_win* w, bool looped);

/* Re-applies text/back/sel_text/sel_back/play_text/play_back/
 * play_sel_text/font to the tree view (a custom GtkCellRenderer
 * "foreground-gdk"/"background-gdk" cell-data-func keyed on each row's
 * selected/playing state, matching PlayList.pas's own RedrawItemRealy
 * branching - plus gtk_widget_modify_font for the font) and queues a
 * redraw. Called once at gui_playlist_win_create, and again by gui/src/
 * tools_win.c's own PListOpts color-picker/font-picker handlers after
 * each change. */
void gui_playlist_win_refresh_colors(gui_playlist_win* w);

void gui_playlist_win_create(gui_playlist_win* w, GtkWindow* parent,
                              gui_playlist_play_cb on_play, void* userdata);
void gui_playlist_win_toggle_visible(gui_playlist_win* w);

/* Clears the playlist, adds `path` (expanding multi-song .ay files into
 * one entry per subsong, same as gui_playlist_add_file), refreshes the
 * view, and fires on_play for entry 0 if anything was added. Used by
 * MainWin.pas-equivalent Open and drag-and-drop, matching
 * FormDropFiles's real "not a skin drop -> StopAndFreeAll + ClearPlayList"
 * behavior (see MIG-0070's rationale for what's still simplified). */
void gui_playlist_win_replace_with_path(gui_playlist_win* w,
                                         const char* path);

/* Adds (does not clear) `path`, refreshes the view. Does not change
 * what's currently playing. */
void gui_playlist_win_add_path(gui_playlist_win* w, const char* path);

/* Opens a multi-select GtkFileChooserDialog and adds every chosen file
 * (appending, not replacing - PlayList.pas's SBAdd behavior). */
void gui_playlist_win_add_files_dialog(gui_playlist_win* w);

/* Opens a folder-select GtkFileChooserDialog, then recursively scans it
 * (via gui_playlist_add_directory), showing gui/dialogs/progbox.h's
 * ProgBox dialog for the duration with a working Abort button. */
void gui_playlist_win_add_folder_dialog(gui_playlist_win* w);

void gui_playlist_win_clear(gui_playlist_win* w);

/* PlayList.pas: DeletePlayListItem, reached from the real UI via a
 * popup-menu item and the Delete key (PLAreaKeyDown) - both wired here
 * (a "Remove Selected" button, and Delete/BackSpace on the tree view).
 * No-op if nothing is selected. If the removed entry was the one
 * currently loaded, playback is left running (the model's `current`
 * just becomes -1, matching gui_playlist_remove) - matching the
 * original's own behavior of not forcibly stopping playback just
 * because the item was removed from the list. */
void gui_playlist_win_remove_selected(gui_playlist_win* w);

/* FindPLItem.pas: TFrmFndPLItm - a small modal dialog (search text,
 * Anywhere/Author/Title/Filename radio buttons, Find Next / Find All /
 * Close) built directly with idiomatic GTK2 widgets, same as the rest
 * of this window (not generated from FindPLItem.lfm - see
 * gui_playlist_find's own comment on what's narrowed from the real
 * FindItem2/FindItem). "Find Next" selects+scrolls to the next match
 * after the currently-selected row, wrapping around; "Find All"
 * selects every match it can (GtkTreeView single-selection-mode means
 * only the LAST match ends up visibly selected - a real, documented
 * narrowing of the original's true multi-select highlight - but every
 * match is still counted and reported, matching FindItem2's "how many
 * found" semantics as closely as this window's selection model allows). */
void gui_playlist_win_show_find_dialog(gui_playlist_win* w);

/* PlayList.pas: PlayNextItem/PlayPreviousItem (MIG-0127) - moves one
 * step through the CURRENT play order (`direction`: forward/reverse/
 * shuffle), wrapping at the boundary if `looped` else stopping there,
 * and fires on_play if it moved. Returns false only when nothing
 * happened (empty list, or at a boundary with `looped` false). */
bool gui_playlist_win_next(gui_playlist_win* w);
bool gui_playlist_win_prev(gui_playlist_win* w);

void gui_playlist_win_destroy(gui_playlist_win* w);

#endif /* GUI_PLAYLIST_WIN_H */
