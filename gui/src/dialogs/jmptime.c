/* Started as tools/lfm_gen/lfm_gen.py's generated skeleton from
 * ay_emul/JmpTime.lfm (see MIG-0066), since hand-completed with real
 * wiring (see jmptime.h and migration_debt.yaml) - no longer pure
 * generator output, kept in gui/dialogs/ since it's still the same
 * dialog. Keeps the generator's GtkFixed literal-coordinate layout
 * (the recorded per-file decision for generated dialogs) - unlike
 * gui/src/playlist_win.c/mixer_win.c, this dialog was actually built
 * FROM the generator's output rather than replacing it outright, so
 * there's no reason to discard the layout choice already made for it.
 * The top-level window is now a real GtkDialog (Jump/Cancel as actual
 * response buttons) instead of the skeleton's plain GtkWindow with
 * unwired buttons.
 */
#include "gui/dialogs/jmptime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PlayList.pas: TimeSToStr - re-implemented with strtol/snprintf rather
 * than the original's digit-by-digit Val() walk, but producing the same
 * "H:MM:SS" (only if hours>0) / "M:SS" visual output. */
static void format_time(int total_seconds, char* out, size_t cap) {
  if (total_seconds < 0) total_seconds = 0;
  int h = total_seconds / 3600;
  int m = (total_seconds % 3600) / 60;
  int s = total_seconds % 60;
  if (h > 0) {
    snprintf(out, cap, "%d:%02d:%02d", h, m, s);
  } else {
    snprintf(out, cap, "%d:%02d", m, s);
  }
}

/* MainWin.pas: JumpToTime's nested TimeValid - accepts either a plain
 * integer (seconds) or "M:SS"/"MM:SS" (minutes, then a colon, then
 * seconds); re-implemented with strtol rather than the original's
 * Val()-based digit walk, same accepted grammar. Returns false (matching
 * TimeValid's Result := False fallthrough) for anything else. */
static bool parse_time(const char* s, int* out_seconds) {
  char* end;
  long val = strtol(s, &end, 10);
  if (end != s && *end == '\0') {
    *out_seconds = (int)val;
    return true;
  }
  if (end != s && *end == ':') {
    const char* sec_part = end + 1;
    char* end2;
    long secs = strtol(sec_part, &end2, 10);
    if (end2 != sec_part && *end2 == '\0') {
      *out_seconds = (int)(val * 60 + secs);
      return true;
    }
  }
  return false;
}

void gui_jptime_show(GtkWindow* parent, gui_playback* playback) {
  /* MainWin.pas: JumpToTime - "if not IsPlaying then exit" / "if Paused
   * then exit" collapse here to "if nothing loaded, or this format has
   * no known duration to jump within, do nothing" (see jmptime.h's own
   * comment on the AY/YM/VTX-only scope). */
  if (!playback->loaded) return;
  double duration = gui_playback_duration_seconds(playback);
  if (duration <= 0.0) return;

  GtkWidget* dlg = gtk_dialog_new_with_buttons(
      "Jump to time", parent,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, NULL, 0, NULL);
  gtk_dialog_add_button(GTK_DIALOG(dlg), "Jump", GTK_RESPONSE_ACCEPT);
  gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancel", GTK_RESPONSE_CANCEL);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

  GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
  GtkWidget* fixed = gtk_fixed_new();
  gtk_container_set_border_width(GTK_CONTAINER(fixed), 8);
  gtk_widget_set_size_request(fixed, 230, 80);
  gtk_container_add(GTK_CONTAINER(content), fixed);

  GtkWidget* label = gtk_label_new("Minutes:Seconds");
  gtk_fixed_put(GTK_FIXED(fixed), label, 112, 14);

  GtkWidget* entry = gtk_entry_new();
  gtk_widget_set_size_request(entry, 97, -1);
  char cur_buf[32];
  /* MainWin.pas: Edit1.Text := TimeSToStr(round(CurrTime_Rasch/1000)) -
   * prefill with the current position, not 0. */
  format_time((int)gui_playback_position_seconds(playback), cur_buf,
              sizeof(cur_buf));
  gtk_entry_set_text(GTK_ENTRY(entry), cur_buf);
  gtk_fixed_put(GTK_FIXED(fixed), entry, 10, 10);

  char len_buf[32], len_label_text[64];
  format_time((int)duration, len_buf, sizeof(len_buf));
  snprintf(len_label_text, sizeof(len_label_text), "Track length: %s",
           len_buf);
  GtkWidget* len_label = gtk_label_new(len_label_text);
  gtk_fixed_put(GTK_FIXED(fixed), len_label, 10, 45);

  gtk_widget_show_all(dlg);
  gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1); /* Edit1.SelectAll */
  gtk_widget_grab_focus(entry);

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    int seconds;
    if (parse_time(gtk_entry_get_text(GTK_ENTRY(entry)), &seconds)) {
      gui_playback_request_seek_seconds(playback, (double)seconds);
    }
  }
  gtk_widget_destroy(dlg);
}
