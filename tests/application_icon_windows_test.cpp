// leanflutter/window_manager#534 (#78): Application::SetIcon() on Windows
// sets the big and small icon of the application's top-level windows, from a
// UTF-8 path with non-ASCII characters. Windows are not shown; no input.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include "nativeapi.h"

namespace {
int failures = 0;
void Check(bool ok, const char* label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
  failures += !ok;
}
std::string Utf8(const std::wstring& wide) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(size - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr);
  return out;
}
// A 32 x 32, 32 bpp .ico file of one colour.
void WriteIcon(const std::wstring& path, uint32_t bgra) {
  std::vector<uint8_t> f;
  auto u16 = [&](uint16_t v) { f.push_back(v & 0xFF); f.push_back(v >> 8); };
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.push_back((v >> (8 * i)) & 0xFF); };
  const uint32_t pixels = 32 * 32 * 4, mask = 32 * 4, image = 40 + pixels + mask;
  u16(0); u16(1); u16(1);                                   // ICONDIR
  f.push_back(32); f.push_back(32); f.push_back(0); f.push_back(0);
  u16(1); u16(32); u32(image); u32(22);                     // ICONDIRENTRY
  u32(40); u32(32); u32(64); u16(1); u16(32); u32(0);       // BITMAPINFOHEADER
  u32(pixels + mask); u32(0); u32(0); u32(0); u32(0);
  for (int i = 0; i < 32 * 32; ++i) u32(bgra);
  f.insert(f.end(), mask, 0);                               // AND mask: all opaque
  FILE* file = _wfopen(path.c_str(), L"wb");
  fwrite(f.data(), 1, f.size(), file);
  fclose(file);
}
HWND MakeWindow() {
  WNDCLASSW cls = {};
  cls.lpfnWndProc = DefWindowProcW;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"NativeApiIconTest";
  RegisterClassW(&cls);
  return CreateWindowExW(0, cls.lpszClassName, L"nativeapi icon", WS_OVERLAPPEDWINDOW, 100, 100,
                         300, 200, nullptr, nullptr, cls.hInstance, nullptr);
}
HICON Icon(HWND hwnd, WPARAM which) {
  return reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, which, 0));
}
int IconWidth(HICON icon) {
  ICONINFO info = {};
  if (!icon || !GetIconInfo(icon, &info)) return 0;
  BITMAP bitmap = {};
  GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap);
  if (info.hbmColor) DeleteObject(info.hbmColor);
  if (info.hbmMask) DeleteObject(info.hbmMask);
  return bitmap.bmWidth;
}
}  // namespace

int main() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"nativeapi 图标";
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring red = dir + L"\\红色.ico", blue = dir + L"\\蓝色.ico", text = dir + L"\\不是图标.ico";
  WriteIcon(red, 0xFF0000FF);
  WriteIcon(blue, 0xFFFF0000);
  if (FILE* file = _wfopen(text.c_str(), L"wb")) { fputs("not an icon", file); fclose(file); }

  auto& app = nativeapi::Application::GetInstance();
  HWND first = MakeWindow(), second = MakeWindow();
  Check(!Icon(first, ICON_BIG) && !Icon(first, ICON_SMALL), "the windows start without an icon of their own");

  Check(app.SetIcon(Utf8(red)), "SetIcon accepts a .ico at a non-ASCII UTF-8 path");
  for (HWND hwnd : {first, second}) {
    Check(Icon(hwnd, ICON_BIG) && Icon(hwnd, ICON_SMALL), "each window has a big and a small icon");
  }
  Check(IconWidth(Icon(first, ICON_BIG)) == GetSystemMetrics(SM_CXICON) &&
            IconWidth(Icon(first, ICON_SMALL)) == GetSystemMetrics(SM_CXSMICON),
        "at the sizes the system uses");
  const HICON before = Icon(first, ICON_BIG);
  Check(app.SetIcon(Utf8(blue)) && Icon(first, ICON_BIG) && Icon(first, ICON_BIG) != before,
        "a second SetIcon replaces it");
  Check(!app.SetIcon(Utf8(dir + L"\\missing.ico")), "a missing file is refused");
  Check(!app.SetIcon(Utf8(text)), "a file that is no icon is refused");
  Check(!app.SetIcon(""), "an empty path is refused");

  DestroyWindow(first);
  DestroyWindow(second);
  DeleteFileW(red.c_str());
  DeleteFileW(blue.c_str());
  DeleteFileW(text.c_str());
  RemoveDirectoryW(dir.c_str());
  std::cout << (failures ? "FAILED" : "OK") << std::endl;
  return failures ? 1 : 0;
}
