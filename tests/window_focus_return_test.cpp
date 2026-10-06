// Real desktop regression for Focus() / Blur() (issue #69): Focus() from a
// background process, Blur() back to the previous window of this process and
// to another process's window. The other process is this executable started
// with --anchor. No mouse or keyboard input; changes the foreground window, so
// it is not registered with CTest. Exit 77 = skipped (no usable desktop).
//
// Linux needs X11 with a window manager that maintains _NET_ACTIVE_WINDOW
// (Openbox on Xvfb works); Wayland cannot report or activate other clients.
#include "nativeapi.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#undef None
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}

void Pump(int milliseconds = 100) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  do {
#ifdef _WIN32
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
#else
    while (gtk_events_pending()) gtk_main_iteration();
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < end);
}

// Focus changes are asynchronous everywhere; wait for the window system.
bool WaitFor(const std::function<bool()>& done, int milliseconds = 3000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (!done()) {
    if (std::chrono::steady_clock::now() >= end) return false;
    Pump(20);
  }
  Pump(50);
  return done();
}

// The foreground toplevel as a number comparable with Handle().
unsigned long long Foreground() {
#ifdef _WIN32
  HWND foreground = GetForegroundWindow();
  return reinterpret_cast<unsigned long long>(foreground ? GetAncestor(foreground, GA_ROOT)
                                                         : nullptr);
#else
  GdkDisplay* display = gdk_display_get_default();
  Display* xdisplay = GDK_DISPLAY_XDISPLAY(display);
  Atom type = 0;
  int format = 0;
  unsigned long count = 0, remaining = 0;
  unsigned char* data = nullptr;
  unsigned long long active = 0;
  if (XGetWindowProperty(xdisplay, DefaultRootWindow(xdisplay),
                         XInternAtom(xdisplay, "_NET_ACTIVE_WINDOW", False), 0, 1, False,
                         XA_WINDOW, &type, &format, &count, &remaining, &data) == Success &&
      data && count == 1)
    active = *reinterpret_cast<unsigned long*>(data);
  if (data) XFree(data);
  return active;
#endif
}

void DescribeForeground(const char* when) {
#ifdef _WIN32
  HWND foreground = GetForegroundWindow();
  wchar_t title[256] = L"";
  if (foreground) GetWindowTextW(foreground, title, 256);
  DWORD pid = 0;
  if (foreground) GetWindowThreadProcessId(foreground, &pid);
  std::wcout << L"  foreground " << when << L": " << reinterpret_cast<unsigned long long>(foreground)
             << L" pid " << pid << L" \"" << title << L"\"" << std::endl;
#else
  std::cout << "  foreground " << when << ": " << Foreground() << std::endl;
#endif
}

unsigned long long Handle(nativeapi::Window& window) {
#ifdef _WIN32
  return reinterpret_cast<unsigned long long>(window.GetNativeObject());
#else
  auto* surface = gtk_widget_get_window(GTK_WIDGET(window.GetNativeObject()));
  return surface ? GDK_WINDOW_XID(surface) : 0;
#endif
}

void Destroy(nativeapi::Window& window) {
#ifdef _WIN32
  DestroyWindow(static_cast<HWND>(window.GetNativeObject()));
#else
  gtk_widget_destroy(GTK_WIDGET(window.GetNativeObject()));
#endif
}

// The other process. It prints its window handle, then focuses itself from the
// background the way an app started by a launcher would.
int RunAnchor() {
  nativeapi::Window anchor;
  anchor.SetTitle("nativeapi focus return anchor");
  anchor.SetBounds({60, 420, 420, 260});
  anchor.Show();
  Pump(300);
  anchor.Focus();
  std::cout << "ANCHOR " << Handle(anchor) << std::endl;
  Pump(90000);  // The test kills this process when it is done.
  return 0;
}

struct Anchor {
  unsigned long long handle = 0;
#ifdef _WIN32
  PROCESS_INFORMATION process = {};
  HANDLE output = nullptr;
#else
  pid_t pid = 0;
  int output = -1;
#endif
  bool Start(const char* self);
  void Stop();
};

