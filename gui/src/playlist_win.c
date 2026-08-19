/* Must precede every #include (even our own headers, which transitively
 * pull in <stdint.h> -> <features.h> and lock in glibc's own lower
 * default first otherwise, triggering a harmless but noisy
 * "_POSIX_C_SOURCE redefined" warning). */
#define _POSIX_C_SOURCE 200809L /* for strcasecmp under strict -std=c11 */
#include "gui/playlist_win.h"

#include <gdk/gdkkeysyms.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ay_engine/psg_export.h"
#include "ay_export/vtx_export.h"
#include "ay_player/wav.h"
#include "gui/dialogs/itemedit.h"
#include "gui/dialogs/progbox.h"

enum { COL_DISPLAY = 0, COL_INDEX, NUM_COLS };

typedef struct scan_progress_ctx {
  gui_prbox* pb;
} scan_progress_ctx;

static bool on_scan_progress(int files_examined, void* userdata) {
  (void)files_examined;
  scan_progress_ctx* ctx = (scan_progress_ctx*)userdata;
  gui_prbox_pulse(ctx->pb);
  return !ctx->pb->aborted;
}

/* PlayList.pas: TimeSToStr (2402-2416)/CalculateTotalTime (3233-3283) -
 * MIG-0126. Real Pascal's own version is a lazy, on-demand calculation
 * (LTotTime starts disabled/blank until the user clicks it, forcing a
 * synchronous GetTime pass over every not-yet-probed item, showing a
 * "+" suffix if any item's time still isn't known - PollGetTimeRequest's
 * own background queue is what normally fills in the rest over time).
 * This port's own gui_playlist_add_file/add_single_song_entry already
 * probe EVERY item's duration eagerly at add-time (duration_seconds,
 * same probe_song call that already extracts author/title) - so there
 * is no "not yet known" state to represent here at all: the total is
 * always exact and immediately available, with no click-to-force-
 * calculate handler needed (LTotTimeMouseDown's own real behavior).
 * H:MM:SS if >= 1 hour, M:SS otherwise - TimeSToStr's own conditional
 * digit-dropping, just via snprintf instead of hand-rolled digit math. */
static void refresh_total_time(gui_playlist_win* w) {
  double total = 0.0;
  for (int i = 0; i < w->model.count; i++)
    total += w->model.items[i].duration_seconds;
  int secs = (int)(total + 0.5);
  char buf[32];
  if (secs >= 3600) {
    snprintf(buf, sizeof(buf), "Total: %d:%02d:%02d", secs / 3600,
              (secs / 60) % 60, secs % 60);
  } else {
    snprintf(buf, sizeof(buf), "Total: %d:%02d", secs / 60, secs % 60);
  }
  gtk_label_set_text(GTK_LABEL(w->label_total_time), buf);
}

static void refresh_view(gui_playlist_win* w) {
  gtk_list_store_clear(w->store);
  for (int i = 0; i < w->model.count; i++) {
    GtkTreeIter iter;
    gtk_list_store_append(w->store, &iter);
    gtk_list_store_set(w->store, &iter, COL_DISPLAY,
                        w->model.items[i].display, COL_INDEX, i, -1);
  }
  refresh_total_time(w);
}

/* PlayList.pas: CreatePlayOrder (485-524, MIG-0127) - only SHUFFLE mode
 * needs a persisted permutation (forward/reverse are computed
 * analytically, see play_order_position/play_order_item below); this
 * is that array's lazy (re)builder, run once per stale (w->shuffle_
 * count != model.count) use rather than at every one of CreatePlayOrder's
 * own ~10 real call sites - see gui_playlist_win's own struct comment.
 * Puts the currently-playing item first (CreatePlayOrder's own `if
 * PlayingItem >= 0 then ... PlayingOrder[0] := PlayingItem`), then
 * Fisher-Yates over the rest via rand() - this port's own established
 * shuffle convention (gui/src/playlist.c's RandomSortClick port). */
static void ensure_shuffle_order(gui_playlist_win* w) {
  int count = w->model.count;
  if (w->shuffle_order && w->shuffle_count == count) return;
  free(w->shuffle_order);
  w->shuffle_order = count > 0 ? malloc(sizeof(int) * (size_t)count) : NULL;
  w->shuffle_count = count;
  if (count == 0) return;
  for (int i = 0; i < count; i++) w->shuffle_order[i] = i;
  int start = 0;
  if (w->model.current >= 0 && w->model.current < count) {
    for (int i = 0; i < count; i++) {
      if (w->shuffle_order[i] == w->model.current) {
        int tmp = w->shuffle_order[0];
        w->shuffle_order[0] = w->shuffle_order[i];
        w->shuffle_order[i] = tmp;
        break;
      }
    }
    start = 1;
  }
  for (int i = count - 1; i > start; i--) {
    int j = start + rand() % (i - start + 1);
    int tmp = w->shuffle_order[i];
    w->shuffle_order[i] = w->shuffle_order[j];
    w->shuffle_order[j] = tmp;
  }
}

/* Item index -> its position in the current play order. -1 if not
 * found (shuffle mode only - forward/reverse always find one). */
static int play_order_position(gui_playlist_win* w, int item_index) {
  int count = w->model.count;
  switch (w->direction) {
    case GUI_PLAYLIST_DIRECTION_REVERSE:
      return count - 1 - item_index;
    case GUI_PLAYLIST_DIRECTION_SHUFFLE:
      ensure_shuffle_order(w);
      for (int i = 0; i < count; i++)
        if (w->shuffle_order[i] == item_index) return i;
      return -1;
    default:
      return item_index;
  }
}

