#include <gtk/gtk.h>

#include "gui/ipc.h"
#include "gui/mainwin.h"

static void on_ipc_file(const char* path, void* userdata) {
  gui_mainwin* mw = (gui_mainwin*)userdata;
  gui_playlist_win_add_path(&mw->plwin, path);
  if (mw->plwin.model.current < 0) {
    /* Nothing was playing yet in this already-running instance - start
     * the newly-arrived file, matching a fresh launch's own behavior. */
    gui_playlist_win_replace_with_path(&mw->plwin, path);
  }
  gtk_window_deiconify(GTK_WINDOW(mw->window));
  gtk_window_present(GTK_WINDOW(mw->window));
  mw->minimized = false;
}

int main(int argc, char** argv) {
  gtk_init(&argc, &argv);

  const char* argv_path = (argc > 1) ? argv[1] : NULL;

  /* WinVersion.pas: IPCSendParams/StartIPC - if another instance is
   * already running, hand our one file argument to it and exit
   * immediately, before creating any window (see gui/include/gui/ipc.h
   * for exactly what's ported vs. simplified from the real
   * CommandLineInterpreter). */
  if (!gui_ipc_init(argv_path, NULL, NULL)) {
    return 0;
  }

  gui_mainwin mw;
  if (!gui_mainwin_create(&mw)) {
    gui_ipc_shutdown();
    return 1;
  }
  gui_ipc_set_callback(on_ipc_file, &mw);

  if (argv_path) {
    gui_playlist_win_replace_with_path(&mw.plwin, argv_path);
  }

  gtk_main();

  /* Part B: MainWin.pas: SaveParams's window-geometry/tray-mode/volume
   * subset - called here, between gtk_main() returning and
   * gui_mainwin_destroy below, while every window (main + playlist/
   * mixer/tools) is still alive/realized so gtk_window_get_position/
   * get_size are meaningful - see gui_mainwin_save_settings's own
   * mainwin.h comment for why this can't live inside gui_mainwin_destroy
   * itself. */
  gui_mainwin_save_settings(&mw);

  gui_ipc_shutdown();
  gui_mainwin_destroy(&mw);
  return 0;
}
