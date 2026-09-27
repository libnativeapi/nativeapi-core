// clang-format off
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
// clang-format on
#include <gdiplus.h>

#include <memory>
#include <string>
#include <vector>

#include "../../drag_source_impl.h"
#include "../../foundation/dispatcher.h"
#include "../../image.h"
#include "dpi_utils_windows.h"
#include "drag_drop_utils_windows.h"
#include "string_utils_windows.h"

namespace nativeapi {
namespace {

class DropSourceCom : public IDropSource {
 public:
  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (!object) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *object = static_cast<IDropSource*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG references = InterlockedDecrement(&references_);
    if (references == 0) delete this;
    return references;
  }

  // IDropSource
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape_pressed, DWORD key_state) override {
    if (escape_pressed) return DRAGDROP_S_CANCEL;
    if ((key_state & (MK_LBUTTON | MK_RBUTTON)) == 0) return DRAGDROP_S_DROP;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD /*effect*/) override {
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }

 private:
  LONG references_ = 1;
};

bool SetHGlobalData(IDataObject* data, CLIPFORMAT format, const void* bytes, size_t size) {
  HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
  if (!global) return false;
  if (void* target = GlobalLock(global)) {
    memcpy(target, bytes, size);
    GlobalUnlock(global);
  }
  FORMATETC etc = {format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM medium = {};
  medium.tymed = TYMED_HGLOBAL;
  medium.hGlobal = global;
  if (FAILED(data->SetData(&etc, &medium, TRUE))) {
    GlobalFree(global);
    return false;
  }
  return true;
}

bool SetDataFilePaths(IDataObject* data, const std::vector<std::string>& paths) {
  // DROPFILES followed by NUL-separated wide paths and a final NUL.
  std::wstring list;
  for (const auto& path : paths) {
    std::wstring wide = StringToWString(path);
    if (wide.empty()) continue;
    list += wide;
    list.push_back(L'\0');
  }
  if (list.empty()) return false;
  list.push_back(L'\0');
  std::vector<char> buffer(sizeof(DROPFILES) + list.size() * sizeof(wchar_t));
  auto* header = reinterpret_cast<DROPFILES*>(buffer.data());
  header->pFiles = sizeof(DROPFILES);
  header->fWide = TRUE;
  memcpy(buffer.data() + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));
  return SetHGlobalData(data, CF_HDROP, buffer.data(), buffer.size());
}

bool SetDataText(IDataObject* data, const std::string& text) {
  std::wstring wide = StringToWString(text);
  return SetHGlobalData(data, CF_UNICODETEXT, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
}

void SetDragImage(IDataObject* data, Image& image) {
  auto* bitmap = static_cast<Gdiplus::Bitmap*>(image.GetNativeObject());
  if (!bitmap) return;
  HBITMAP hbitmap = nullptr;
  if (bitmap->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbitmap) != Gdiplus::Ok) return;
  IDragSourceHelper* helper = nullptr;
  if (FAILED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&helper)))) {
    DeleteObject(hbitmap);
    return;
  }
  SHDRAGIMAGE drag_image = {};
  drag_image.sizeDragImage.cx = static_cast<LONG>(bitmap->GetWidth());
  drag_image.sizeDragImage.cy = static_cast<LONG>(bitmap->GetHeight());
  drag_image.ptOffset.x = drag_image.sizeDragImage.cx / 2;
  drag_image.ptOffset.y = drag_image.sizeDragImage.cy / 2;
  drag_image.hbmpDragImage = hbitmap;
  drag_image.crColorKey = CLR_NONE;
  // The helper owns the bitmap once this succeeds.
  if (FAILED(helper->InitializeFromBitmap(&drag_image, data))) {
    DeleteObject(hbitmap);
  }
  helper->Release();
}

bool PrimaryButtonDown() {
  const int primary = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
  return (GetAsyncKeyState(primary) & 0x8000) != 0;
}

Point CursorPosition() {
  POINT cursor = {0, 0};
  GetCursorPos(&cursor);
  return PhysicalToLogicalPoint(cursor);
}

}  // namespace

struct DragSource::Impl::Platform {
  // Cleared when the source is destroyed; the posted drag checks it.
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);

  ~Platform() { *alive = false; }

  static void Run(Impl* impl, std::weak_ptr<bool> alive_token) {
    auto alive = alive_token.lock();
    if (!alive || !*alive || !impl->dragging) return;

    HWND hwnd = impl->window ? static_cast<HWND>(impl->window->GetNativeObject()) : nullptr;
    // The button may have been released before this turn of the message loop.
    if (!hwnd || !IsWindow(hwnd) || !PrimaryButtonDown()) {
      impl->Finish(CursorPosition(), DragOperation::None);
      return;
    }

    DWORD effect = DROPEFFECT_NONE;
    HRESULT result = E_FAIL;
    // The window that got the mouse-down; it will not see the mouse-up.
    POINT cursor = {0, 0};
    GetCursorPos(&cursor);
    HWND pressed = WindowFromPoint(cursor);
    if (pressed != hwnd && !IsChild(hwnd, pressed)) pressed = nullptr;
    {
      ScopedOleInitialize ole;
      IDataObject* data = nullptr;
      if (ole.Succeeded() &&
          SUCCEEDED(SHCreateDataObject(nullptr, 0, nullptr, nullptr, IID_PPV_ARGS(&data)))) {
        bool has_data = false;
        if (!impl->file_paths.empty()) has_data |= SetDataFilePaths(data, impl->file_paths);
        if (impl->text.has_value()) has_data |= SetDataText(data, *impl->text);
        if (has_data) {
          if (impl->image) SetDragImage(data, *impl->image);
          auto* source = new DropSourceCom();
          // Runs a modal loop until the drop; listeners may run meanwhile.
          result = DoDragDrop(data, source, ToDropEffect(impl->operation), &effect);
          source->Release();
        }
        data->Release();
      }
    }

    if (pressed && IsWindow(pressed)) {
      POINT client = {0, 0};
      GetCursorPos(&client);
      ScreenToClient(pressed, &client);
      PostMessageW(pressed, WM_LBUTTONUP, 0, MAKELPARAM(client.x, client.y));
    }
    if (!*alive) return;
    impl->Finish(CursorPosition(),
                 result == DRAGDROP_S_DROP ? FromDropEffect(effect) : DragOperation::None);
  }
};

bool DragSource::IsSupported() {
  return true;
}

DragSource::Impl::Impl(DragSource* owner)
    : owner(owner), platform(std::make_unique<Platform>()) {}

DragSource::Impl::~Impl() = default;

bool DragSource::Impl::Start() {
  if (!PrimaryButtonDown()) {
    return false;
  }
  // DoDragDrop blocks until the drop. Running it from the message loop lets
  // StartDragging() return first, as on the other platforms, and keeps the
  // caller (a Flutter pointer handler, say) out of the modal loop.
  Impl* impl = this;
  std::weak_ptr<bool> alive = platform->alive;
  return RunOnMainThread([impl, alive]() { Platform::Run(impl, alive); });
}

}  // namespace nativeapi
