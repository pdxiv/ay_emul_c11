/* See gui/include/gui/settings.h's own file comment for the full
 * rationale (GKeyFile vs. options.pas's bespoke `Name=Value` reader,
 * file location, scope). */
#include "gui/settings.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static GKeyFile* g_settings;
static char g_settings_path[1024];

/* options.pas:124-154 OptionsInit's own two-branch path resolution
 * (exe-relative, then GetAppConfigDirUTF8) collapses here to just the
 * user-config-dir branch - see settings.h's own comment on why. */
static const char* settings_path(void) {
  if (g_settings_path[0]) return g_settings_path;
  const char* base = g_get_user_config_dir();
  snprintf(g_settings_path, sizeof(g_settings_path),
           "%s/ay_emul_c11/settings.ini", base);
  return g_settings_path;
}

bool gui_settings_load(void) {
  if (g_settings) g_key_file_free(g_settings);
  g_settings = g_key_file_new();
  if (!g_settings) return false;
  /* GError intentionally discarded: a missing/malformed file just
   * leaves g_settings empty, matching options.pas's own OptionsInit
   * `if not CheckAndRead(...) then ... exit` "no file yet" case - every
   * gui_settings_get_* below already falls back to its caller-supplied
   * default in that situation. */
  g_key_file_load_from_file(g_settings, settings_path(), G_KEY_FILE_NONE,
                             NULL);
  return true;
}

bool gui_settings_save(void) {
  if (!g_settings) return false;
  const char* path = settings_path();
  char* dir = g_path_get_dirname(path);
  /* options.pas: CheckWriteAccess's own `if not FileExists(fname) then
   * if not ForceDirectories(path) then exit`. */
  g_mkdir_with_parents(dir, 0755);
  g_free(dir);
  GError* err = NULL;
  bool ok = g_key_file_save_to_file(g_settings, path, &err);
  if (!ok) {
    /* options.pas: OptionsDone's own `except end` - non-fatal, matching
     * settings.h's own comment. Reported (not silent), unlike the
     * original, since stderr costs nothing and helps debugging. */
    fprintf(stderr, "gui: failed to save settings to '%s': %s\n", path,
            err ? err->message : "unknown error");
    g_clear_error(&err);
  }
  return ok;
}

int gui_settings_get_int(const char* group, const char* key, int fallback) {
  if (!g_settings) return fallback;
  GError* err = NULL;
  int v = g_key_file_get_integer(g_settings, group, key, &err);
  if (err) {
    g_clear_error(&err);
    return fallback;
  }
  return v;
}

void gui_settings_set_int(const char* group, const char* key, int value) {
  if (!g_settings) return;
  g_key_file_set_integer(g_settings, group, key, value);
}

bool gui_settings_get_bool(const char* group, const char* key,
                            bool fallback) {
  if (!g_settings) return fallback;
  GError* err = NULL;
  gboolean v = g_key_file_get_boolean(g_settings, group, key, &err);
  if (err) {
    g_clear_error(&err);
    return fallback;
  }
  return v != FALSE;
}

void gui_settings_set_bool(const char* group, const char* key, bool value) {
  if (!g_settings) return;
  g_key_file_set_boolean(g_settings, group, key, value ? TRUE : FALSE);
}

char* gui_settings_get_string(const char* group, const char* key) {
  if (!g_settings) return NULL;
  GError* err = NULL;
  char* v = g_key_file_get_string(g_settings, group, key, &err);
  if (err) {
    g_clear_error(&err);
    return NULL;
  }
  return v; /* already a fresh g_strdup'd buffer, per GLib's own contract */
}

void gui_settings_set_string(const char* group, const char* key,
                              const char* value) {
  if (!g_settings) return;
  g_key_file_set_string(g_settings, group, key, value ? value : "");
}