/* Inverse of play_order_position above. */
static int play_order_item(gui_playlist_win* w, int position) {
  int count = w->model.count;
  switch (w->direction) {
    case GUI_PLAYLIST_DIRECTION_REVERSE:
      return count - 1 - position;
    case GUI_PLAYLIST_DIRECTION_SHUFFLE:
      ensure_shuffle_order(w);
      return w->shuffle_order[position];
    default:
      return position;
  }
}

static void fire_play(gui_playlist_win* w, int index) {
  if (index < 0 || index >= w->model.count) return;
  w->model.current = index;
  /* MIG-0125: model.current changing means the "currently playing" row
   * indicator (gui_playlist_win_refresh_colors's own cell-data-func,
   * keyed on this same field) needs a redraw - previously nothing at
   * all indicated the playing row in this window, so this queue_draw
   * had no visible effect to trigger until that feature existed. */
  gtk_widget_queue_draw(w->tree_view);
  if (w->on_play) {
    w->on_play(w->model.items[index].path, w->model.items[index].song_index,
               &w->model.items[index].overrides, w->userdata);
  }
}

/* PlayList.pas: RedrawItemRealy's own Selected/PlayingItem branching
 * (2526-2600, see gui/include/gui/playlist_win.h's own struct comment)
 * - GTK's own per-row equivalent, invoked automatically on every
 * redraw. `*_set` false means "leave this GdkColor* NULL", which GTK's
 * own foreground-gdk/background-gdk cell-renderer properties treat as
 * "use the theme default" (matching an unset PLColor*'s own "system
 * color" default - see this window's own gui_playlist_win_create). */
static void playlist_cell_data_func(GtkTreeViewColumn* col,
                                     GtkCellRenderer* cell,
                                     GtkTreeModel* model, GtkTreeIter* iter,
                                     gpointer data) {
  (void)col;
  gui_playlist_win* w = (gui_playlist_win*)data;
  int index;
  gtk_tree_model_get(model, iter, COL_INDEX, &index, -1);
  bool playing = (index == w->model.current);
  bool err = (index >= 0 && index < w->model.count) &&
             w->model.items[index].load_error;
  GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->tree_view));
  bool selected = gtk_tree_selection_iter_is_selected(sel, iter);

  /* Error only overrides the TEXT color (PLColorErr/PLColorErrSel) -
   * the background still follows selected/playing state regardless of
   * error, exactly matching RedrawItemRealy's own branching (2526-
   * 2600, MIG-0126: the `if Err = FileNoError` check only ever changes
   * which TxtColor is picked, never BkColor). */
  const GdkColor* fg;
  const GdkColor* bg;
  if (selected) {
    bg = w->sel_back.set ? &w->sel_back.color : NULL;
    if (err) {
      fg = w->err_sel_text.set ? &w->err_sel_text.color : NULL;
    } else {
      fg = playing ? (w->play_sel_text.set ? &w->play_sel_text.color : NULL)
                   : (w->sel_text.set ? &w->sel_text.color : NULL);
    }
  } else {
    bg = playing ? (w->play_back.set ? &w->play_back.color : NULL)
                 : (w->back.set ? &w->back.color : NULL);
    if (err) {
      fg = w->err_text.set ? &w->err_text.color : NULL;
    } else {
      fg = playing ? (w->play_text.set ? &w->play_text.color : NULL)
                   : (w->text.set ? &w->text.color : NULL);
    }
  }
  g_object_set(cell, "foreground-gdk", fg, "background-gdk", bg, NULL);
}

void gui_playlist_win_refresh_colors(gui_playlist_win* w) {
  gtk_widget_modify_font(w->tree_view, w->font);
  gtk_widget_queue_draw(w->tree_view);
}

/* PlayList.pas: SBLoopClick - `ListLooped := SBLoop.Down;`. */
static void on_loop_toggled(GtkWidget* widget, gpointer data) {
  gui_playlist_win* w = (gui_playlist_win*)data;
  gui_playlist_win_set_looped(
      w, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}

/* SBDirection's own Glyph swap (ImageList1.GetBitmap) substituted with
 * plain text, same "icon -> text label" convention this port already
 * uses elsewhere (e.g. Tools' tray-mode radio group). */
static const char* direction_label(int direction) {
  switch (direction) {
    case GUI_PLAYLIST_DIRECTION_REVERSE:
      return "Order: Reverse";
    case GUI_PLAYLIST_DIRECTION_SHUFFLE:
      return "Order: Shuffle";
    default:
      return "Order: Forward";
  }
}

/* PlayList.pas: SBDirectionClick. */
static void on_direction_clicked(GtkWidget* widget, gpointer data) {
  gui_playlist_win* w = (gui_playlist_win*)data;
  gui_playlist_win_cycle_direction(w);
  gtk_button_set_label(GTK_BUTTON(widget), direction_label(w->direction));
}

static void on_row_activated(GtkTreeView* tree_view, GtkTreePath* path,
                              GtkTreeViewColumn* column, gpointer data) {
  (void)tree_view;
  (void)column;
  gui_playlist_win* w = (gui_playlist_win*)data;
  gint* indices = gtk_tree_path_get_indices(path);
  if (indices) fire_play(w, indices[0]);
}

static void on_add_files_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win_add_files_dialog((gui_playlist_win*)data);
}

static void on_add_folder_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win_add_folder_dialog((gui_playlist_win*)data);
}

static void on_clear_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win_clear((gui_playlist_win*)data);
}

static void on_remove_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win_remove_selected((gui_playlist_win*)data);
}