#ifdef _WIN32
bool Anchor::Start(const char*) {
  SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
  HANDLE write = nullptr;
  if (!CreatePipe(&output, &write, &inherit, 0)) return false;
  SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring command = L"\"" + std::wstring(path) + L"\" --anchor";
  STARTUPINFOW startup = {sizeof(startup)};
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write;
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
  CloseHandle(write);
  if (!started) return false;
  std::string line;
  char c = 0;
  DWORD read = 0;
  while (ReadFile(output, &c, 1, &read, nullptr) && read == 1) {
    if (c == '\n') {
      if (line.rfind("ANCHOR ", 0) == 0) {
        handle = std::strtoull(line.c_str() + 7, nullptr, 10);
        return handle != 0;
      }
      line.clear();
    } else if (c != '\r') {
      line += c;
    }
  }
  return false;
}
void Anchor::Stop() {
  if (process.hProcess) {
    TerminateProcess(process.hProcess, 0);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
  }
  if (output) CloseHandle(output);
}
#else
bool Anchor::Start(const char* self) {
  int fds[2];
  if (pipe(fds) != 0) return false;
  pid = fork();
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    execl(self, self, "--anchor", static_cast<char*>(nullptr));
    _exit(127);
  }
  close(fds[1]);
  output = fds[0];
  if (pid < 0) return false;
  std::string line;
  char c = 0;
  while (read(output, &c, 1) == 1) {
    if (c == '\n') {
      if (line.rfind("ANCHOR ", 0) == 0) {
        handle = std::strtoull(line.c_str() + 7, nullptr, 10);
        return handle != 0;
      }
      line.clear();
    } else {
      line += c;
    }
  }
  return false;
}
void Anchor::Stop() {
  if (pid > 0) {
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
  }
  if (output >= 0) close(output);
}
#endif
}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetProcessDPIAware();
#else
  gdk_set_allowed_backends("x11");
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "SKIP no X11 display" << std::endl;
    return 77;
  }
#endif
  nativeapi::Application::GetInstance();
  if (argc > 1 && std::strcmp(argv[1], "--anchor") == 0) return RunAnchor();

  Anchor anchor;
  if (!anchor.Start(argv[0])) {
    std::cout << "FAIL anchor process did not start" << std::endl;
    anchor.Stop();
    return 1;
  }
  const auto anchor_in_front = [&] { return Foreground() == anchor.handle; };
  if (!WaitFor(anchor_in_front, 5000)) {
    // Without the anchor in front nothing below is meaningful; most likely
    // the desktop has no window manager reporting the active window.
    std::cout << "SKIP the anchor process never got the foreground (foreground "
              << Foreground() << ")" << std::endl;
    anchor.Stop();
    return 77;
  }
  Check(true, "another process's Focus() took the foreground");

  nativeapi::Window a, b, c;
  a.SetTitle("nativeapi focus return A");
  b.SetTitle("nativeapi focus return B");
  c.SetTitle("nativeapi focus return C");
  a.SetBounds({520, 120, 300, 200});
  b.SetBounds({860, 120, 300, 200});
  c.SetBounds({1200, 120, 300, 200});

  // Show() may be refused the foreground; Focus() must then still get it.
  a.Show();
  a.Focus();
  Check(WaitFor([&] { return a.IsFocused(); }), "Focus() from the background takes the focus");
  Check(Foreground() == Handle(a), "the focused window is the foreground window");

  b.Show();
  b.Focus();
  Check(WaitFor([&] { return b.IsFocused(); }), "Focus() moves focus to another own window");
  b.Focus();  // A repeat must keep A as B's Blur() target.
  b.Blur();
  Check(WaitFor([&] { return a.IsFocused(); }), "Blur() returns focus to the previous own window");
  Check(b.IsVisible() && !b.IsFocused(), "the blurred window stays visible without focus");

  b.Blur();
  Pump(300);
  Check(a.IsFocused(), "Blur() of an unfocused window moves nothing");

  b.SetFocusable(false);
  b.Focus();
  Pump(300);
  Check(a.IsFocused() && !b.IsFocused(), "Focus() on a non-focusable window does nothing");
  b.SetFocusable(true);

  DescribeForeground("before Blur() to the anchor");
#ifdef _WIN32
  std::cout << "  A's Blur() target: "
            << reinterpret_cast<unsigned long long>(GetPropW(
                   static_cast<HWND>(a.GetNativeObject()), L"NativeAPIBlurReturnWindow"))
            << std::endl;
#endif
  a.Blur();
  Check(WaitFor(anchor_in_front), "Blur() returns focus to the previous process's window");
  DescribeForeground("after Blur() to the anchor");
  Check(!a.IsFocused(), "the blurred window has no focus");

  // From the background again, then fall back when the target is gone.
  a.Focus();
  Check(WaitFor([&] { return a.IsFocused(); }), "Focus() from the background again");
  c.Show();
  c.Focus();
  Check(WaitFor([&] { return c.IsFocused(); }), "focus a third window");
  Destroy(a);
  Pump(200);
  if (!c.IsFocused()) c.Focus();  // Some window managers move focus on destroy.
  WaitFor([&] { return c.IsFocused(); });
  c.Blur();
  Check(WaitFor([&] { return !c.IsFocused() && Foreground() != Handle(c); }),
        "Blur() with a destroyed previous window still gives up the focus");
  std::cout << "  foreground after fallback: " << Foreground() << " (anchor " << anchor.handle
            << ", B " << Handle(b) << ")" << std::endl;

  c.Minimize();
  Pump(300);
  c.Focus();
  Check(WaitFor([&] { return c.IsFocused() && !c.IsMinimized(); }),
        "Focus() restores and focuses a minimized window");

  Destroy(b);
  Destroy(c);
  Pump(100);
  anchor.Stop();
  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
