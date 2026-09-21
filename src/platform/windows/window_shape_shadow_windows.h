#pragma once

// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on
#include "dpi_utils_windows.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>
#include "../window_shadow_blur.h"

namespace nativeapi::shape_shadow {
constexpr wchar_t kConfig[] = L"NativeAPICustomShadow";
constexpr wchar_t kState[] = L"NativeAPIShapeShadow";
constexpr wchar_t kClass[] = L"NativeAPIShapeShadowWindow";
constexpr UINT_PTR kRasterTimer = 1;

// Jobs contain owned snapshots only. Workers never access HWNDs or State; closing
// a window can discard its job without waiting for the raster or dangling data.
struct RasterJob {
  HRGN region = nullptr;
  int width = 0, height = 0, margin = 0;
  double scale = 1;
  WindowShadow options;
  std::vector<DWORD> pixels;
  std::atomic<bool> ready{false};
  ~RasterJob() {
    if (region)
      DeleteObject(region);
  }
};
struct State {
  HWND layer = nullptr;
  HWND target = nullptr;
  int margin = 0;
  bool timer_running = false;
  std::shared_ptr<RasterJob> running;
  std::shared_ptr<RasterJob> pending;
};

inline void Position(HWND target, State* state) {
  if (!IsWindowVisible(target) || IsIconic(target)) {
    ShowWindow(state->layer, SW_HIDE);
    return;
  }
  RECT rect{};
  GetWindowRect(target, &rect);
  RECT current{};
  if (IsWindowVisible(state->layer) && GetWindowRect(state->layer, &current) &&
      current.left == rect.left - state->margin && current.top == rect.top - state->margin &&
      GetWindow(target, GW_HWNDNEXT) == state->layer)
    return;
  SetWindowPos(state->layer, target, rect.left - state->margin, rect.top - state->margin, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

inline void Render(RasterJob& job) {
  const size_t count = static_cast<size_t>(job.width) * job.height;
  std::vector<unsigned char> alpha(count);
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc)
    return;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = job.width;
  info.bmiHeader.biHeight = -job.height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap) {
    DeleteDC(dc);
    return;
  }
  auto old = SelectObject(dc, bitmap);
  auto* rgba = static_cast<DWORD*>(bits);
  std::fill(rgba, rgba + count, 0);
  const auto offset = job.options.GetOffset();
  OffsetRgn(job.region, job.margin + static_cast<int>(std::lround(offset.x * job.scale)),
            job.margin + static_cast<int>(std::lround(offset.y * job.scale)));
  FillRgn(dc, job.region, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  GdiFlush();
  for (size_t i = 0; i < count; ++i)
    alpha[i] = rgba[i] & 0xff;
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  window_shadow::Blur(alpha, job.width, job.height, window_shadow::Radius(job.options, job.scale));
  job.pixels.resize(count);
  const auto color = job.options.GetColor();
  for (size_t i = 0; i < count; ++i)
    job.pixels[i] = window_shadow::Pixel(alpha[i], color);
}

inline DWORD WINAPI RasterWorker(void* data) {
  std::unique_ptr<std::shared_ptr<RasterJob>> owner(static_cast<std::shared_ptr<RasterJob>*>(data));
  auto& job = **owner;
  try {
    Render(job);
  } catch (...) {
    job.pixels.clear();
  }
  job.ready.store(true, std::memory_order_release);
  return 0;
}

inline bool Publish(State* state, const RasterJob& job) {
  if (job.pixels.empty())
    return false;
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc)
    return false;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = job.width;
  info.bmiHeader.biHeight = -job.height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap) {
    DeleteDC(dc);
    return false;
  }
  auto old = SelectObject(dc, bitmap);
  std::memcpy(bits, job.pixels.data(), job.pixels.size() * sizeof(DWORD));
  RECT bounds{};
  GetWindowRect(state->target, &bounds);
  POINT destination{bounds.left - job.margin, bounds.top - job.margin};
  POINT source{};
  SIZE size{job.width, job.height};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  const bool ok = UpdateLayeredWindow(state->layer, nullptr, &destination, &size, dc, &source, 0,
                                      &blend, ULW_ALPHA) != FALSE;
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  if (ok) {
    state->margin = job.margin;
    Position(state->target, state);
  }
  return ok;
}