static gboolean on_tree_key_press(GtkWidget* widget, GdkEventKey* event,
                                   gpointer data) {
  (void)widget;
  if (event->keyval == GDK_Delete || event->keyval == GDK_BackSpace) {
    gui_playlist_win_remove_selected((gui_playlist_win*)data);
    return TRUE;
  }
  return FALSE;
}

static void on_find_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win_show_find_dialog((gui_playlist_win*)data);
}

/* PlayList.pas: SBSaveClick (~2914-2951) - a GtkFileChooserDialog SAVE
 * with a filename-extension filter choosing .ayl vs .m3u, same as the
 * original's own FilterIndex (1=AYL, 2=M3U) SaveDialog1 - here decided
 * from which of the two filters is active when the user confirms,
 * appending the matching extension if the typed filename doesn't
 * already end in it (matching the original's own `if FName <> '.ayl'
 * then FName := ... + '.ayl'` fallback). */
static void on_save_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win* w = (gui_playlist_win*)data;
  GtkWidget* dlg = gtk_file_chooser_dialog_new(
      "Save playlist", GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_SAVE,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE,
      GTK_RESPONSE_ACCEPT, NULL);
  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);

  GtkFileFilter* filter_ayl = gtk_file_filter_new();
  gtk_file_filter_set_name(filter_ayl, "AY Emulator Playlist (*.ayl)");
  gtk_file_filter_add_pattern(filter_ayl, "*.ayl");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter_ayl);

  GtkFileFilter* filter_m3u = gtk_file_filter_new();
  gtk_file_filter_set_name(filter_m3u, "M3U Playlist (*.m3u)");
  gtk_file_filter_add_pattern(filter_m3u, "*.m3u");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter_m3u);

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char* fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    GtkFileFilter* active = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(dlg));
    bool want_m3u = (active == filter_m3u);
    const char* want_ext = want_m3u ? ".m3u" : ".ayl";
    const char* dot = strrchr(fname, '.');
    char final_name[1024];
    if (dot && strcasecmp(dot, want_ext) == 0) {
      strncpy(final_name, fname, sizeof(final_name) - 1);
      final_name[sizeof(final_name) - 1] = '\0';
    } else {
      snprintf(final_name, sizeof(final_name), "%s%s", fname, want_ext);
    }
    bool ok = want_m3u
                  ? gui_playlist_save_m3u(&w->model, final_name)
                  : gui_playlist_save_ayl(&w->model, final_name, &w->defaults);
    if (!ok) {
      GtkWidget* msg = gtk_message_dialog_new(
          GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
          GTK_BUTTONS_OK, "Could not save playlist to '%s'", final_name);
      gtk_dialog_run(GTK_DIALOG(msg));
      gtk_widget_destroy(msg);
    }
    g_free(fname);
  }
  gtk_widget_destroy(dlg);
}

/* PlayList.pas: Deduplicate1Click - reports the count via a message
 * dialog (the original just redraws silently, but this port has no
 * always-visible status bar to put that feedback in, so a small
 * message dialog stands in for it - shown only when something was
 * actually removed, to stay out of the way otherwise). */
static void on_dedup_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win* w = (gui_playlist_win*)data;
  int removed = gui_playlist_dedup(&w->model);
  refresh_view(w);
  if (removed > 0) {
    GtkWidget* msg = gtk_message_dialog_new(
        GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, "Removed %d duplicate entr%s", removed,
        removed == 1 ? "y" : "ies");
    gtk_dialog_run(GTK_DIALOG(msg));
    gtk_widget_destroy(msg);
  }
}

/* PlayList.pas: PopupMenu2's RandomSort/ByauthorSort/BytitleSort/
 * ByfilenameSort/Byfiletype1 menu items (MIG-0089) - a GtkMenu popped
 * up from a "Sort" button rather than the original's right-click
 * SBTools popup, same rationale as every other hand-built window here
 * (idiomatic GTK2, not a literal `.lfm`/menu-structure transcription). */
static void on_sort_mode_activate(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win* w = (gui_playlist_win*)data;
  gui_playlist_sort_mode mode =
      (gui_playlist_sort_mode)(intptr_t)g_object_get_data(
          G_OBJECT(widget), "sort-mode");
  gui_playlist_sort(&w->model, mode);
  refresh_view(w);
}

static void on_sort_clicked(GtkWidget* widget, gpointer data) {
  gui_playlist_win* w = (gui_playlist_win*)data;
  GtkWidget* menu = gtk_menu_new();
  static const struct {
    const char* label;
    gui_playlist_sort_mode mode;
  } items[] = {
      {"Sort by author", GUI_PLAYLIST_SORT_AUTHOR},
      {"Sort by title", GUI_PLAYLIST_SORT_TITLE},
      {"Sort by file name", GUI_PLAYLIST_SORT_FILENAME},
      {"Sort by file type", GUI_PLAYLIST_SORT_FILETYPE},
      {"Sort randomly", GUI_PLAYLIST_SORT_RANDOM},
  };
  for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
    GtkWidget* item = gtk_menu_item_new_with_label(items[i].label);
    g_object_set_data(G_OBJECT(item), "sort-mode",
                       (gpointer)(intptr_t)items[i].mode);
    g_signal_connect(item, "activate", G_CALLBACK(on_sort_mode_activate), w);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  }
  gtk_widget_show_all(menu);
  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0,
                 gtk_get_current_event_time());
  (void)widget;
}

