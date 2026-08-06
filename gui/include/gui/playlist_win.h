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

typedef struct gui_playlist_win {
  GtkWidget* window;
  GtkWidget* tree_view;
  GtkListStore* store;
  gui_playlist model;
  gui_playlist_play_cb on_play;
  void* userdata;
  int find_last_index; /* FindPLItem.pas: LastSelected - persists across
                         * find-dialog invocations, -1 initially */
} gui_playlist_win;

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

/* Moves model.current by +1/-1 (clamped, no wraparound - matches
 * PlayList.pas's PlayNextItem/PlayPreviousItem at the list boundaries)
 * and fires on_play if it moved. Returns false if already at the
 * boundary (nothing to do). */
bool gui_playlist_win_next(gui_playlist_win* w);
bool gui_playlist_win_prev(gui_playlist_win* w);

void gui_playlist_win_destroy(gui_playlist_win* w);

#endif /* GUI_PLAYLIST_WIN_H */