inline void Pump(State* state) {
  if (state->running && state->running->ready.load(std::memory_order_acquire)) {
    if (!Publish(state, *state->running))
      ShowWindow(state->layer, SW_HIDE);
    state->running.reset();
  }
  if (!state->running && state->pending) {
    state->running = std::move(state->pending);
    auto* work = new std::shared_ptr<RasterJob>(state->running);
    if (!QueueUserWorkItem(RasterWorker, work, WT_EXECUTEDEFAULT)) {
      delete work;
      state->running.reset();
    }
  }
  if (!state->running && !state->pending) {
    KillTimer(state->layer, kRasterTimer);
    state->timer_running = false;
  }
}

inline LRESULT CALLBACK LayerProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  if (message == WM_NCHITTEST)
    return HTTRANSPARENT;
  if (message == WM_MOUSEACTIVATE)
    return MA_NOACTIVATE;
  if (message == WM_TIMER && wp == kRasterTimer) {
    if (auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
      Pump(state);
    return 0;
  }
  return DefWindowProcW(hwnd, message, wp, lp);
}

inline void Update(HWND hwnd, HRGN region, double scale);
inline void Clear(HWND hwnd);
inline void Refresh(HWND hwnd) {
  HRGN region = CreateRectRgn(0, 0, 0, 0);
  if (!region)
    return;
  if (GetWindowRgn(hwnd, region) == ERROR) {
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    SetRectRgn(region, 0, 0, rect.right - rect.left, rect.bottom - rect.top);
  }
  Update(hwnd, region, GetScaleFactorForWindow(hwnd));
  DeleteObject(region);
}

inline LRESULT CALLBACK
Follow(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
  if (message == WM_NCDESTROY) {
    Clear(hwnd);
    return DefSubclassProc(hwnd, message, wp, lp);
  }
  const LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
  auto* state = reinterpret_cast<State*>(GetPropW(hwnd, kState));
  if (!state)
    return result;
  if ((message == WM_SIZE || message == WM_DPICHANGED) && !IsIconic(hwnd))
    Refresh(hwnd);
  else if (message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW)
    Position(hwnd, state);
  return result;
}

inline void Clear(HWND hwnd) {
  auto* state = reinterpret_cast<State*>(RemovePropW(hwnd, kState));
  if (!state)
    return;
  RemoveWindowSubclass(hwnd, Follow, reinterpret_cast<UINT_PTR>(&Follow));
  SetWindowLongPtrW(state->layer, GWLP_USERDATA, 0);
  DestroyWindow(state->layer);
  delete state;
}

inline void Update(HWND hwnd, HRGN region, double scale) {
  RECT bounds{};
  if (!GetWindowRect(hwnd, &bounds))
    return;
  const auto* custom = static_cast<WindowShadow*>(GetPropW(hwnd, kConfig));
  const WindowShadow options = custom ? *custom : WindowShadow{};
  const int margin = window_shadow::Margin(options, scale);
  const int width = bounds.right - bounds.left + margin * 2;
  const int height = bounds.bottom - bounds.top + margin * 2;
  if (width <= 0 || height <= 0 || static_cast<int64_t>(width) * height > 16000000) {
    Clear(hwnd);
    return;
  }
  auto* state = reinterpret_cast<State*>(GetPropW(hwnd, kState));
  if (!state) {
    static const ATOM atom = [] {
      WNDCLASSW cls{};
      cls.lpfnWndProc = LayerProc;
      cls.hInstance = GetModuleHandleW(nullptr);
      cls.lpszClassName = kClass;
      return RegisterClassW(&cls);
    }();
    if (!atom)
      return;
    HWND layer = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!layer)
      return;
    state = new State{};
    state->layer = layer;
    state->target = hwnd;
    state->margin = margin;
    if (!SetPropW(hwnd, kState, reinterpret_cast<HANDLE>(state)) ||
        !SetWindowSubclass(hwnd, Follow, reinterpret_cast<UINT_PTR>(&Follow),
                           reinterpret_cast<DWORD_PTR>(state))) {
      RemovePropW(hwnd, kState);
      DestroyWindow(layer);
      delete state;
      return;
    }
    SetWindowLongPtrW(layer, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  auto job = std::make_shared<RasterJob>();
  job->region = CreateRectRgn(0, 0, 0, 0);
  if (!job->region || CombineRgn(job->region, region, nullptr, RGN_COPY) == ERROR)
    return;
  job->width = width;
  job->height = height;
  job->margin = margin;
  job->scale = scale;
  job->options = options;
  // WM_SIZE and SetShape can both arrive in the same frame. Only the newest
  // snapshot waits behind the active raster; the UI never performs the blur.
  state->pending = std::move(job);
  if (!state->timer_running) {
    state->timer_running = SetTimer(state->layer, kRasterTimer, 8, nullptr) != 0;
  }
}
}  // namespace nativeapi::shape_shadow