/* PlayList.pas: MenuWAV/MenuVTX/MenuPSGClick (978-996, MIG-0128) - the
 * non-BASS subset of PlayList.pas's own "Convert" popup submenu
 * (MenuYM6/MenuZXAY are NOT ported - this port has no YM6 or .ay file
 * WRITER anywhere at all, a real new-engine-serialization gap distinct
 * from "wire up an existing exporter", see migration_debt.yaml). */
typedef enum {
  CONVERT_WAV,
  CONVERT_VTX,
  CONVERT_PSG,
} convert_format;

static uint8_t* read_whole_file_pl(const char* path, size_t* out_size) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  long size = ftell(f);
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
  uint8_t* data = (uint8_t*)malloc((size_t)size);
  if (!data) { fclose(f); return NULL; }
  size_t read = fread(data, 1, (size_t)size, f);
  fclose(f);
  if (read != (size_t)size) { free(data); return NULL; }
  *out_size = (size_t)size;
  return data;
}

/* ay_export's own make_ts_second_path (tools/ay_export/src/main.c) -
 * appends "2" right before the extension for a Turbosound pair's
 * second output file (AWAY.psg -> AWAY2.psg), same convention. */
static void make_ts_second_path(const char* path, char* out, size_t cap) {
  const char* dot = strrchr(path, '.');
  size_t base_len = dot ? (size_t)(dot - path) : strlen(path);
  const char* ext = dot ? dot : "";
  if (base_len + 1 + strlen(ext) + 1 > cap) { out[0] = '\0'; return; }
  memcpy(out, path, base_len);
  out[base_len] = '2';
  strcpy(out + base_len + 1, ext);
}

/* tools/ay_player/src/main.c's own render_to_wav, adapted for a
 * player_pair (gui_playback's own real playback primitive) rather than
 * a bare player - PLAYER_OK et al already handle a non-paired pair
 * transparently via player_pair_make_buffer. Renders to the file's own
 * natural end (player_pair_real_end_all), same "no arbitrary --seconds
 * cap" as a real file conversion (unlike ay_player's own CLI default). */
static bool export_wav(player_pair* pair, const char* path) {
  wav_writer w;
  if (!wav_writer_open(&w, path, 2, 48000, 16)) return false;
  int16_t buf[512 * 2];
  while (!player_pair_real_end_all(pair)) {
    int n = player_pair_make_buffer(pair, buf, 512);
    if (n <= 0) break;
    if (!wav_writer_write(&w, buf, n)) {
      wav_writer_close(&w);
      return false;
    }
  }
  return wav_writer_close(&w);
}

/* Builds `path` with its extension replaced by `new_ext` (e.g. ".wav")
 * for the Save dialog's own suggested filename. */
static void replace_extension(const char* path, const char* new_ext,
                               char* out, size_t cap) {
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  const char* dot = strrchr(base, '.');
  size_t base_len = dot ? (size_t)(dot - base) : strlen(base);
  snprintf(out, cap, "%.*s%s", (int)base_len, base, new_ext);
}

/* Shared body of MenuWAV/MenuVTX/MenuPSGClick - loads the selected
 * entry (as a player_pair, matching this port's own real Turbosound-
 * pairing model, MIG-0112 - a plain single-voice entry just gets
 * pair.active == false, psg_export_write_pair/vtx_export_write_pair
 * both already handle that transparently), prompts for an output path
 * via a standard Save dialog, and writes it. */
static void convert_selected(gui_playlist_win* w, convert_format fmt) {
  GtkTreeSelection* sel =
      gtk_tree_view_get_selection(GTK_TREE_VIEW(w->tree_view));
  GtkTreeModel* model;
  GtkTreeIter iter;
  if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
  gint index = -1;
  gtk_tree_model_get(model, &iter, COL_INDEX, &index, -1);
  if (index < 0 || index >= w->model.count) return;
  gui_playlist_entry* e = &w->model.items[index];

  size_t size;
  uint8_t* data = read_whole_file_pl(e->path, &size);
  if (!data) return;

  player_pair pair;
  player_status st =
      e->has_ts_pair
          ? player_pair_load_song(&pair, e->path, data, size, e->path, data,
                                   size, 48000, e->song_index, true)
          : player_pair_load_song(&pair, e->path, data, size, NULL, NULL, 0,
                                   48000, e->song_index, true);
  free(data);
  if (st != PLAYER_OK) return;

  const char* ext = fmt == CONVERT_WAV ? ".wav"
                     : fmt == CONVERT_VTX ? ".vtx"
                                          : ".psg";
  char suggested[300];
  replace_extension(e->path, ext, suggested, sizeof(suggested));

  GtkWidget* dlg = gtk_file_chooser_dialog_new(
      "Export", GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_SAVE,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE,
      GTK_RESPONSE_ACCEPT, NULL);
  gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), suggested);
  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);

  bool ok = false;
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char* out_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    const char* title = e->overrides.title[0] ? e->overrides.title : NULL;
    const char* author = e->overrides.author[0] ? e->overrides.author : NULL;
    switch (fmt) {
      case CONVERT_WAV:
        ok = export_wav(&pair, out_path);
        break;
      case CONVERT_VTX:
        ok = vtx_export_write_pair(out_path, &pair, 0, title, author, NULL,
                                    NULL, NULL, 0);
        break;
      case CONVERT_PSG: {
        char path2[1200];
        make_ts_second_path(out_path, path2, sizeof(path2));
        ok = psg_export_write_pair(out_path, path2, &pair);
        break;
      }
    }
    g_free(out_path);
  }
  gtk_widget_destroy(dlg);
  player_pair_free(&pair);

  if (!ok) {
    GtkWidget* msg = gtk_message_dialog_new(
        GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK, "Export failed.");
    gtk_dialog_run(GTK_DIALOG(msg));
    gtk_widget_destroy(msg);
  }
}

