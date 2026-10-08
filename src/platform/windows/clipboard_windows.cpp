#ifndef NOMINMAX
#define NOMINMAX
#endif
// Windows headers must precede GDI+ and shell declarations.
#include <windows.h>

#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../clipboard_impl.h"
#include "string_utils_windows.h"

namespace nativeapi {
namespace {
UINT HtmlFormat() {
  static UINT value = RegisterClipboardFormatW(L"HTML Format");
  return value;
}
UINT PngFormat() {
  static UINT value = RegisterClipboardFormatW(L"PNG");
  return value;
}

std::vector<unsigned char> Bytes(UINT format) {
  HANDLE handle = GetClipboardData(format);
  if (!handle)
    return {};
  const SIZE_T size = GlobalSize(handle);
  const auto* source = static_cast<const unsigned char*>(GlobalLock(handle));
  if (!source)
    return {};
  std::vector<unsigned char> result(source, source + size);
  GlobalUnlock(handle);
  return result;
}

bool Utf8(const std::wstring& value, std::string& result) {
  if (value.empty()) {
    result.clear();
    return true;
  }
  if (value.size() > INT_MAX)
    return false;
  const int length = static_cast<int>(value.size());
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, nullptr,
                                       0, nullptr, nullptr);
  if (!size)
    return false;
  result.resize(size);
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, result.data(),
                             size, nullptr, nullptr) == size;
}

bool Fragment(const std::vector<unsigned char>& bytes, std::string& result) {
  const std::string source(bytes.begin(), bytes.end());
  auto offset = [&](const char* key, size_t& value) {
    const size_t pos = source.find(key);
    if (pos == std::string::npos)
      return false;
    const size_t start = pos + std::strlen(key);
    const size_t end = source.find_first_not_of("0123456789", start);
    if (end == start)
      return false;
    try {
      value = static_cast<size_t>(std::stoull(source.substr(start, end - start)));
    } catch (...) {
      return false;
    }
    return value <= source.size();
  };
  size_t begin, end;
  if (!offset("StartFragment:", begin) || !offset("EndFragment:", end) || begin > end)
    return false;
  result = source.substr(begin, end - begin);
  return clipboard_internal::ValidString(result);
}

std::string Html(const std::string& fragment) {
  const std::string before = "<html><body><!--StartFragment-->";
  const std::string after = "<!--EndFragment--></body></html>";
  const char* pattern =
      "Version:1.0\r\nStartHTML:%010llu\r\nEndHTML:%010llu\r\nStartFragment:%010llu\r\nEndFragment:"
      "%010llu\r\n";
  char header[256];
  const int size = std::snprintf(header, sizeof(header), pattern, 0ull, 0ull, 0ull, 0ull);
  const auto start = static_cast<unsigned long long>(size);
  std::snprintf(header, sizeof(header), pattern, start,
                start + before.size() + fragment.size() + after.size(), start + before.size(),
                start + before.size() + fragment.size());
  return std::string(header) + before + fragment + after;
}

struct NativeData {
  UINT format;
  HGLOBAL handle;
  NativeData(UINT format, const void* bytes, size_t size)
      : format(format), handle(GlobalAlloc(GMEM_MOVEABLE, size)) {
    if (handle) {
      void* target = GlobalLock(handle);
      if (target) {
        std::memcpy(target, bytes, size);
        GlobalUnlock(handle);
      } else {
        GlobalFree(handle);
        handle = nullptr;
      }
    }
  }
  NativeData(NativeData&& other) noexcept : format(other.format), handle(other.handle) {
    other.handle = nullptr;
  }
  ~NativeData() {
    if (handle)
      GlobalFree(handle);
  }
};

