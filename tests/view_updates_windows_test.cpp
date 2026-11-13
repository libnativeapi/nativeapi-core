// Real Win32 geometry, paint scheduling and lifetime checks. No synthetic input.
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>
#include "../src/view_impl.h"
#include "nativeapi.h"

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
  failures += !ok;
}
struct Counts {
  int positions = 0;
  int paints = 0;
  std::function<void()> on_position;
};
LRESULT CALLBACK Watch(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
  auto& count = *reinterpret_cast<Counts*>(data);
  if (msg == WM_WINDOWPOSCHANGED) {
    ++count.positions;
    auto callback = std::move(count.on_position);
    if (callback)
      callback();
  }
  if (msg == WM_PAINT || msg == WM_ERASEBKGND)
    ++count.paints;
  return DefSubclassProc(hwnd, msg, wp, lp);
}
double Milliseconds() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

int main() {
  using namespace nativeapi;
  SetProcessDPIAware();
  Application::GetInstance();
  auto window = std::make_shared<Window>();
  window->SetTitle("NativeAPI view update checks");
  window->SetContentSize({640, 480});
  auto root = window->GetContentView();
  auto column = std::make_shared<View>();
  column->SetFrame({0, 0, 300, 400});
  column->SetLayout(ViewLayout::Column);
  root->AddSubview(column);
  std::vector<std::shared_ptr<Button>> buttons;
  View::BeginUpdates();
  for (int i = 0; i < 96; ++i) {
    auto button = std::make_shared<Button>("Button");
    column->AddSubview(button);
    buttons.push_back(button);
  }
  View::EndUpdates();
  int layouts = 0;
  ViewInternal::Of(*root).host_layout = [&] { ++layouts; };
  auto run = [&](bool batch) {
    layouts = 0;
    double start = Milliseconds();
    if (batch)
      View::BeginUpdates();
    for (size_t i = 0; i < buttons.size(); ++i)
      buttons[i]->SetPreferredSize({100, batch ? 25.0 : 24.0});
    if (batch)
      View::EndUpdates();
    return Milliseconds() - start;
  };
  const double immediate = run(false);
  Check(layouts == 96, "unbatched size changes complete synchronously");
  const double batched = run(true);
  Check(layouts == 1, "96 preferred-size changes coalesce to one layout");
  std::cout << "PROFILE immediate_ms=" << immediate << " batch_ms=" << batched << '\n';
  layouts = 0;
  for (auto& button : buttons) {
    button->SetPreferredSize({100, 25});
    button->SetVisible(true);
    button->SetEnabled(true);
  }
  Check(layouts == 0, "unchanged properties do not relayout");

  auto hwnd = static_cast<HWND>(column->GetNativeObject());
  auto child_hwnd = static_cast<HWND>(buttons[0]->GetNativeObject());
  Counts count, child_count;
  SetWindowSubclass(hwnd, Watch, 100, reinterpret_cast<DWORD_PTR>(&count));
  SetWindowSubclass(child_hwnd, Watch, 100, reinterpret_cast<DWORD_PTR>(&child_count));
  ShowWindow(static_cast<HWND>(window->GetNativeObject()), SW_SHOWNOACTIVATE);
  RedrawWindow(static_cast<HWND>(window->GetNativeObject()), nullptr, nullptr,
               RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  count = {};
  child_count = {};
  layouts = 0;
  View::BeginUpdates();
  View::BeginUpdates();
  for (int i = 1; i <= 100; ++i)
    column->SetFrame({double(i), 0, 300, 400});
  Check(View::IsUpdating() && count.positions == 0, "translations wait inside nested batch");
  Check(column->GetFrame().x == 100, "pending geometry is available to layout");
  View::EndUpdates();
  Check(View::IsUpdating() && count.positions == 0, "inner EndUpdates does not publish");
  View::EndUpdates();
  Check(!View::IsUpdating() && count.positions == 1,
        "outer EndUpdates publishes one final translation");
  Check(layouts == 0 && child_count.positions == 0,
        "absolute translation does not lay out descendants");
  Check(count.paints == 0 && child_count.paints == 0, "publication does not synchronously repaint");
  Check(GetUpdateRect(hwnd, nullptr, FALSE), "publication schedules repaint");
  RedrawWindow(static_cast<HWND>(window->GetNativeObject()), nullptr, nullptr,
               RDW_UPDATENOW | RDW_ALLCHILDREN);
  Check(count.paints > 0 || child_count.paints > 0, "scheduled paint is delivered");

  count = {};
  child_count = {};
  View::BeginUpdates();
  column->SetFrame({200, 0, 300, 400});
  column->SetFrame({100, 0, 300, 400});
  View::EndUpdates();
  Check(count.positions == 0 && !GetUpdateRect(hwnd, nullptr, FALSE),
        "cancelled moves publish neither geometry nor repaint damage");
  View::BeginUpdates();
  column->SetFrame({200, 0, 300, 400});
  column->SetFrame({100, 0, 300, 400});
  buttons[0]->SetVisible(false);
  View::EndUpdates();
  Check(count.positions == 0, "returning to the original frame cancels pending translation");
  Check(!buttons[0]->IsVisible() && !(GetWindowLongPtrW(child_hwnd, GWL_STYLE) & WS_VISIBLE),
        "batch preserves hidden control state");
  View::BeginUpdates();
  auto temporary = std::make_shared<View>();
  temporary->SetFrame({10, 10, 20, 20});
  temporary->SetFrame({30, 30, 20, 20});
  temporary.reset();
  View::EndUpdates();
  View::EndUpdates();
  Check(!View::IsUpdating(), "destroyed pending view and unmatched end are safe");

  auto peer = std::make_shared<View>();
  peer->SetFrame({0, 0, 20, 20});
  root->AddSubview(peer);
  layouts = 0;
  count.on_position = [&] {
    View::BeginUpdates();
    peer->SetFrame({33, 44, 20, 20});
    buttons[1]->SetPreferredSize({100, 26});
    View::EndUpdates();
  };
  View::BeginUpdates();
  column->SetFrame({110, 0, 300, 400});
  View::EndUpdates();
  RECT actual;
  GetWindowRect(static_cast<HWND>(peer->GetNativeObject()), &actual);
  MapWindowPoints(nullptr, static_cast<HWND>(root->GetNativeObject()),
                  reinterpret_cast<POINT*>(&actual), 2);
  const double scale = GetDpiForWindow(static_cast<HWND>(peer->GetNativeObject())) / 96.0;
  Check(actual.left == std::lround(33 * scale) && actual.top == std::lround(44 * scale) &&
            layouts == 1,
        "native position callbacks can enqueue nested updates without losing work");
  RedrawWindow(static_cast<HWND>(root->GetNativeObject()), nullptr, nullptr,
               RDW_VALIDATE | RDW_NOERASE | RDW_ALLCHILDREN);
  View::BeginUpdates();
  column->SetFrame({120, 0, 300, 400});
  View::EndUpdates(false);
  Check(column->GetFrame().x == 120 && !GetUpdateRect(hwnd, nullptr, FALSE),
        "a host renderer can publish geometry without redundant native invalidation");
  View::BeginUpdates();
  peer->SetFrame({-0.5 / scale, 0, 20, 20});
  View::EndUpdates(false);
  const double negative_half = peer->GetFrame().x;
  View::BeginUpdates();
  peer->SetFrame({71.5 / scale, 0, 20, 20});
  View::EndUpdates(false);
  Check(std::abs((peer->GetFrame().x - negative_half) * scale - 72) < 1e-6,
        "pixel snapping preserves integer translations across negative half pixels");

  HWND borrowed_hwnd = CreateWindowExW(0, L"EDIT", L"Borrowed", WS_CHILD | WS_VISIBLE, 0, 0, 80, 30,
                                       static_cast<HWND>(root->GetNativeObject()), nullptr,
                                       GetModuleHandleW(nullptr), nullptr);
  HFONT borrowed_font =
      CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                  CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  SendMessageW(borrowed_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(borrowed_font), FALSE);
  auto borrowed = std::make_shared<View>(borrowed_hwnd);
  borrowed->SetFrame({0, 0, 80, 30});
  root->AddSubview(borrowed);
  Check(reinterpret_cast<HFONT>(SendMessageW(borrowed_hwnd, WM_GETFONT, 0, 0)) == borrowed_font,
        "wrapping and attaching a borrowed control preserve the host font");
  root->RemoveSubview(borrowed);
  borrowed.reset();
  Check(IsWindow(borrowed_hwnd) &&
            reinterpret_cast<HFONT>(SendMessageW(borrowed_hwnd, WM_GETFONT, 0, 0)) == borrowed_font,
        "releasing a borrowed control preserves its window and font");
  DestroyWindow(borrowed_hwnd);
  DeleteObject(borrowed_font);
  RemoveWindowSubclass(hwnd, Watch, 100);
  RemoveWindowSubclass(child_hwnd, Watch, 100);
  ViewInternal::Of(*root).host_layout = {};
  window->Close();
  // Deliberately retain one C handle until static HandleTable teardown. The
  // main thread's update queues can already have been destroyed at this point.
  const auto shutdown_view = native_view_create();
  native_view_set_frame(shutdown_view, {0, 0, 20, 20});
  return failures ? 1 : 0;
}