static void on_convert_mode_activate(GtkWidget* widget, gpointer data) {
  gui_playlist_win* w = (gui_playlist_win*)data;
  convert_format fmt = (convert_format)(intptr_t)g_object_get_data(
      G_OBJECT(widget), "convert-format");
  convert_selected(w, fmt);
}

/* PlayList.pas: PopupMenu1's own "Convert" submenu - see this file's
 * own on_sort_clicked for the same "GtkMenu popped from a button"
 * substitution this port already established for that analogous
 * SBTools/context-menu case (no real right-click context menu exists
 * in this window at all - matching that same precedent, not a new
 * simplification introduced here). */
static void on_convert_clicked(GtkWidget* widget, gpointer data) {
  gui_playlist_win* w = (gui_playlist_win*)data;
  GtkWidget* menu = gtk_menu_new();
  static const struct {
    const char* label;
    convert_format fmt;
  } items[] = {
      {"Export to WAV...", CONVERT_WAV},
      {"Export to VTX...", CONVERT_VTX},
      {"Export to PSG...", CONVERT_PSG},
  };
  for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
    GtkWidget* item = gtk_menu_item_new_with_label(items[i].label);
    g_object_set_data(G_OBJECT(item), "convert-format",
                       (gpointer)(intptr_t)items[i].fmt);
    g_signal_connect(item, "activate", G_CALLBACK(on_convert_mode_activate),
                      w);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  }
  gtk_widget_show_all(menu);
  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0,
                 gtk_get_current_event_time());
  (void)widget;
}

/* PlayList.pas: MenuItemAdjustingClick (~1009) - opens ItemEdit.pas on
 * the currently-selected item (MIG-0088). No-op if nothing is
 * selected, matching the original's own "if LastSelected < 0 then
 * exit"-style guard on every other selection-scoped popup command
 * here (see gui_playlist_win_remove_selected). */
static void on_adjust_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  gui_playlist_win* w = (gui_playlist_win*)data;
  GtkTreeSelection* sel =
      gtk_tree_view_get_selection(GTK_TREE_VIEW(w->tree_view));
  GtkTreeModel* model;
  GtkTreeIter iter;
  if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
  gint index = -1;
  gtk_tree_model_get(model, &iter, COL_INDEX, &index, -1);
  if (index < 0 || index >= w->model.count) return;
  gui_itemedit_show(GTK_WINDOW(w->window), &w->model.items[index],
                     &w->defaults);
  refresh_view(w);
}

static void select_and_scroll(gui_playlist_win* w, int index) {
  GtkTreePath* path = gtk_tree_path_new_from_indices(index, -1);
  GtkTreeSelection* sel =
      gtk_tree_view_get_selection(GTK_TREE_VIEW(w->tree_view));
  gtk_tree_selection_select_path(sel, path);
  gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(w->tree_view), path, NULL,
                                TRUE, 0.5, 0.0);
  gtk_tree_path_free(path);
}

