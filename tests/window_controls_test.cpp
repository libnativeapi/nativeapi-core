// Native caption policy regression. Creates only this process's windows;
// no synthetic input. Run with a desktop/window manager (77 means unavailable).
#include "nativeapi.h"
#include "../src/capi/window_c.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <future>
#include <thread>
#include <vector>
#ifdef __APPLE__
#import <AppKit/AppKit.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <gtk/gtk.h>
#endif
using namespace nativeapi;
namespace {
int failures = 0;
void Check(bool condition, const char* message) {
  std::cout << (condition ? "PASS " : "FAIL ") << message << std::endl;
  failures += !condition;
}
void Pump(int ms = 150) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  do {
#ifdef __APPLE__
    while (auto* event = [NSApp nextEventMatchingMask:NSEventMaskAny
        untilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]
        inMode:NSDefaultRunLoopMode dequeue:YES]) [NSApp sendEvent:event];
#elif defined(_WIN32)
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
#else
    for (int i = 0; i < 32 && gtk_events_pending() && std::chrono::steady_clock::now() < end; ++i)
      g_main_context_iteration(nullptr, FALSE);
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < end);
}
#if !defined(__APPLE__) && !defined(_WIN32)
bool GtkContentFillsFrame(const Window& window, GtkWidget* content) {
  const auto frame = window.GetBounds();
  const auto bounds = window.GetContentBounds();
  const bool fills = bounds.x == frame.x && bounds.y == frame.y &&
         bounds.width == frame.width && bounds.height == frame.height &&
         bounds.width == gtk_widget_get_allocated_width(content) &&
         bounds.height == gtk_widget_get_allocated_height(content);
  if (!fills)
    std::cout << "INFO GTK frame=" << frame.x << "," << frame.y << " " << frame.width << "x" << frame.height
              << " content=" << bounds.x << "," << bounds.y << " " << bounds.width << "x" << bounds.height
              << " allocation=" << gtk_widget_get_allocated_width(content) << "x"
              << gtk_widget_get_allocated_height(content) << std::endl;
  return fills;
}
void GtkHostWithoutTitleBar() {
  auto* host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  auto* content = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(host), content);
  gtk_widget_show(content);
  Window window(host);
  window.SetTitle("nativeapi GTK host without an explicit title bar");
  window.SetContentSize({520, 340});
  window.ShowInactive();
  Pump(400);
  const bool csd = gtk_style_context_has_class(gtk_widget_get_style_context(host), "csd");
  Check(window.SetFrameless(true) && window.IsFrameless(),
        "GTK host without an explicit title bar accepts frameless policy");
  Pump(400);
  if (!csd)
    Check(!gtk_window_get_decorated(GTK_WINDOW(host)), "frameless non-CSD GTK disables native decorations");
  Check(gtk_widget_get_mapped(content) && GtkContentFillsFrame(window, content),
        "frameless GTK host without an explicit title bar gives its frame to content");
  gtk_widget_destroy(host);
}
void CompositeGtkTitleBar() {
  auto* host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  auto* titlebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  auto* header = gtk_header_bar_new();
  auto* extra_row = gtk_label_new("Application title-bar row");
  auto* hidden_row = gtk_label_new("Initially hidden title-bar row");
  gtk_widget_set_size_request(extra_row, -1, 24);
  gtk_box_pack_start(GTK_BOX(titlebar), header, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(titlebar), extra_row, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(titlebar), hidden_row, FALSE, FALSE, 0);
  gtk_widget_show(header);
  gtk_widget_show(extra_row);
  gtk_widget_show(titlebar);
  gtk_window_set_titlebar(GTK_WINDOW(host), titlebar);
  auto* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  auto* content_header = gtk_header_bar_new();
  auto* drawing = gtk_drawing_area_new();
  gtk_box_pack_start(GTK_BOX(content), content_header, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(content), drawing, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(host), content);
  gtk_widget_show(content_header);
  gtk_widget_show(drawing);
  gtk_widget_show(content);
  Window window(host);
  window.SetTitle("nativeapi composite GTK title-bar regression");
  window.SetContentSize({520, 340});
  window.ShowInactive();
  Pump(400);
  std::cout << "INFO composite GTK decoration class="
            << (gtk_style_context_has_class(gtk_widget_get_style_context(host), "csd") ? "csd" : "non-CSD")
            << std::endl;
  Check(gtk_widget_get_mapped(titlebar) && gtk_widget_get_mapped(extra_row) &&
        !gtk_widget_get_visible(hidden_row), "composite GTK title bar starts with its original child visibility");
  auto check_hidden = [&] {
    Check(!gtk_widget_get_visible(titlebar) && !gtk_widget_get_mapped(titlebar) &&
          !gtk_widget_get_mapped(extra_row), "GTK hides the entire composite title bar");
    Check(gtk_widget_get_visible(header) && gtk_widget_get_visible(extra_row) &&
          !gtk_widget_get_visible(hidden_row), "GTK title-bar transitions preserve individual child visibility");
    Check(gtk_widget_get_mapped(content_header), "GTK composite title-bar changes preserve the application content header");
    Check(GtkContentFillsFrame(window, content), "hidden composite GTK title bar consumes no frame space");
    const auto size = window.GetContentSize();
    if (size.width != 520 || size.height != 340)
      std::cout << "INFO composite GTK requested=520x340 actual=" << size.width << "x" << size.height
                << " frame=" << window.GetBounds().width << "x" << window.GetBounds().height << std::endl;
    Check(size.width == 520 && size.height == 340,
          "hidden composite GTK title bar retains the requested content size");
  };
  for (int round = 0; round < 2; ++round) {
    Check(window.SetFrameless(true) && window.SetFrameless(true) && window.IsFrameless(),
          "composite GTK title bar accepts repeated frameless requests");
    // GTK's CSD resize accounting depends on the new title-bar style being measured.
    Pump(400);
    window.SetContentSize({520, 340});
    Pump(400);
    check_hidden();
    for (bool shadow : {false, true}) {
      window.SetHasShadow(shadow);
      Pump();
      Check(window.IsFrameless() && window.HasShadow() == shadow,
            "GTK shadow changes preserve frameless policy");
      check_hidden();
    }
    window.Hide();
    Pump();
    window.ShowInactive();
    Pump(400);
    check_hidden();
    window.SetTitleBarStyle(TitleBarStyle::Hidden);
    Pump();
    Check(!window.IsFrameless(), "explicit Hidden exits frameless for a composite GTK title bar");
    // Hidden may restore a non-CSD frame. Request content size after that style settles.
    window.SetContentSize({520, 340});
    Pump();
    check_hidden();
    Check(window.SetFrameless(true) && window.SetFrameless(false),
          "composite GTK title bar exits frameless through the public policy");
    Pump(400);
    Check(!window.IsFrameless() && window.GetTitleBarStyle() == TitleBarStyle::Normal &&
          gtk_widget_get_mapped(titlebar) && gtk_widget_get_mapped(header) &&
          gtk_widget_get_mapped(extra_row) && !gtk_widget_get_visible(hidden_row),
          "Normal restores the composite GTK title bar without showing initially hidden children");
    Check(gtk_widget_get_mapped(content_header) &&
          window.GetBounds().height > window.GetContentSize().height,
          "restored composite GTK title bar occupies its own space above application content");
  }
  gtk_widget_destroy(host);
}
#endif
}
int main(int argc, char** argv) {
#ifdef __APPLE__
  [NSApplication sharedApplication];
#elif !defined(_WIN32)
  if (!gtk_init_check(&argc, &argv)) return 77;
#endif
  Application::GetInstance();
#if !defined(__APPLE__) && !defined(_WIN32)
  auto* host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  auto* header = gtk_header_bar_new();
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), ":minimize,maximize,close");
  gtk_widget_show(header);
  gtk_window_set_titlebar(GTK_WINDOW(host), header);
  auto* content_header = gtk_header_bar_new();
  gtk_widget_show(content_header);
  gtk_container_add(GTK_CONTAINER(host), content_header);
  Window window(host);
