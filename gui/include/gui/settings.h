/* Minimal, generic settings-persistence layer - Part B of the Mixer-
 * window-adjacent session (window-position + tray-mode persistence).
 *
 * Real Pascal persists ~80 settings via MainWin.pas's own SaveDW/GetDW/
 * SaveStr/GetStr helpers (MainWin.pas:4279-4440 SaveParams, ~4509-4800
 * CommandLineAndRegCheck's load side) onto a flat `Name=Value` text file
 * (options.pas: `Ay_Emul.cfg`, next to the executable if writable there,
 * else GetAppConfigDirUTF8 - see options.pas:124-154's OptionsInit).
 * Porting that entire ~80-key surface is out of scope for this session
 * (tracked as open migration debt where it matters - see
 * migration_debt.yaml); this module is deliberately generic and small,
 * covering only the handful of keys this session's own features need
 * (window geometry, tray mode, system-mixer selection - see gui/src/
 * mainwin.c and gui/src/tools_win.c for the actual key names in use).
 *
 * GLib's own GKeyFile (g_key_file_new/load_from_file/save_to_file/
 * get_integer/set_integer etc.) replaces the original's bespoke
 * `Name=Value` reader/writer - already available transitively via GTK2's
 * own dependency on GLib (see gui/Makefile's GTK_CFLAGS), so no new
 * library dependency is introduced, and it's the natural idiomatic
 * choice for a GTK2 app's own settings file on Linux (an INI-style
 * format, sections instead of a single flat namespace, real quoting/
 * escaping rules) rather than reimplementing options.pas's own simpler
 * but less standard reader.
 *
 * File location: $XDG_CONFIG_HOME/ay_emul_c11/settings.ini (via GLib's
 * own g_get_user_config_dir(), which already resolves the same
 * $XDG_CONFIG_HOME-with-~/.config-fallback rule the original's
 * GetAppConfigDirUTF8 uses on Linux) - the "next to the executable"
 * fallback branch of options.pas's own OptionsInit is not replicated
 * (a portable install's executable directory is frequently read-only,
 * e.g. /usr/bin - the user-config-dir path alone is sufficient and is
 * the primary/preferred branch in the original too).
 */
#ifndef GUI_SETTINGS_H
#define GUI_SETTINGS_H

#include <stdbool.h>

/* Loads the on-disk settings file, if any, into an in-memory GKeyFile
 * (kept for the process's lifetime). Safe to call once at startup;
 * a missing file is not an error (every gui_settings_get_* below then
 * just returns its caller-supplied default). Returns false only on a
 * real GLib allocation failure (never on "file doesn't exist yet"). */
bool gui_settings_load(void);

/* Writes the in-memory settings back to disk (creating
 * $XDG_CONFIG_HOME/ay_emul_c11/ if it doesn't exist yet - GLib's
 * g_mkdir_with_parents, matching options.pas's own ForceDirectories
 * call in CheckWriteAccess). Returns false if the write itself failed
 * (e.g. no write permission) - matching OptionsDone's own silent
 * `except` swallow, this is intentionally non-fatal to the caller: a
 * failed settings save should never block application shutdown. */
bool gui_settings_save(void);

/* [group]key lookups/stores - `group` is a plain GKeyFile group name
 * (this module's own callers use "Window" and "Volume" - see mainwin.c/
 * tools_win.c), `key` the option name (matching the original's own flat
 * SaveDW/GetDW key names, e.g. "MainX"/"TrayMode", where it lines up one
 * to one - see each call site's own comment for the exact Pascal name
 * being mirrored). */
int gui_settings_get_int(const char* group, const char* key, int fallback);
void gui_settings_set_int(const char* group, const char* key, int value);

bool gui_settings_get_bool(const char* group, const char* key,
                            bool fallback);
void gui_settings_set_bool(const char* group, const char* key, bool value);

/* Returns a newly g_strdup'd string (caller g_free()s it), or NULL if
 * absent - matching GetStr's own boolean "found or not" contract rather
 * than GetDW's "out param + boolean" one, since C has no `var` string
 * out-param convention as cheap as Object Pascal's here. */
char* gui_settings_get_string(const char* group, const char* key);
void gui_settings_set_string(const char* group, const char* key,
                              const char* value);

#endif /* GUI_SETTINGS_H */
