// Bounded idle shutdown and private wake-up delivery, no keyboard input.
// Requires X11; registered with CTest in an isolated Xvfb test run.
#include "nativeapi.h"
#include <X11/Xlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
  const char* isolated = std::getenv("NATIVEAPI_TEST_PRIVATE_X11");
  if (!isolated || std::string(isolated) != "1") return 77;
  if (argc > 1) {
    auto& manager = nativeapi::ShortcutManager::GetInstance();
    auto shortcut = manager.Register("Ctrl+Alt+Shift+F9", [] {});
    if (!shortcut) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    std::cout << "child returning with an idle shortcut worker\n";
    return 0;  // Singleton destruction must wake and join the worker.
  }
  XInitThreads();
  ::Display* observer = XOpenDisplay(nullptr);
  if (!observer) return 77;
  const ::Window root = DefaultRootWindow(observer);
  const Atom exit_atom = XInternAtom(observer, "NATIVEAPI_SHORTCUT_EXIT", False);
  XSelectInput(observer, root, KeyPressMask);
  XSync(observer, False);
  // A second nativeapi client must not receive the first client's stop message.
  auto& manager = nativeapi::ShortcutManager::GetInstance();
  if (!manager.Register("Ctrl+Alt+Shift+F10", [] {})) return 1;
  const pid_t child = fork();
  if (child < 0) return 1;
  if (child == 0) { execl(argv[0], argv[0], "--child", nullptr); _exit(127); }
  int status = 0;
  bool exited = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    if (waitpid(child, &status, WNOHANG) == child) { exited = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!exited) { kill(child, SIGKILL); waitpid(child, &status, 0); }
  XSync(observer, False);
  bool broadcast = false;
  while (XPending(observer)) {
    XEvent event;
    XNextEvent(observer, &event);
    if (event.type == ClientMessage && event.xclient.message_type == exit_atom) broadcast = true;
  }
  XCloseDisplay(observer);
  if (!exited || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || broadcast) {
    std::cerr << "FAIL idle worker failed to stop, or wake-up reached another client\n";
    return 1;
  }
  manager.UnregisterAll();
  std::cout << "PASS idle shutdown and non-broadcast wake-up; parent also exits idle\n";
}