#else
  Window window;
#endif
  window.SetTitle("nativeapi caption controls regression");
  window.SetBounds({100, 100, 600, 400});
  window.ShowInactive();
  Window alias(window.GetNativeObject());
  auto ffi_window = native_window_create_with_native_window(window.GetNativeObject());
  Check(native_window_get_id(ffi_window) == window.GetId(), "C ABI alias retains native identity");
  std::vector<WindowProperty> changes;
  window.AddListener<WindowPropertyChangedEvent>([&](const auto& event) {
    changes.push_back(event.GetProperty());
  });
  Pump(400);
#ifdef __APPLE__
  Check(window.GetNativeNumber() == [(__bridge NSWindow*)window.GetNativeObject() windowNumber] &&
        native_window_get_native_number(ffi_window) == window.GetNativeNumber(),
        "C ABI reports the native AppKit window-server number");
#endif
  const bool minimize = window.IsMinimizable();
  const bool maximize = window.IsMaximizable();
  const bool close = window.IsClosable();
  Check(alias.IsWindowControlButtonsVisible(), "native button group starts visible");
  changes.clear();
  native_window_set_window_control_buttons_visible(ffi_window, false);
  Pump();
  Check(!alias.IsWindowControlButtonsVisible() &&
        !native_window_is_window_control_buttons_visible(ffi_window),
        "C ABI hiding and state are shared by native window aliases");
  Check(alias.IsMinimizable() == minimize && alias.IsMaximizable() == maximize &&
        alias.IsClosable() == close, "group hiding preserves independent caption policies");
  Check(std::count(changes.begin(), changes.end(), WindowProperty::WindowControlButtonsVisible) == 1 &&
        std::count(changes.begin(), changes.end(), WindowProperty::Minimizable) == 0 &&
        std::count(changes.begin(), changes.end(), WindowProperty::Maximizable) == 0 &&
        std::count(changes.begin(), changes.end(), WindowProperty::Closable) == 0,
        "hiding emits one group change without changing independent policies");
