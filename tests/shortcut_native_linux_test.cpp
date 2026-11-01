// OS registration checks only; no keyboard input is generated.
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>

#include "../src/shortcut_manager.h"

namespace {
int failures = 0;
bool conflict = false;
void Check(bool value, const char* label) {
  std::cout << (value ? "PASS: " : "FAIL: ") << label << '\n';
  if (!value) ++failures;
}
int ProbeError(Display*, XErrorEvent*) { conflict = true; return 0; }
bool Taken(Display* display, KeyCode key, unsigned int modifiers) {
  XSync(display, False);
  conflict = false;
  auto previous = XSetErrorHandler(ProbeError);
  XGrabKey(display, key, modifiers, DefaultRootWindow(display), False,
           GrabModeAsync, GrabModeAsync);
  XSync(display, False);
  XSetErrorHandler(previous);
  if (!conflict) XUngrabKey(display, key, modifiers, DefaultRootWindow(display));
  XSync(display, False);
  return conflict;
}
}  // namespace

int main() {
  if (!std::getenv("DISPLAY")) return 77;
  int ready[2], release[2];
  if (pipe(ready) || pipe(release)) return EXIT_FAILURE;
  auto child = fork();
  if (child < 0) return EXIT_FAILURE;
  if (child == 0) {
    close(ready[0]); close(release[1]);
    auto display = XOpenDisplay(nullptr);
    if (!display) _exit(1);
    auto key = XKeysymToKeycode(display, XK_F11);
    XSetErrorHandler(ProbeError);
    XGrabKey(display, key, ControlMask | Mod1Mask | ShiftMask,
             DefaultRootWindow(display), False, GrabModeAsync, GrabModeAsync);
    XSync(display, False);
    char status = conflict ? '0' : '1';
    auto sent = write(ready[1], &status, 1);
    (void)sent;
    if (read(release[0], &status, 1) != 1) _exit(1);
    XCloseDisplay(display);
    _exit(0);
  }
  close(ready[1]); close(release[0]);
  char status = '0';
  bool owns = read(ready[0], &status, 1) == 1 && status == '1';
  auto& manager = nativeapi::ShortcutManager::GetInstance();
  if (owns) {
    Check(!manager.Register("Ctrl+Alt+Shift+NativeKey65480", [] {}),
          "an external grab returns failure without terminating Xlib");
  }
  status = 'x';
  auto sent = write(release[1], &status, 1);
  (void)sent;
  close(release[1]); close(ready[0]);
  int exit_status = 0;
  waitpid(child, &exit_status, 0);
  if (!owns) {
    std::cout << "SKIP: probe chord unavailable\n";
    return 77;
  }
  Check(WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0, "probe process exits");
  auto raw = manager.Register("Ctrl+Alt+Shift+NativeKey65480", [] {});
  Check(raw != nullptr, "native key registers after the external owner releases it");
  Check(!manager.Register("Ctrl+Alt+Shift+F11", [] {}), "named/native aliases cannot steal a grab");
  if (raw) Check(manager.Unregister(raw->GetId()), "unregister native key");
  auto named = manager.Register("Ctrl+Alt+Shift+F11", [] {});
  Check(named != nullptr, "partial failed grabs were rolled back");
  auto locked = manager.Register("Ctrl+Alt+Shift+CapsLock+NativeKey65480", [] {});
  Check(locked != nullptr, "CapsLock can be explicitly required");
  if (locked) Check(manager.Unregister(locked->GetId()), "release explicit CapsLock shortcut");
  auto probe = XOpenDisplay(nullptr);
  Check(probe != nullptr, "open independent X11 probe connection");
  if (probe) {
    auto key = XKeysymToKeycode(probe, XK_F11);
    auto mods = ControlMask | Mod1Mask | ShiftMask | LockMask;
    Check(Taken(probe, key, mods), "removing CapsLock binding preserves the normal binding's lock variants");
    if (named) manager.Unregister(named->GetId());
    Check(!Taken(probe, key, mods), "unregister releases the OS grab");
    XCloseDisplay(probe);
  }
  manager.UnregisterAll();
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
