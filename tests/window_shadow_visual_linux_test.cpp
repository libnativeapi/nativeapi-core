// window_manager #594: an already realized RGB host cannot carry a transparent
// shadow gutter. Exercise the public API against real GTK/compositor surfaces.
#include <gtk/gtk.h>
#include "../src/window.h"
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <gdk/gdkx.h>
#endif
#include <iostream>

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
void Settle() {
  const auto end = g_get_monotonic_time() + 700000;
  while (g_get_monotonic_time() < end) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
}
void Inspect(GtkWidget* widget, GtkWidget* content, bool alpha, bool fitted) {
  auto* surface = gtk_widget_get_window(widget);
  int x = 0, y = 0;
  gtk_widget_translate_coordinates(content, widget, 0, 0, &x, &y);
  const int w = gtk_widget_get_allocated_width(content);
  const int h = gtk_widget_get_allocated_height(content);
  const int sw = gtk_widget_get_allocated_width(widget);
  const int sh = gtk_widget_get_allocated_height(widget);
  const int depth = gdk_visual_get_depth(gdk_window_get_visual(surface));
  std::cout << "SURFACE depth=" << depth << " content=" << x << ',' << y << ',' << w << ',' << h
            << " allocation=" << sw << ',' << sh << std::endl;
#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11_WINDOW(surface)) {
    int count = 0, order = 0;
    const int scale = gdk_window_get_scale_factor(surface);
    auto* rectangles = XShapeGetRectangles(GDK_WINDOW_XDISPLAY(surface), GDK_WINDOW_XID(surface),
                                           ShapeInput, &count, &order);
    auto accepts = [&](int px, int py) {
      for (int i = 0; i < count; ++i) {
        const auto& r = rectangles[i];
        if (px >= r.x && py >= r.y && px < r.x + r.width && py < r.y + r.height)
          return true;
      }
      return false;
    };
    Check(accepts(x * scale, y * scale) && accepts((x + w) * scale - 1, (y + h) * scale - 1),
          "actual X11 input region accepts both content edges");
    if (alpha && x > 0 && y > 0)
      Check(!accepts(0, 0), "actual X11 input region excludes the shadow corner");
    if (rectangles)
      XFree(rectangles);
  }
#endif
  if (!alpha || (fitted && x == 0 && y == 0)) {
    Check(x == 0 && y == 0 && w == sw && h == sh,
          "RGB surface or GTK fitted decoration has no reserved shadow gutter");
  } else {
    Check(x >= 132 && y >= 132 && sw - x - w >= 132 && sh - y - h >= 132,
          "RGBA surface keeps the complete shadow envelope");
    auto* image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    auto* cr = cairo_create(image);
    gtk_widget_draw(widget, cr);
    cairo_destroy(cr);
    cairo_surface_flush(image);
    const auto pixel = *reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(image));
    Check((pixel >> 24) == 0, "GTK gutter corner is transparent despite opaque theme CSS");
    cairo_surface_destroy(image);
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_WINDOW(surface)) {
      auto* display = GDK_WINDOW_XDISPLAY(surface);
      XSync(display, False);
      auto* pixels = XGetImage(display, GDK_WINDOW_XID(surface), 0, 0, 1, 1, AllPlanes, ZPixmap);
      Check(depth == 32 && pixels && (XGetPixel(pixels, 0, 0) >> 24) == 0,
            "actual X11 backing has depth 32 and transparent gutter pixels");
      if (pixels)
        XDestroyImage(pixels);
    }
#endif
  }
  if (!fitted)
    Check(w == 480 && h == 320, "content remains exactly 480x320");
}
void Run(bool rgba, bool late_preference = false) {
  std::cout << "CASE rgba=" << rgba << " late_preference=" << late_preference << std::endl;
  auto* widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(widget), "NativeAPI shadow visual regression");
  // Plain Flutter/SSD host, without a header bar selecting an RGBA visual for it.
  gtk_window_set_decorated(GTK_WINDOW(widget), FALSE);
  auto* screen = gtk_widget_get_screen(widget);
  gtk_widget_set_visual(
      widget, rgba ? gdk_screen_get_rgba_visual(screen) : gdk_screen_get_system_visual(screen));
#ifdef GDK_WINDOWING_WAYLAND
  // Wayland's regular Flutter runner uses CSD. Both visuals are RGBA there.
  if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget))) {
    gtk_window_set_decorated(GTK_WINDOW(widget), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(widget), gtk_header_bar_new());
  }
#endif
  auto* content = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(widget), content);
  g_signal_connect(content, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer) -> gboolean {
                     cairo_set_source_rgb(cr, .1, .5, .2);
                     cairo_paint(cr);
                     return TRUE;
                   }),
                   nullptr);
  gtk_window_set_default_size(GTK_WINDOW(widget), 480, 320);
  gtk_widget_show_all(widget);
  Settle();
  auto* original_surface = gtk_widget_get_window(widget);
  if (late_preference)
    gtk_widget_set_visual(widget, gdk_screen_get_rgba_visual(screen));
  nativeapi::Window window(widget);
  window.SetTitleBarStyle(nativeapi::TitleBarStyle::Hidden);
  gtk_widget_show(widget);
  Settle();
  window.SetContentSize({480, 320});
  Settle();
  const bool alpha =
      gdk_window_get_visual(gtk_widget_get_window(widget)) == gdk_screen_get_rgba_visual(screen);
  Check(original_surface == gtk_widget_get_window(widget), "host surface is never recreated");
#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11_WINDOW(original_surface))
    Check(alpha == rgba, "realized X11 visual is preserved");
#endif
  Inspect(widget, content, alpha, false);
  for (bool shadow : {false, true}) {
    window.SetHasShadow(shadow);
    Settle();
    Check(window.HasShadow() == shadow, "requested shadow policy is retained");
    Inspect(widget, content, alpha, false);
  }
  window.Maximize();
  Settle();
  Check(window.IsMaximized(), "maximize acknowledged");
  Inspect(widget, content, alpha, true);
  window.Unmaximize();
  Settle();
  Check(!window.IsMaximized(), "restore acknowledged");
  Inspect(widget, content, alpha, false);
  gtk_widget_hide(widget);
  gtk_widget_show(widget);
  Settle();
  Inspect(widget, content, alpha, false);
  gtk_widget_destroy(widget);
  Settle();
}
}  // namespace
int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv) || !gdk_screen_is_composited(gdk_screen_get_default()))
    return 77;
  auto* theme = gtk_css_provider_new();
  gtk_css_provider_load_from_data(theme, "window, window:backdrop { background: #eff0f1; }", -1,
                                  nullptr);
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(theme),
                                            GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
  g_object_unref(theme);
  Run(false);
  Run(true);
  Run(false, true);
  std::cout << "RESULT failures=" << failures << std::endl;
  return failures ? 1 : 0;
}