#if defined(_WIN32)
  const auto hwnd = static_cast<HWND>(window.GetNativeObject());
  Check(!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_SYSMENU) &&
        (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION,
        "Win32 removes caption buttons while retaining the title bar");
#elif !defined(__APPLE__)
  Check(!gtk_header_bar_get_show_close_button(GTK_HEADER_BAR(header)),
        "GTK's native standard decoration group is hidden");
  Check(gtk_widget_get_visible(content_header), "application content headers remain visible");
#endif
#if defined(_WIN32)
  auto worker = std::async(std::launch::async, [&] {
    alias.SetWindowControlButtonsVisible(true);
    return alias.IsWindowControlButtonsVisible();
  });
  for (int i = 0; i < 70 && worker.wait_for(std::chrono::milliseconds(0)) !=
      std::future_status::ready; ++i) Pump(100);
  Check(worker.get(), "worker calls apply and read controls on their native owner thread");
  window.SetWindowControlButtonsVisible(false);
#endif
  alias.SetMinimizable(false);
  alias.SetMaximizable(false);
  alias.SetClosable(false);
  native_window_set_window_control_buttons_visible(ffi_window, true);
  Pump();
  Check(!window.IsMinimizable() && !window.IsMaximizable() && !window.IsClosable(),
        "showing the group does not re-enable independently disabled actions");
  window.SetClosable(true);
  window.SetMinimizable(true);
  window.SetMaximizable(true);
  Pump();
  Check(alias.IsWindowControlButtonsVisible(), "re-enabling policies restores native buttons");