std::shared_ptr<Image> DecodeClipboardImage() {
  if (IsClipboardFormatAvailable(PngFormat())) {
    auto png = Bytes(PngFormat());
    if (!png.empty()) {
      if (auto image = Image::FromBase64(clipboard_internal::Base64(png.data(), png.size())))
        return image;
    }
  }
  auto dib = Bytes(IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB);
  if (dib.size() < sizeof(BITMAPINFOHEADER))
    return nullptr;
  BITMAPINFOHEADER info;
  std::memcpy(&info, dib.data(), sizeof(info));
  if (info.biSize < sizeof(info) || info.biSize > dib.size() || info.biBitCount > 32)
    return nullptr;
  size_t offset = info.biSize;
  if (info.biSize == sizeof(info) &&
      (info.biCompression == BI_BITFIELDS || info.biCompression == 6))
    offset += info.biCompression == 6 ? 16 : 12;
  const size_t colors =
      info.biClrUsed ? info.biClrUsed : (info.biBitCount <= 8 ? size_t{1} << info.biBitCount : 0);
  if (colors > (dib.size() - std::min(offset, dib.size())) / sizeof(RGBQUAD))
    return nullptr;
  offset += colors * sizeof(RGBQUAD);
  if (offset > dib.size() ||
      dib.size() > std::numeric_limits<DWORD>::max() - sizeof(BITMAPFILEHEADER))
    return nullptr;
  BITMAPFILEHEADER header{};
  header.bfType = 0x4d42;
  header.bfSize = static_cast<DWORD>(sizeof(header) + dib.size());
  header.bfOffBits = static_cast<DWORD>(sizeof(header) + offset);
  std::vector<unsigned char> bmp(sizeof(header) + dib.size());
  std::memcpy(bmp.data(), &header, sizeof(header));
  std::memcpy(bmp.data() + sizeof(header), dib.data(), dib.size());
  return Image::FromBase64(clipboard_internal::Base64(bmp.data(), bmp.size()));
}
}  // namespace

struct Clipboard::Impl::Platform {
  explicit Platform(Impl* impl) : impl(impl) {}
  Impl* impl;
  HWND window = nullptr;
  DWORD revision = 0;
  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<Platform*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<Platform*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (message == WM_CLIPBOARDUPDATE && self && self->impl->monitoring) {
      DWORD revision = GetClipboardSequenceNumber();
      if (revision != self->revision) {
        self->revision = revision;
        self->impl->Changed();
      }
      return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }
  bool EnsureWindow() {
    if (window)
      return true;
    static const wchar_t* name = L"NativeAPI.Clipboard";
    WNDCLASSW klass{};
    klass.lpfnWndProc = Proc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = name;
    if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return false;
    window =
        CreateWindowExW(0, name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, klass.hInstance, this);
    return window != nullptr;
  }
};

Clipboard::Impl::Impl(Clipboard* owner)
    : owner(owner), platform(std::make_unique<Platform>(this)) {}
Clipboard::Impl::~Impl() {
  Stop();
  if (platform->window)
    DestroyWindow(platform->window);
}
bool Clipboard::IsSupported() {
  return true;
}
bool Clipboard::IsChangeMonitoringSupported() {
  return true;
}
void Clipboard::Impl::Read(unsigned mask, std::function<void(bool, ClipboardData)> callback) {
  const DWORD revision = GetClipboardSequenceNumber();
  RunOnMainThread([mask, revision, callback = std::move(callback)] {
    if (!OpenClipboard(nullptr)) {
      callback(false, {});
      return;
    }
    ClipboardData data;
    bool ok = true;
    try {
      if ((mask & Bit(ClipboardReadFormat::Text)) && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        auto bytes = Bytes(CF_UNICODETEXT);
        std::wstring text;
        bool terminated = false;
        for (size_t i = 0; i + sizeof(wchar_t) <= bytes.size(); i += sizeof(wchar_t)) {
          wchar_t c;
          std::memcpy(&c, bytes.data() + i, sizeof(c));
          if (!c) {
            terminated = true;
            break;
          }
          text += c;
        }
        std::string utf8;
        if (terminated && Utf8(text, utf8))
          data.text = std::move(utf8);
        else
          ok = false;
      }
      if ((mask & Bit(ClipboardReadFormat::Html)) && IsClipboardFormatAvailable(HtmlFormat())) {
        std::string html;
        if (Fragment(Bytes(HtmlFormat()), html))
          data.html = std::move(html);
        else
          ok = false;
      }
      if ((mask & Bit(ClipboardReadFormat::Image)) &&
          (IsClipboardFormatAvailable(PngFormat()) || IsClipboardFormatAvailable(CF_DIBV5) ||
           IsClipboardFormatAvailable(CF_DIB))) {
        data.image = DecodeClipboardImage();
        if (!data.image)
          ok = false;
      }
      if ((mask & Bit(ClipboardReadFormat::FilePaths)) && IsClipboardFormatAvailable(CF_HDROP)) {
        HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        if (!drop)
          ok = false;
        else
          for (UINT i = 0, count = DragQueryFileW(drop, 0xffffffff, nullptr, 0); i < count; ++i) {
            UINT size = DragQueryFileW(drop, i, nullptr, 0);
            std::vector<wchar_t> path(size + 1);
            if (!DragQueryFileW(drop, i, path.data(), size + 1)) {
              ok = false;
              break;
            }
            std::string utf8;
            if (!Utf8(std::wstring(path.data(), size), utf8)) {
              ok = false;
              break;
            }
            data.file_paths.push_back(std::move(utf8));
          }
      }
      ok = ok && revision == GetClipboardSequenceNumber() && clipboard_internal::ValidData(data);
    } catch (...) {
      ok = false;
    }
    CloseClipboard();
    callback(ok, ok ? std::move(data) : ClipboardData{});
  });
}