void gui_playlist_win_create(gui_playlist_win* w, GtkWindow* parent,
                              gui_playlist_play_cb on_play, void* userdata) {
  memset(w, 0, sizeof(*w));
  gui_playlist_init(&w->model);
  w->on_play = on_play;
  w->userdata = userdata;
  w->find_last_index = -1;
  gui_playlist_defaults_init(&w->defaults);

  /* PlayList.pas:2748-2749 - PLColorPl/PLColorPlSel's own real FIXED
   * literal defaults ($0DA00D/$FF80FF, Delphi TColor's BGR-in-hex-
   * literal convention already converted to RGB here) - unlike every
   * other PLColor* default, which is GetSysColor(...)-derived (a
   * Windows theme value with no portable literal equivalent - left
   * unset here, see this struct's own header comment, so the
   * GtkTreeView just renders those rows with its native theme colors
   * until gui/src/mainwin.c's settings-load overrides them or the user
   * picks new ones via Tools). gdk_color_parse can't fail on a literal
   * hex string. */
  gdk_color_parse("#0DA00D", &w->play_text.color);
  w->play_text.set = true;
  gdk_color_parse("#FF80FF", &w->play_sel_text.color);
  w->play_sel_text.set = true;
  /* PlayList.pas:2754-2755 - PLColorErr/PLColorErrSel's own real FIXED
   * literal defaults ($FF/$FFFF00 -> #0000FF/#00FFFF, MIG-0126). */
  gdk_color_parse("#0000FF", &w->err_text.color);
  w->err_text.set = true;
  gdk_color_parse("#00FFFF", &w->err_sel_text.color);
  w->err_sel_text.set = true;

  w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(w->window), "Playlist");
  gtk_window_set_default_size(GTK_WINDOW(w->window), 360, 400);
  if (parent) gtk_window_set_transient_for(GTK_WINDOW(w->window), parent);
  g_signal_connect(w->window, "delete-event",
                    G_CALLBACK(gtk_widget_hide_on_delete), NULL);

  GtkWidget* vbox = gtk_vbox_new(FALSE, 4);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);
  gtk_container_add(GTK_CONTAINER(w->window), vbox);

  GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

  w->store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_INT);
  w->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->store));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w->tree_view), FALSE);
  GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
  GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
      "Track", renderer, "text", COL_DISPLAY, NULL);
  gtk_tree_view_column_set_cell_data_func(
      col, renderer, playlist_cell_data_func, w, NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(w->tree_view), col);
  g_signal_connect(w->tree_view, "row-activated",
                    G_CALLBACK(on_row_activated), w);
  g_signal_connect(w->tree_view, "key-press-event",
                    G_CALLBACK(on_tree_key_press), w);
  gtk_container_add(GTK_CONTAINER(scroll), w->tree_view);

  /* PlayList.pas: SBLoop/SBDirection (MIG-0127) - playlist-wide play
   * order, see gui/include/gui/playlist_win.h's own struct comment. */
  GtkWidget* order_hbox = gtk_hbox_new(FALSE, 4);
  w->check_loop = gtk_check_button_new_with_label("Loop playlist");
  g_signal_connect(w->check_loop, "toggled", G_CALLBACK(on_loop_toggled), w);
  gtk_box_pack_start(GTK_BOX(order_hbox), w->check_loop, FALSE, FALSE, 0);
  w->button_direction =
      gtk_button_new_with_label(direction_label(w->direction));
  g_signal_connect(w->button_direction, "clicked",
                    G_CALLBACK(on_direction_clicked), w);
  gtk_box_pack_start(GTK_BOX(order_hbox), w->button_direction, FALSE, FALSE,
                      0);
  gtk_box_pack_start(GTK_BOX(vbox), order_hbox, FALSE, FALSE, 0);

  /* PlayList.pas: LTotTime (MIG-0126) - see refresh_total_time's own
   * comment for why no click handler is needed here. */
  w->label_total_time = gtk_label_new("Total: 0:00");
  gtk_misc_set_alignment(GTK_MISC(w->label_total_time), 1.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vbox), w->label_total_time, FALSE, FALSE, 0);

  GtkWidget* hbox = gtk_hbox_new(TRUE, 4);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  GtkWidget* add_files_btn = gtk_button_new_with_label("Add Files");
  g_signal_connect(add_files_btn, "clicked",
                    G_CALLBACK(on_add_files_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), add_files_btn, TRUE, TRUE, 0);

  GtkWidget* add_folder_btn = gtk_button_new_with_label("Add Folder");
  g_signal_connect(add_folder_btn, "clicked",
                    G_CALLBACK(on_add_folder_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), add_folder_btn, TRUE, TRUE, 0);

  GtkWidget* remove_btn = gtk_button_new_with_label("Remove");
  g_signal_connect(remove_btn, "clicked", G_CALLBACK(on_remove_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), remove_btn, TRUE, TRUE, 0);

  GtkWidget* clear_btn = gtk_button_new_with_label("Clear");
  g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), clear_btn, TRUE, TRUE, 0);

  GtkWidget* find_btn = gtk_button_new_with_label("Find");
  g_signal_connect(find_btn, "clicked", G_CALLBACK(on_find_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), find_btn, TRUE, TRUE, 0);

  GtkWidget* adjust_btn = gtk_button_new_with_label("Adjust...");
  g_signal_connect(adjust_btn, "clicked", G_CALLBACK(on_adjust_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), adjust_btn, TRUE, TRUE, 0);

  GtkWidget* sort_btn = gtk_button_new_with_label("Sort...");
  g_signal_connect(sort_btn, "clicked", G_CALLBACK(on_sort_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), sort_btn, TRUE, TRUE, 0);

  GtkWidget* dedup_btn = gtk_button_new_with_label("Dedup");
  g_signal_connect(dedup_btn, "clicked", G_CALLBACK(on_dedup_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), dedup_btn, TRUE, TRUE, 0);

  GtkWidget* convert_btn = gtk_button_new_with_label("Convert...");
  g_signal_connect(convert_btn, "clicked", G_CALLBACK(on_convert_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), convert_btn, TRUE, TRUE, 0);

  GtkWidget* save_btn = gtk_button_new_with_label("Save...");
  g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), w);
  gtk_box_pack_start(GTK_BOX(hbox), save_btn, TRUE, TRUE, 0);

  gui_playlist_win_refresh_colors(w);
}

void gui_playlist_win_toggle_visible(gui_playlist_win* w) {
  if (gtk_widget_get_visible(w->window)) {
    gtk_widget_hide(w->window);
  } else {
    gtk_widget_show_all(w->window);
  }
}

/* PlayList.pas: Add_File's extension dispatch (~1780-1792) - a chosen
 * path ending in .ayl/.m3u/.m3u8 is a PLAYLIST file (its own entries
 * get added, via gui_playlist_load_ayl/load_m3u), not itself a
 * playable chiptune, matching the original's own FTS='AYL'/FTS='M3U'
 * branches (see gui_playlist_load_ayl/gui_playlist_load_m3u's own
 * header comments for what's ported of each - .cue is NOT dispatched
 * here, see migration_debt.yaml). Every other extension goes through
 * the normal real-player-probe add path unchanged. Returns the number
 * of entries added. */
static int add_any(gui_playlist_win* w, const char* path) {
  const char* dot = strrchr(path, '.');
  if (dot) {
    if (strcasecmp(dot, ".ayl") == 0)
      return gui_playlist_load_ayl(&w->model, path, &w->defaults);
    if (strcasecmp(dot, ".m3u") == 0 || strcasecmp(dot, ".m3u8") == 0)
      return gui_playlist_load_m3u(&w->model, path);
  }
  return gui_playlist_add_file(&w->model, path);
}

void gui_playlist_win_replace_with_path(gui_playlist_win* w,
                                         const char* path) {
  gui_playlist_clear(&w->model);
  int added = add_any(w, path);
  refresh_view(w);
  if (added > 0) fire_play(w, 0);
}

void gui_playlist_win_add_path(gui_playlist_win* w, const char* path) {
  add_any(w, path);
  refresh_view(w);
}

