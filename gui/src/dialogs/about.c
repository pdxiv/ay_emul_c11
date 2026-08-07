#include "gui/dialogs/about.h"

#include <gdk/gdkkeysyms.h>
#include <string.h>

#include "gui/regions.h"
#include "gui/zones.h"

/* gui/src/about_bitmap.c - generated from ay_emul/About.bmp, see that
 * file's own header comment for how to regenerate it. */
extern const unsigned char gui_about_bmp[];
extern const unsigned int gui_about_bmp_len;

#define AB_WIDTH 343
#define AB_HEIGHT 346

typedef struct about_win {
  GtkWidget* window;
  GtkWidget* area;
  GdkPixbuf* bitmap; /* the full 446x350 source sheet, not just the
                       * 343x346 base crop - button sub-images live
                       * outside that crop too (About.pas:FormCreate) */
  gui_button but_ok;
  gui_button but_help;
  gui_button* pressed_button; /* see gui/src/mainwin.c's own
                                * pressed_button field comment - same
                                * press-and-drag-off-to-cancel tracking */
  GMainLoop* loop;
} about_win;

static void draw_base(cairo_t* cr, GdkPixbuf* bitmap) {
  /* About.pas:FormCreate's AbDBuffer.Canvas.CopyRect(Rect(0,0,343,346),
   * Bitmap.Canvas, Rect(1,2,344,348)) - the base image is a 343x346
   * crop starting at (1,2) in the source sheet, not (0,0). */
  cairo_save(cr);
  cairo_translate(cr, -1, -2);
  gdk_cairo_set_source_pixbuf(cr, bitmap, 0, 0);
  cairo_rectangle(cr, 1, 2, AB_WIDTH, AB_HEIGHT);
  cairo_fill(cr);
  cairo_restore(cr);
}

static void draw_version_text(cairo_t* cr) {
  /* MainWin.pas:2574-2577 (Linux branch): Font.Height=34, centered at
   * (122,260) - VersionString = '3.0' (winversion.pas). */
  cairo_save(cr);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 34);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, "3.0", &ext);
  double x = 122.0 - (ext.width / 2.0 + ext.x_bearing);
  double y = 260.0 - (ext.height / 2.0 + ext.y_bearing);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, "3.0");
  cairo_restore(cr);
}

static gboolean on_expose(GtkWidget* widget, GdkEventExpose* event,
                           gpointer data) {
  (void)event;
  about_win* aw = (about_win*)data;
  cairo_t* cr = gdk_cairo_create(gtk_widget_get_window(widget));
  draw_base(cr, aw->bitmap);
  gui_button_draw(&aw->but_ok, cr, aw->bitmap);
  gui_button_draw(&aw->but_help, cr, aw->bitmap);
  draw_version_text(cr);
  cairo_destroy(cr);
  return FALSE;
}

static void on_realize(GtkWidget* widget, gpointer data) {
  (void)data;
  gui_apply_about_window_shape(gtk_widget_get_window(widget));
}

static gboolean on_button_press(GtkWidget* widget, GdkEventButton* event,
                                 gpointer data) {
  about_win* aw = (about_win*)data;
  int x = (int)event->x, y = (int)event->y;
  if (gui_button_hit_test(&aw->but_ok, x, y)) {
    aw->but_ok.is_pushed = true;
    aw->pressed_button = &aw->but_ok;
  } else if (gui_button_hit_test(&aw->but_help, x, y)) {
    aw->but_help.is_pushed = true;
    aw->pressed_button = &aw->but_help;
  } else {
    /* About.pas:FormMouseDown - BeginDrag when neither button is hit,
     * same as MainWin.pas's own MoveWin zone (see gui/src/mainwin.c's
     * on_button_press). Targets aw->window (the real toplevel), not
     * `widget` (aw->area, a child GtkDrawingArea with its own nested
     * GdkWindow) - passing the child window to gdk_window_begin_move_
     * drag silently no-ops on WMs that don't walk up to the real
     * toplevel themselves, same real bug just fixed in mainwin.c's
     * own drag zone. */
    gdk_window_begin_move_drag(gtk_widget_get_window(aw->window),
                                event->button, (int)event->x_root,
                                (int)event->y_root, event->time);
  }
  gtk_widget_queue_draw(widget);
  return TRUE;
}

/* About.pas:FormMouseUp - the "Help" zone opens FrmMain.CallHelp
 * (a Windows .chm help file this port doesn't have, see mainwin.c's
 * own on_key_press comment on why CallHelp itself isn't ported); shown
 * here as an informational message instead of a silent no-op, matching
 * CallHelp's own graceful fallback (`if not OpenDocument(h) then
 * ShowMessage(...)`) for when the .chm is genuinely missing. */
static void show_help_unavailable(GtkWidget* parent) {
  GtkWidget* msg = gtk_message_dialog_new(
      GTK_WINDOW(parent), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
      "Help documentation is not available in this port.");
  gtk_dialog_run(GTK_DIALOG(msg));
  gtk_widget_destroy(msg);
}

/* About.pas: FormMouseMove's own Push/UnPush tracking (mirrors
 * MainWin.pas's same per-frame behavior - see gui/src/mainwin.c's
 * pressed_button comment) - purely the visual feedback here, since
 * on_button_release above already re-checks the release position
 * itself and was never functionally broken like mainwin.c's
 * equivalent was. */
