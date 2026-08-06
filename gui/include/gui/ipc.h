/* C11 port of WinVersion.pas's IPCServer/IPCClient single-instance
 * mechanism (StartIPC/StopIPC/IPCSendParams, backed by Lazarus's
 * TSimpleIPCServer/TSimpleIPCClient - a named Unix domain socket on
 * Linux). Scoped narrower than the original on purpose: the real
 * CommandLineInterpreter (MainWin.pas:1118+) accepts a large set of
 * `-s`/`-b`/`-z`/`-y`/`-q`/`-t`/`-a`/etc. flags (sample rate, bit depth,
 * Z80/AY/MFP clock overrides...) that this port has no equivalent
 * settings surface for yet (see migration_debt.yaml) - this only
 * forwards a single plain file/directory path argument from a second
 * invocation to the first already-running instance, the actual
 * real-world use case ("open a file, app is already running").
 */
#ifndef GUI_IPC_H
#define GUI_IPC_H

#include <stdbool.h>

typedef void (*gui_ipc_file_cb)(const char* path, void* userdata);

/* If another instance is already running (a server is listening on the
 * well-known socket): forwards `argv_path` (may be NULL) to it and
 * returns false - the caller should exit immediately without creating
 * any GUI, matching IPCSendParams's real "hand off and quit" behavior.
 * Otherwise, becomes the server (listens for future gui_ipc_init calls
 * from other processes, delivering their paths via `on_file`, which
 * fires on the GTK main thread - safe to touch GTK widgets from it) and
 * returns true - the caller should proceed to create its GUI normally,
 * same as if it were started with no arguments. */
bool gui_ipc_init(const char* argv_path, gui_ipc_file_cb on_file,
                   void* userdata);

/* Updates the callback used for files arriving from FUTURE gui_ipc_init
 * calls made by other processes (only meaningful after gui_ipc_init
 * returned true - i.e. this process is the server). Lets the caller
 * create its GUI (which needs the socket-ownership decision from
 * gui_ipc_init to happen before any window is shown) and only then
 * bind the callback to that now-existing GUI state. */
void gui_ipc_set_callback(gui_ipc_file_cb on_file, void* userdata);

/* Stops listening and removes the socket file. Safe to call even if
 * this process never became the server. */
void gui_ipc_shutdown(void);

#endif /* GUI_IPC_H */