void gui_playlist_win_add_files_dialog(gui_playlist_win* w) {
  GtkWidget* dlg = gtk_file_chooser_dialog_new(
      "Add files to playlist", GTK_WINDOW(w->window),
      GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT, NULL);
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    GSList* files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
    for (GSList* it = files; it; it = it->next) {
      add_any(w, (const char*)it->data);
      g_free(it->data);
    }
    g_slist_free(files);
    refresh_view(w);
  }
  gtk_widget_destroy(dlg);
}

void gui_playlist_win_add_folder_dialog(gui_playlist_win* w) {
  GtkWidget* dlg = gtk_file_chooser_dialog_new(
      "Add folder to playlist", GTK_WINDOW(w->window),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT, NULL);

  /* seldir.pas: CBRecurse ("Recurse into subdirectories" - one of
   * ChooseDirectory's options, the only one with a direct equivalent in
   * this port; see gui_playlist_add_directory's own comment for why
   * the others - do-detect/playlist-inclusion-mode/path-to-name - are
   * not ported). Defaults checked, matching AddFolderRecurseDirs's
   * real default. */
  GtkWidget* recurse_check =
      gtk_check_button_new_with_label("Recurse into subdirectories");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(recurse_check), TRUE);
  gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dlg), recurse_check);
  gtk_widget_show(recurse_check);

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char* dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    bool recurse =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(recurse_check));

    gui_prbox pb;
    gui_prbox_create(&pb, GTK_WINDOW(w->window), "Searching for tunes...");

    /* on_scan_progress pulses ProgBox and checks its Abort button (see
     * gui/dialogs/progbox.h) on every file gui_playlist_add_directory
     * examines. */
    scan_progress_ctx ctx = {&pb};
    gui_playlist_add_directory(&w->model, dir, recurse, on_scan_progress,
                                &ctx);

    gui_prbox_destroy(&pb);
    g_free(dir);
    refresh_view(w);
  }
  gtk_widget_destroy(dlg);
}

void gui_playlist_win_clear(gui_playlist_win* w) {
  gui_playlist_clear(&w->model);
  refresh_view(w);
}

void gui_playlist_win_remove_selected(gui_playlist_win* w) {
  GtkTreeSelection* sel =
      gtk_tree_view_get_selection(GTK_TREE_VIEW(w->tree_view));
  GtkTreeModel* model;
  GtkTreeIter iter;
  if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

  gint index = -1;
  gtk_tree_model_get(model, &iter, COL_INDEX, &index, -1);
  gui_playlist_remove(&w->model, index);
  refresh_view(w);
}

/* PlayList.pas: PlayNextItem (863-872) - walks the CURRENT play order
 * (MIG-0127), not raw model.current +/- 1, wrapping to the start if
 * `looped` (ListLooped) is set instead of stopping at the boundary. */
bool gui_playlist_win_next(gui_playlist_win* w) {
  if (w->model.count == 0) return false;
  int pos = (w->model.current >= 0)
                ? play_order_position(w, w->model.current)
                : -1;
  int next_pos = pos + 1;
  if (next_pos >= w->model.count) {
    if (!w->looped) return false;
    next_pos = 0;
  }
  fire_play(w, play_order_item(w, next_pos));
  return true;
}

/* PlayList.pas: PlayPreviousItem (874-883) - same shape as
 * gui_playlist_win_next above. */
bool gui_playlist_win_prev(gui_playlist_win* w) {
  if (w->model.count == 0) return false;
  int pos = (w->model.current >= 0)
                ? play_order_position(w, w->model.current)
                : 0;
  int prev_pos = pos - 1;
  if (prev_pos < 0) {
    if (!w->looped) return false;
    prev_pos = w->model.count - 1;
  }
  fire_play(w, play_order_item(w, prev_pos));
  return true;
}

/* PlayList.pas: SBDirectionClick (3417-3424) - `(Direction + 1) and 3`
 * collapsed to the 3 real modes (see gui/include/gui/playlist_win.h's
 * own enum comment). Invalidates shuffle_order so the next Next/Prev
 * rebuilds it fresh, matching SetDirection's own CreatePlayOrder call
 * right after changing Direction. */
void gui_playlist_win_cycle_direction(gui_playlist_win* w) {
  w->direction = (w->direction + 1) % 3;
  w->shuffle_count = -1; /* forces ensure_shuffle_order to rebuild */
}

void gui_playlist_win_set_looped(gui_playlist_win* w, bool looped) {
  w->looped = looped;
}

/* FindPLItem.pas: Button1Click (Find Next) - search from LastSelected+1
 * to the end, then wrap 0..LastSelected if nothing was found forward. */
static void find_next(gui_playlist_win* w, const char* needle,
                       gui_playlist_find_mode mode) {
  int found = gui_playlist_find(&w->model, w->find_last_index + 1, mode,
                                 needle);
  if (found < 0 && w->find_last_index >= 0) {
    int wrapped = gui_playlist_find(&w->model, 0, mode, needle);
    if (wrapped <= w->find_last_index) found = wrapped;
  }
  if (found < 0) {
    GtkWidget* msg = gtk_message_dialog_new(
        GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, "Search string not found");
    gtk_dialog_run(GTK_DIALOG(msg));
    gtk_widget_destroy(msg);
    return;
  }
  w->find_last_index = found;
  select_and_scroll(w, found);
}

/* FindPLItem.pas: Button2Click (Find All) - counts and selects every
 * match; GtkTreeView's single-selection mode means only the last match
 * ends up visibly selected+scrolled-to (see this function's own header
 * comment in playlist_win.h for why that's a documented, not silent,
 * narrowing). */