bool Clipboard::Impl::Write(const ClipboardData& data) {
  std::vector<NativeData> prepared;
  if (data.text) {
    const auto text = StringToWString(*data.text);
    prepared.emplace_back(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
  }
  if (data.html) {
    const auto html = Html(*data.html);
    prepared.emplace_back(HtmlFormat(), html.c_str(), html.size() + 1);
  }
  if (data.image) {
    auto png = clipboard_internal::DecodeBase64(data.image->ToBase64());
    if (png.empty())
      return false;
    prepared.emplace_back(PngFormat(), png.data(), png.size());
    auto* bitmap = static_cast<Gdiplus::Bitmap*>(data.image->GetNativeObject());
    const UINT width = bitmap->GetWidth(), height = bitmap->GetHeight();
    if (!width || !height || width > INT_MAX / 4 || height > INT_MAX ||
        static_cast<uint64_t>(width) * height * 4 >
            std::numeric_limits<size_t>::max() - sizeof(BITMAPV5HEADER))
      return false;
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = static_cast<LONG>(width);
    header.bV5Height = -static_cast<LONG>(height);
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    header.bV5CSType = LCS_sRGB;
    std::vector<unsigned char> dib(sizeof(header) + static_cast<size_t>(width) * height * 4);
    std::memcpy(dib.data(), &header, sizeof(header));
    Gdiplus::Rect rectangle(0, 0, width, height);
    Gdiplus::BitmapData pixels{};
    if (bitmap->LockBits(&rectangle, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &pixels) !=
        Gdiplus::Ok)
      return false;
    for (UINT y = 0; y < height; ++y)
      std::memcpy(
          dib.data() + sizeof(header) + static_cast<size_t>(y) * width * 4,
          static_cast<unsigned char*>(pixels.Scan0) + static_cast<ptrdiff_t>(y) * pixels.Stride,
          width * 4);
    bitmap->UnlockBits(&pixels);
    prepared.emplace_back(CF_DIBV5, dib.data(), dib.size());
  }
  if (!data.file_paths.empty()) {
    std::wstring paths;
    for (const auto& path : data.file_paths) {
      paths += StringToWString(path);
      paths += L'\0';
    }
    paths += L'\0';
    DROPFILES header{};
    header.pFiles = sizeof(header);
    header.fWide = TRUE;
    std::vector<unsigned char> bytes(sizeof(header) + paths.size() * sizeof(wchar_t));
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), paths.data(), paths.size() * sizeof(wchar_t));
    prepared.emplace_back(CF_HDROP, bytes.data(), bytes.size());
    const DWORD effect = DROPEFFECT_COPY;
    prepared.emplace_back(RegisterClipboardFormatW(L"Preferred DropEffect"), &effect,
                          sizeof(effect));
  }
  for (const auto& item : prepared)
    if (!item.format || !item.handle)
      return false;
  if (!platform->EnsureWindow() || !OpenClipboard(platform->window))
    return false;
  bool ok = EmptyClipboard() != FALSE;
  if (ok)
    for (auto& item : prepared) {
      if (SetClipboardData(item.format, item.handle))
        item.handle = nullptr;
      else {
        ok = false;
        break;
      }
    }
  CloseClipboard();
  return ok;
}
bool Clipboard::Impl::Start() {
  if (!platform->EnsureWindow())
    return false;
  platform->revision = GetClipboardSequenceNumber();
  return AddClipboardFormatListener(platform->window) != FALSE;
}
void Clipboard::Impl::Stop() {
  if (monitoring && platform->window)
    RemoveClipboardFormatListener(platform->window);
  monitoring = false;
}
}  // namespace nativeapi