#if defined(_WIN32)
  window.SetClosable(false);
  const UINT close_state = GetMenuState(GetSystemMenu(hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND);
  Check(!alias.IsClosable() && window.IsWindowControlButtonsVisible() &&
        window.IsMinimizable() && window.IsMaximizable() &&
        close_state != static_cast<UINT>(-1) && (close_state & (MF_DISABLED | MF_GRAYED)),
        "only the native close command is disabled, retaining minimize and maximize");
  window.SetClosable(true);
#endif
  window.SetTitleBarStyle(TitleBarStyle::Hidden);
  window.SetWindowControlButtonsVisible(false);
  Pump();
  Check(!alias.IsWindowControlButtonsVisible(), "a hidden title bar has no native buttons");
#if defined(_WIN32)
  const auto custom_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  Check((custom_style & (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) ==
        (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
        "custom chrome retains native system-menu and snap styles");
#elif !defined(__APPLE__)
  Check(!gtk_widget_get_visible(header) && gtk_widget_get_visible(content_header),
        "title-bar style changes affect only the window's own header");
#endif
  window.SetTitleBarStyle(TitleBarStyle::Normal);
  Pump();
  Check(alias.IsWindowControlButtonsVisible(), "normal title-bar style resets group visibility");
  const bool shadow = window.HasShadow();
  changes.clear();
  Check(native_window_set_frameless(ffi_window, true), "C ABI accepts frameless mode");
#if !defined(__APPLE__) && !defined(_WIN32)
  Pump(400);  // Include the deferred GTK shadow-gutter decision.
#else
  Pump();
#endif
  Check(alias.IsFrameless() && native_window_is_frameless(ffi_window) &&
        window.GetTitleBarStyle() == TitleBarStyle::Hidden && !window.IsWindowControlButtonsVisible(),
        "C ABI frameless policy is shared and hides the native caption");
  Check(std::count(changes.begin(), changes.end(), WindowProperty::Frameless) == 1,
        "frameless entry emits exactly one property change");
#if defined(_WIN32)
  auto full_client = [&] {
    RECT outer{}, client{};
    GetWindowRect(hwnd, &outer);
    GetClientRect(hwnd, &client);
    POINT origin{};
    ClientToScreen(hwnd, &origin);
    return origin.x == outer.left && origin.y == outer.top &&
           client.right == outer.right - outer.left && client.bottom == outer.bottom - outer.top;
  };
  Check(full_client(), "frameless content fills the entire Win32 frame");
  RECT frame_rect{};
  GetWindowRect(hwnd, &frame_rect);
  Check(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(frame_rect.left + 1,
            (frame_rect.top + frame_rect.bottom) / 2)) == HTLEFT,
        "frameless Win32 content retains native edge resize hit testing");
#elif !defined(__APPLE__)
  Check(!gtk_widget_get_visible(header) && gtk_widget_get_mapped(content_header) &&
        GtkContentFillsFrame(window, content_header),
        "frameless GTK removes visible decorations without hiding or insetting application content");
  if (gtk_style_context_has_class(gtk_widget_get_style_context(host), "csd"))
    Check(gtk_window_get_decorated(GTK_WINDOW(host)), "frameless CSD GTK retains decoration geometry for shadows");
  else
    Check(!gtk_window_get_decorated(GTK_WINDOW(host)), "frameless non-CSD GTK disables native decorations");
#else
  NSWindow* ns_window = (__bridge NSWindow*)window.GetNativeObject();
  NSView* title_view = [[ns_window standardWindowButton:NSWindowCloseButton] superview].superview;
  Check(title_view && title_view.hidden, "frameless AppKit hides the native title-bar container");
#endif
  window.SetHasShadow(false);
  window.SetHasShadow(true);
  Pump();
  Check(alias.IsFrameless() && window.HasShadow(), "shadows do not restore a frameless window's frame");
  window.SetWindowControlButtonsVisible(true);
  Check(!window.IsWindowControlButtonsVisible(), "frameless mode cannot reveal native caption controls");
  window.SetWindowControlButtonsVisible(false);
#if defined(_WIN32)
  Check(full_client(), "re-enabling Win32 shadows leaves the client frame intact");
#endif
  alias.SetTitleBarStyle(TitleBarStyle::Hidden);
  Pump();
  Check(!window.IsFrameless() && window.GetTitleBarStyle() == TitleBarStyle::Hidden,
        "explicit hidden title-bar style exits frameless mode");
  window.SetFrameless(true);
  alias.SetFrameless(false);
  Pump();
  Check(!window.IsFrameless() && window.GetTitleBarStyle() == TitleBarStyle::Normal,
        "disabling frameless restores a normal title bar");
  window.SetHasShadow(shadow);
#ifdef __APPLE__
  window.SetResizable(true);
  alias.SetMaximizable(false);
  Pump();
  Check(window.IsResizable() && !window.IsMaximizable(),
        "macOS zoom policy is independent of edge resizing");
  const auto before_zoom = window.GetBounds();
  [ns_window zoom:nil];
  Pump();
  Check(!window.IsMaximized() && window.GetBounds().width == before_zoom.width,
        "disabled native zoom leaves the window unchanged");
  window.Maximize();
  Pump();
  Check(window.IsMaximized(), "explicit maximize remains available with native zoom disabled");
  window.Unmaximize();
  Pump();
  window.SetResizable(false);
  alias.SetMaximizable(true);
  Check(!window.IsResizable() && window.IsMaximizable(),
        "enabling macOS zoom does not re-enable edge resizing");
  window.SetResizable(true);
#endif
#if !defined(__APPLE__)
  window.SetClosable(false);
  changes.clear();
  window.SetFullScreen(true);
  for (int i = 0; i < 50 && !window.IsFullScreen(); ++i) Pump(100);
  Check(window.IsFullScreen() && !window.IsWindowControlButtonsVisible(),
        "fullscreen has no normal caption buttons");
  Check(std::count(changes.begin(), changes.end(), WindowProperty::WindowControlButtonsVisible) == 1,
        "native fullscreen emits one caption-group visibility change");
  alias.SetWindowControlButtonsVisible(false);
  alias.SetMinimizable(false);
  alias.SetMaximizable(false);
  window.SetFullScreen(false);
  for (int i = 0; i < 50 && window.IsFullScreen(); ++i) Pump(100);
  Check(!window.IsFullScreen() && !window.IsWindowControlButtonsVisible() &&
        !window.IsMinimizable() && !window.IsMaximizable() && !window.IsClosable(),
        "fullscreen restores updated visibility and independent policies");
#endif
  int closes = 0;
  window.AddListener<WindowCloseRequestedEvent>([&](const auto& event) {
    ++closes;
    event.GetRequest()->Cancel();
  });
  Pump();
  window.SetClosable(false);
  window.Close();
  for (int i = 0; i < 50 && !closes; ++i) Pump(50);
  Check(closes == 1, "programmatic close bypasses disabled native close controls");
  native_window_free(ffi_window);
#ifdef __APPLE__
  [(__bridge NSWindow*)window.GetNativeObject() close];
#elif defined(_WIN32)
  DestroyWindow(hwnd);
#else
  gtk_widget_destroy(host);
  GtkHostWithoutTitleBar();
  CompositeGtkTitleBar();
#endif
  return failures ? 1 : 0;
}
