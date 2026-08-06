#include "gui/ipc.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static char g_socket_path[1024];
static int g_server_fd = -1;
static gui_ipc_file_cb g_on_file;
static void* g_on_file_userdata;

static void socket_path(char* out, size_t cap) {
  /* g_get_user_runtime_dir() is glib's XDG_RUNTIME_DIR wrapper - a
   * per-user, not-world-readable directory, appropriate for a socket
   * only this user's own instances should reach (WinVersion.pas's
   * IPCServer.Global := True is broader, all-users-on-the-machine, but
   * this port doesn't have a multi-user deployment scenario to match
   * that against - a documented simplification, not a silent gap). */
  snprintf(out, cap, "%s/ay_emul_c11.sock", g_get_user_runtime_dir());
}

static gboolean on_server_readable(GIOChannel* channel, GIOCondition cond,
                                    gpointer data) {
  (void)channel;
  (void)cond;
  (void)data;
  int client_fd = accept(g_server_fd, NULL, NULL);
  if (client_fd < 0) return TRUE; /* keep watching */

  /* Local, single-user, trusted socket; a short recv timeout is enough
   * to keep a stuck/malicious client from blocking the GTK main loop
   * indefinitely, without needing a full async read state machine for
   * what's normally a few bytes sent then immediately closed. */
  struct timeval tv = {2, 0};
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  char buf[1024];
  ssize_t total = 0;
  ssize_t n;
  while (total < (ssize_t)sizeof(buf) - 1 &&
         (n = recv(client_fd, buf + total, sizeof(buf) - 1 - total, 0)) > 0) {
    total += n;
  }
  close(client_fd);
  buf[total] = '\0';

  if (total > 0 && g_on_file) g_on_file(buf, g_on_file_userdata);
  return TRUE; /* keep watching for the next connection */
}

bool gui_ipc_init(const char* argv_path, gui_ipc_file_cb on_file,
                   void* userdata) {
  socket_path(g_socket_path, sizeof(g_socket_path));

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  /* sun_path is typically 108 bytes (Linux) - much shorter than
   * g_socket_path's 1024-byte buffer. XDG_RUNTIME_DIR is always short
   * in practice (e.g. /run/user/1000), but check explicitly rather than
   * let strncpy silently truncate into a colliding/wrong socket path. */
  if (strlen(g_socket_path) >= sizeof(addr.sun_path)) {
    return true; /* can't set up IPC - proceed without single-instance
                   * forwarding, same fallback as the bind/listen
                   * failure path below */
  }
  memcpy(addr.sun_path, g_socket_path, strlen(g_socket_path) + 1);

  /* First, try connecting as a CLIENT - if another instance is already
   * listening, hand off argv_path to it and let the caller exit
   * (WinVersion.pas: IPCSendParams). */
  int probe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (probe_fd >= 0) {
    if (connect(probe_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
      if (argv_path) send(probe_fd, argv_path, strlen(argv_path), 0);
      close(probe_fd);
      return false;
    }
    close(probe_fd);
  }

  /* No server reachable (or the socket file is stale - unlink and
   * rebind either way) - become the server ourselves. */
  unlink(g_socket_path);
  g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (g_server_fd < 0) return true; /* can't set up IPC - proceed anyway,
                                      * just without single-instance
                                      * forwarding, rather than failing
                                      * to start the app at all */
  if (bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
      listen(g_server_fd, 8) != 0) {
    close(g_server_fd);
    g_server_fd = -1;
    return true;
  }

  g_on_file = on_file;
  g_on_file_userdata = userdata;
  GIOChannel* channel = g_io_channel_unix_new(g_server_fd);
  g_io_add_watch(channel, G_IO_IN, on_server_readable, NULL);
  g_io_channel_unref(channel); /* the watch itself keeps a ref */

  return true;
}

void gui_ipc_set_callback(gui_ipc_file_cb on_file, void* userdata) {
  g_on_file = on_file;
  g_on_file_userdata = userdata;
}

void gui_ipc_shutdown(void) {
  if (g_server_fd >= 0) {
    close(g_server_fd);
    g_server_fd = -1;
    unlink(g_socket_path);
  }
}