static gboolean on_motion(GtkWidget* widget, GdkEventMotion* event,
                           gpointer data) {
  about_win* aw = (about_win*)data;
  if (aw->pressed_button) {
    aw->pressed_button->is_pushed = gui_button_hit_test(
        aw->pressed_button, (int)event->x, (int)event->y);
    gtk_widget_queue_draw(widget);
  }
  return TRUE;
}

static gboolean on_button_release(GtkWidget* widget, GdkEventButton* event,
                                   gpointer data) {
  about_win* aw = (about_win*)data;
  int x = (int)event->x, y = (int)event->y;
  bool was_ok = aw->but_ok.is_pushed;
  bool was_help = aw->but_help.is_pushed;
  aw->but_ok.is_pushed = false;
  aw->but_help.is_pushed = false;
  aw->pressed_button = NULL;
  gtk_widget_queue_draw(widget);
  if (was_ok && gui_button_hit_test(&aw->but_ok, x, y)) {
    gtk_widget_destroy(aw->window);
  } else if (was_help && gui_button_hit_test(&aw->but_help, x, y)) {
    show_help_unavailable(aw->window);
  }
  return TRUE;
}

static gboolean on_key_press(GtkWidget* widget, GdkEventKey* event,
                              gpointer data) {
  (void)data;
  if (event->keyval == GDK_Escape) { /* About.pas:FormKeyPress */
    gtk_widget_destroy(widget);
    return TRUE;
  }
  return FALSE;
}

static void on_destroy(GtkWidget* widget, gpointer data) {
  (void)widget;
  about_win* aw = (about_win*)data;
  if (g_main_loop_is_running(aw->loop)) g_main_loop_quit(aw->loop);
}

void gui_about_show(GtkWindow* parent) {
  about_win aw;
  memset(&aw, 0, sizeof(aw));

  GError* err = NULL;
  GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
  bool ok = gdk_pixbuf_loader_write(loader, gui_about_bmp, gui_about_bmp_len,
                                     &err);
  if (ok) ok = gdk_pixbuf_loader_close(loader, &err) != FALSE;
  if (err) g_error_free(err);
  if (ok) {
    aw.bitmap = gdk_pixbuf_loader_get_pixbuf(loader);
    if (aw.bitmap) g_object_ref(aw.bitmap);
  }
  g_object_unref(loader);
  if (!aw.bitmap) return; /* shouldn't happen for the embedded bitmap */

  /* About.pas:FormCreate - But[0] (OK) and But[1] ("Help"/logo). */
  aw.but_ok = (gui_button){.x = 176, .y = 250, .w = 83, .h = 82,
                            .src_normal_x = 177, .src_normal_y = 252,
                            .src_pushed_x = 323, .src_pushed_y = 252};
  aw.but_help = (gui_button){.x = 243, .y = 0, .w = 100, .h = 142,
                              .src_normal_x = 244, .src_normal_y = 2,
                              .src_pushed_x = 345, .src_pushed_y = 2};

  aw.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(aw.window), "About program");
  gtk_window_set_decorated(GTK_WINDOW(aw.window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(aw.window), FALSE);
  gtk_widget_set_size_request(aw.window, AB_WIDTH, AB_HEIGHT);
  if (parent) gtk_window_set_transient_for(GTK_WINDOW(aw.window), parent);
  gtk_window_set_modal(GTK_WINDOW(aw.window), TRUE);
  gtk_window_set_position(GTK_WINDOW(aw.window), GTK_WIN_POS_CENTER_ON_PARENT);
  g_signal_connect(aw.window, "realize", G_CALLBACK(on_realize), NULL);
  g_signal_connect(aw.window, "key-press-event", G_CALLBACK(on_key_press),
                    &aw);
  g_signal_connect(aw.window, "destroy", G_CALLBACK(on_destroy), &aw);

  aw.area = gtk_drawing_area_new();
  gtk_widget_set_size_request(aw.area, AB_WIDTH, AB_HEIGHT);
  gtk_widget_add_events(aw.area, GDK_BUTTON_PRESS_MASK |
                                      GDK_BUTTON_RELEASE_MASK |
                                      GDK_POINTER_MOTION_MASK);
  g_signal_connect(aw.area, "expose-event", G_CALLBACK(on_expose), &aw);
  g_signal_connect(aw.area, "button-press-event",
                    G_CALLBACK(on_button_press), &aw);
  g_signal_connect(aw.area, "button-release-event",
                    G_CALLBACK(on_button_release), &aw);
  g_signal_connect(aw.area, "motion-notify-event", G_CALLBACK(on_motion),
                    &aw);
  gtk_container_add(GTK_CONTAINER(aw.window), aw.area);

  aw.loop = g_main_loop_new(NULL, FALSE);
  gtk_widget_show_all(aw.window);
  g_main_loop_run(aw.loop); /* About.pas: ShowModal - blocks the caller
                              * until the window is destroyed (OK/
                              * Escape/window-manager close), same as a
                              * real modal ShowModal call, without
                              * needing a GtkDialog (this window's
                              * clicks are all custom hit-tested, not
                              * stock dialog buttons) */
  g_main_loop_unref(aw.loop);

  g_object_unref(aw.bitmap);
}