static void find_all(gui_playlist_win* w, const char* needle,
                      gui_playlist_find_mode mode) {
  int count = 0;
  int last = -1;
  for (int i = gui_playlist_find(&w->model, 0, mode, needle); i >= 0;
       i = gui_playlist_find(&w->model, i + 1, mode, needle)) {
    count++;
    last = i;
  }
  if (count == 0) {
    GtkWidget* msg = gtk_message_dialog_new(
        GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK, "Search string not found");
    gtk_dialog_run(GTK_DIALOG(msg));
    gtk_widget_destroy(msg);
    return;
  }
  w->find_last_index = last;
  select_and_scroll(w, last);
  GtkWidget* msg = gtk_message_dialog_new(
      GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
      GTK_BUTTONS_OK, "%d match%s found", count, count == 1 ? "" : "es");
  gtk_dialog_run(GTK_DIALOG(msg));
  gtk_widget_destroy(msg);
}

typedef struct find_dialog_ctx {
  gui_playlist_win* w;
  GtkWidget* entry;
  GtkWidget* rb_author;
  GtkWidget* rb_title;
  GtkWidget* rb_filename;
} find_dialog_ctx;

static gui_playlist_find_mode selected_find_mode(find_dialog_ctx* ctx) {
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_author)))
    return GUI_PLAYLIST_FIND_AUTHOR;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_title)))
    return GUI_PLAYLIST_FIND_TITLE;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ctx->rb_filename)))
    return GUI_PLAYLIST_FIND_FILENAME;
  return GUI_PLAYLIST_FIND_ANYWHERE;
}

static void on_find_next_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  find_dialog_ctx* ctx = (find_dialog_ctx*)data;
  find_next(ctx->w, gtk_entry_get_text(GTK_ENTRY(ctx->entry)),
            selected_find_mode(ctx));
}

static void on_find_all_clicked(GtkWidget* widget, gpointer data) {
  (void)widget;
  find_dialog_ctx* ctx = (find_dialog_ctx*)data;
  find_all(ctx->w, gtk_entry_get_text(GTK_ENTRY(ctx->entry)),
           selected_find_mode(ctx));
}

void gui_playlist_win_show_find_dialog(gui_playlist_win* w) {
  GtkWidget* dlg = gtk_dialog_new_with_buttons(
      "Find playlist item", GTK_WINDOW(w->window),
      GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
      NULL);

  GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
  GtkWidget* vbox = gtk_vbox_new(FALSE, 6);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
  gtk_container_add(GTK_CONTAINER(content), vbox);

  GtkWidget* search_frame = gtk_frame_new("Search string");
  GtkWidget* entry = gtk_entry_new();
  GtkWidget* entry_align = gtk_alignment_new(0.5, 0.5, 1.0, 1.0);
  gtk_alignment_set_padding(GTK_ALIGNMENT(entry_align), 4, 4, 4, 4);
  gtk_container_add(GTK_CONTAINER(entry_align), entry);
  gtk_container_add(GTK_CONTAINER(search_frame), entry_align);
  gtk_box_pack_start(GTK_BOX(vbox), search_frame, FALSE, FALSE, 0);

  /* FindPLItem.pas: FormCreate's RadioGroup1.Items - "Anywhere"/
   * "Author name"/"Music title"/"File name", in that order (matching
   * gui_playlist_find_mode's own enum values 0-3). */
  GtkWidget* area_frame = gtk_frame_new("Search area");
  GtkWidget* radio_vbox = gtk_vbox_new(TRUE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(radio_vbox), 4);
  gtk_container_add(GTK_CONTAINER(area_frame), radio_vbox);
  GtkWidget* rb_anywhere = gtk_radio_button_new_with_label(NULL, "Anywhere");
  GtkWidget* rb_author = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(rb_anywhere), "Author name");
  GtkWidget* rb_title = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(rb_anywhere), "Music title");
  GtkWidget* rb_filename = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(rb_anywhere), "File name");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb_anywhere), TRUE);
  gtk_box_pack_start(GTK_BOX(radio_vbox), rb_anywhere, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(radio_vbox), rb_author, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(radio_vbox), rb_title, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(radio_vbox), rb_filename, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), area_frame, FALSE, FALSE, 0);

  GtkWidget* btn_hbox = gtk_hbox_new(TRUE, 4);
  GtkWidget* find_next_btn = gtk_button_new_with_label("Find next");
  GtkWidget* find_all_btn = gtk_button_new_with_label("Find all");
  gtk_box_pack_start(GTK_BOX(btn_hbox), find_next_btn, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(btn_hbox), find_all_btn, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), btn_hbox, FALSE, FALSE, 0);

  find_dialog_ctx ctx = {w, entry, rb_author, rb_title, rb_filename};
  g_signal_connect(find_next_btn, "clicked", G_CALLBACK(on_find_next_clicked),
                    &ctx);
  g_signal_connect(find_all_btn, "clicked", G_CALLBACK(on_find_all_clicked),
                    &ctx);

  gtk_widget_show_all(dlg);
  gtk_dialog_run(GTK_DIALOG(dlg)); /* only GTK_STOCK_CLOSE has a response
                                     * id, so this returns on Close or
                                     * window-close - Find Next/Find All
                                     * are handled entirely by their own
                                     * signal handlers above and don't
                                     * end the dialog */
  gtk_widget_destroy(dlg);
}

void gui_playlist_win_destroy(gui_playlist_win* w) {
  gui_playlist_free(&w->model);
  if (w->font) pango_font_description_free(w->font);
  free(w->shuffle_order);
  gtk_widget_destroy(w->window);
}
