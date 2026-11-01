// No-input X11 decoration hint regression. GTK_CSD=0 selects server decorations;
// this checks the requested hints, whose rendering remains the WM's decision.
#include "nativeapi.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <iostream>
using namespace nativeapi;
int main(int argc, char** argv) {
  g_setenv("GTK_CSD", "0", TRUE);
  if (!gtk_init_check(&argc, &argv)) return 77;
  auto* host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  nativeapi::Window window(host);
  auto* surface = gtk_widget_get_window(host);
  if (!GDK_IS_X11_WINDOW(surface)) {
    gtk_widget_destroy(host);
    return 77;
  }
  GdkWMDecoration original = GDK_DECOR_ALL;
  gdk_window_get_decorations(surface, &original);
  window.ShowInactive();
  window.SetWindowControlButtonsVisible(false);
  GdkWMDecoration hidden = GDK_DECOR_ALL;
  gdk_window_get_decorations(surface, &hidden);
  bool ok = !(hidden & (GDK_DECOR_ALL | GDK_DECOR_MENU | GDK_DECOR_MINIMIZE | GDK_DECOR_MAXIMIZE)) &&
            (hidden & GDK_DECOR_TITLE) && !window.IsWindowControlButtonsVisible();
  window.SetWindowControlButtonsVisible(true);
  GdkWMDecoration shown = static_cast<GdkWMDecoration>(0);
  gdk_window_get_decorations(surface, &shown);
  ok = ok && shown == original && window.IsWindowControlButtonsVisible();
  // A fresh host starts with no stored group policy. Hiding the title bar first
  // must capture decorations before GTK replaces them with a zero mask.
  auto* second = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  nativeapi::Window other(second);
  other.SetTitleBarStyle(TitleBarStyle::Hidden);
  other.SetTitleBarStyle(TitleBarStyle::Normal);
  GdkWMDecoration restored = static_cast<GdkWMDecoration>(0);
  gdk_window_get_decorations(gtk_widget_get_window(second), &restored);
  ok = ok && restored == original && other.IsWindowControlButtonsVisible();
  std::cout << (ok ? "PASS " : "FAIL ")
            << "X11 caption hints hide only buttons and restore initial decorations" << std::endl;
  gtk_widget_destroy(host);
  gtk_widget_destroy(second);
  return ok ? 0 : 1;
}
