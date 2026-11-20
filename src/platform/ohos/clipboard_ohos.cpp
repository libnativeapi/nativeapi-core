#include <cstring>
#include "../../clipboard_impl.h"
#include "ohos_file_uri.h"
#include "ohos_image.h"
#include "ohos_runtime.h"

#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include <database/udmf/udmf.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/udmf_meta.h>
#include <database/udmf/uds.h>
#include <atomic>

namespace nativeapi {
namespace {
using Pasteboard = std::unique_ptr<OH_Pasteboard, decltype(&OH_Pasteboard_Destroy)>;
using Data = std::unique_ptr<OH_UdmfData, decltype(&OH_UdmfData_Destroy)>;
using Record = std::unique_ptr<OH_UdmfRecord, decltype(&OH_UdmfRecord_Destroy)>;
using Text = std::unique_ptr<OH_UdsPlainText, decltype(&OH_UdsPlainText_Destroy)>;
using Html = std::unique_ptr<OH_UdsHtml, decltype(&OH_UdsHtml_Destroy)>;
using Pixel = std::unique_ptr<OH_UdsPixelMap, decltype(&OH_UdsPixelMap_Destroy)>;
using File = std::unique_ptr<OH_UdsFileUri, decltype(&OH_UdsFileUri_Destroy)>;
bool RecordHasType(OH_UdmfRecord* record, const char* type) {
  unsigned count = 0;
  char** types = OH_UdmfRecord_GetTypes(record, &count);
  for (unsigned i = 0; types && i < count; ++i)
    if (types[i] && std::strcmp(types[i], type) == 0)
      return true;
  return false;
}
std::shared_ptr<Image> ReadImage(OH_UdmfRecord* record) {
  Pixel pixels(OH_UdsPixelMap_Create(), OH_UdsPixelMap_Destroy);
  OH_Pixelmap_InitializationOptions* options = nullptr;
  OH_PixelmapInitializationOptions_Create(&options);
  std::unique_ptr<OH_Pixelmap_InitializationOptions,
                  decltype(&OH_PixelmapInitializationOptions_Release)>
      owned_options(options, OH_PixelmapInitializationOptions_Release);
  if (!pixels || !options)
    return nullptr;
  OH_PixelmapInitializationOptions_SetWidth(options, 1);
  OH_PixelmapInitializationOptions_SetHeight(options, 1);
  OH_PixelmapInitializationOptions_SetPixelFormat(options, PIXEL_FORMAT_RGBA_8888);
  OH_PixelmapNative* raw = nullptr;
  OH_PixelmapNative_CreateEmptyPixelmap(options, &raw);
  std::unique_ptr<OH_PixelmapNative, void (*)(OH_PixelmapNative*)> map(
      raw, [](OH_PixelmapNative* value) { OH_PixelmapNative_Destroy(&value); });
  if (!map || OH_UdmfRecord_GetPixelMap(record, pixels.get()) != UDMF_E_OK)
    return nullptr;
  OH_UdsPixelMap_GetPixelMap(pixels.get(), map.get());
  // Materialize independent pixels: clipboard buffers can expire after the read.
  return Image::FromBase64(ohos::EncodePixelmapPng(map.get()));
}

bool ReadData(unsigned mask, uint32_t revision, ClipboardData& result) {
  Pasteboard board(OH_Pasteboard_Create(), OH_Pasteboard_Destroy);
  if (!board)
    return false;
  if (revision != OH_Pasteboard_GetChangeCount(board.get()))
    return false;
  if (!OH_Pasteboard_HasData(board.get()))
    return revision == OH_Pasteboard_GetChangeCount(board.get());
  int status = ERR_INNER_ERROR;
  Data data(OH_Pasteboard_GetData(board.get(), &status), OH_UdmfData_Destroy);
  if (status != ERR_OK || !data)
    return false;
  if ((mask & (1u << static_cast<unsigned>(ClipboardReadFormat::Text))) &&
      OH_UdmfData_HasType(data.get(), UDMF_META_PLAIN_TEXT)) {
    Text text(OH_UdsPlainText_Create(), OH_UdsPlainText_Destroy);
    if (!text || OH_UdmfData_GetPrimaryPlainText(data.get(), text.get()) != UDMF_E_OK)
      return false;
    const char* content = OH_UdsPlainText_GetContent(text.get());
    if (!content)
      return false;
    result.text = content;
  }
  if ((mask & (1u << static_cast<unsigned>(ClipboardReadFormat::Html))) &&
      OH_UdmfData_HasType(data.get(), UDMF_META_HTML)) {
    Html html(OH_UdsHtml_Create(), OH_UdsHtml_Destroy);
    if (!html || OH_UdmfData_GetPrimaryHtml(data.get(), html.get()) != UDMF_E_OK)
      return false;
    const char* content = OH_UdsHtml_GetContent(html.get());
    if (!content)
      return false;
    result.html = content;
  }
  const bool want_image = mask & (1u << static_cast<unsigned>(ClipboardReadFormat::Image));
  const bool want_files = mask & (1u << static_cast<unsigned>(ClipboardReadFormat::FilePaths));
  if (want_image || want_files) {
    unsigned count = 0;
    OH_UdmfRecord** records = OH_UdmfData_GetRecords(data.get(), &count);
    if (count && !records)
      return false;
    for (unsigned i = 0; i < count; ++i) {
      if (want_image && !result.image &&
          RecordHasType(records[i], UDMF_META_OPENHARMONY_PIXEL_MAP)) {
        result.image = ReadImage(records[i]);
        if (!result.image)
          return false;
      }
      if (want_files && RecordHasType(records[i], UDMF_META_GENERAL_FILE_URI)) {
        File file(OH_UdsFileUri_Create(), OH_UdsFileUri_Destroy);
        if (!file || OH_UdmfRecord_GetFileUri(records[i], file.get()) != UDMF_E_OK)
          return false;
        const char* uri = OH_UdsFileUri_GetFileUri(file.get());
        std::string path;
        if (uri && ohos::UriToLocalPath(uri, path))
          result.file_paths.push_back(std::move(path));
      }
    }
  }
  return clipboard_internal::ValidData(result) &&
         revision == OH_Pasteboard_GetChangeCount(board.get());
}

bool WriteData(const ClipboardData& value) {
  // Validate all requested formats before replacing the existing clipboard.
  struct FileValue {
    std::string uri;
    bool directory = false;
  };
  std::vector<FileValue> files;
  for (const auto& path : value.file_paths) {
    FileValue file;
    if (!ohos::LocalPathToUri(path, file.uri, file.directory))
      return false;
    files.push_back(std::move(file));
  }
  Pasteboard board(OH_Pasteboard_Create(), OH_Pasteboard_Destroy);
  if (!board)
    return false;
  if (!value.text && !value.html && !value.image && files.empty())
    return OH_Pasteboard_ClearData(board.get()) == ERR_OK;
  Data data(OH_UdmfData_Create(), OH_UdmfData_Destroy);
  Record record(OH_UdmfRecord_Create(), OH_UdmfRecord_Destroy);
  if (!data || !record)
    return false;
  if (value.text) {
    Text text(OH_UdsPlainText_Create(), OH_UdsPlainText_Destroy);
    if (!text || OH_UdsPlainText_SetContent(text.get(), value.text->c_str()) != UDMF_E_OK ||
        OH_UdmfRecord_AddPlainText(record.get(), text.get()) != UDMF_E_OK)
      return false;
  }
  if (value.html) {
    Html html(OH_UdsHtml_Create(), OH_UdsHtml_Destroy);
    if (!html || OH_UdsHtml_SetContent(html.get(), value.html->c_str()) != UDMF_E_OK ||
        OH_UdmfRecord_AddHtml(record.get(), html.get()) != UDMF_E_OK)
      return false;
  }
  if (value.image) {
    Pixel pixels(OH_UdsPixelMap_Create(), OH_UdsPixelMap_Destroy);
    auto* map = static_cast<OH_PixelmapNative*>(value.image->GetNativeObject());
    if (!pixels || !map || OH_UdsPixelMap_SetPixelMap(pixels.get(), map) != UDMF_E_OK ||
        OH_UdmfRecord_AddPixelMap(record.get(), pixels.get()) != UDMF_E_OK)
      return false;
  }
  if ((value.text || value.html || value.image) &&
      OH_UdmfData_AddRecord(data.get(), record.get()) != UDMF_E_OK)
    return false;
  for (const auto& entry : files) {
    File file(OH_UdsFileUri_Create(), OH_UdsFileUri_Destroy);
    Record file_record(OH_UdmfRecord_Create(), OH_UdmfRecord_Destroy);
    if (!file || !file_record ||
        OH_UdsFileUri_SetFileUri(file.get(), entry.uri.c_str()) != UDMF_E_OK ||
        OH_UdsFileUri_SetFileType(
            file.get(), entry.directory ? UDMF_META_FOLDER : UDMF_META_GENERAL_FILE) != UDMF_E_OK ||
        OH_UdmfRecord_AddFileUri(file_record.get(), file.get()) != UDMF_E_OK ||
        OH_UdmfData_AddRecord(data.get(), file_record.get()) != UDMF_E_OK)
      return false;
  }
  return OH_Pasteboard_SetData(board.get(), data.get()) == ERR_OK;
}
}  // namespace

struct Clipboard::Impl::Platform {
  struct Listener {
    std::atomic<bool> active{true};
    uint64_t generation = 0;
    std::function<void()> deliver;
  };
  std::shared_ptr<Listener> listener;
  Pasteboard board{nullptr, OH_Pasteboard_Destroy};
  OH_PasteboardObserver* observer = nullptr;
  uint64_t cleanup = 0;
};
Clipboard::Impl::Impl(Clipboard* owner) : owner(owner), platform(std::make_unique<Platform>()) {}
Clipboard::Impl::~Impl() {
  Stop();
}
bool Clipboard::IsSupported() {
  return ohos::IsInitialized();
}
bool Clipboard::IsChangeMonitoringSupported() {
  return ohos::IsInitialized();
}

void Clipboard::Impl::Read(unsigned mask, std::function<void(bool, ClipboardData)> callback) {
  // Capture only public revision metadata on the caller's thread. Queuing must
  // not move the start of a read past a subsequent write on the same UI turn.
  const auto generation = ohos::EnvironmentGeneration();
  Pasteboard board(generation ? OH_Pasteboard_Create() : nullptr, OH_Pasteboard_Destroy);
  const auto revision = board ? OH_Pasteboard_GetChangeCount(board.get()) : 0;
  const bool ready = board != nullptr;
  auto completion = std::make_shared<std::function<void(bool, ClipboardData)>>(std::move(callback));
  if (!ohos::Post([mask, revision, ready, generation, completion] {
        // Work from an aborted environment must never enter a replacement host.
        if (generation != ohos::EnvironmentGeneration()) return;
        ClipboardData data;
        bool ok = false;
        try {
          ok = ready && ReadData(mask, revision, data);
        } catch (...) {
        }
        (*completion)(ok, ok ? std::move(data) : ClipboardData{});
      }) && generation == ohos::EnvironmentGeneration())
    (*completion)(false, {});
}

bool Clipboard::Impl::Write(const ClipboardData& data) {
  bool ok = false;
  return ohos::RunSync([&] { ok = WriteData(data); }) && ok;
}
void Clipboard::Impl::ReadCustomFormatBase64(
    const std::string&,
    std::function<void(bool, std::optional<std::string>)> callback) {
  if (callback)
    callback(false, std::nullopt);
}
bool Clipboard::Impl::WriteCustomFormatBase64(const std::string&, const std::string&) {
  return false;
}
int64_t Clipboard::Impl::GetChangeCount() const {
  return 0;
}
bool Clipboard::Impl::IsConcealed() const {
  return false;
}
bool Clipboard::Impl::HasFormat(ClipboardReadFormat) const {
  return false;
}
bool Clipboard::Impl::HasCustomFormat(const std::string&) const {
  return false;
}
bool Clipboard::Impl::Start() {
  if (!ohos::IsInitialized())
    return false;
  platform->board.reset(OH_Pasteboard_Create());
  platform->observer = OH_PasteboardObserver_Create();
  if (!platform->board || !platform->observer) {
    Stop();
    return false;
  }
  platform->listener = std::make_shared<Platform::Listener>();
  platform->listener->generation = ohos::EnvironmentGeneration();
  platform->listener->deliver = owner->CreateGuardedCallback<>(
      std::function<void()>([this] { owner->Emit<ClipboardChangedEvent>(); }));
  platform->cleanup = ohos::AddEnvironmentCleanup(
      platform->listener->generation, owner->CreateGuardedCallback<>(
          std::function<void()>([this] { owner->RemoveAllListeners(); Stop(); })));
  if (!platform->cleanup) {
    Stop();
    return false;
  }
  auto* context = new std::shared_ptr<Platform::Listener>(platform->listener);
  if (OH_PasteboardObserver_SetData(
          platform->observer, context,
          [](void* context, Pasteboard_NotifyType) {
            auto listener = *static_cast<std::shared_ptr<Platform::Listener>*>(context);
            // One guarded UI hop retains this subscription's epoch all the
            // way to delivery. A second unguarded EmitAsync would let an old
            // notice reach replacement listeners after Stop()/Start().
            // Never call users on the SDK IPC thread if dispatch is unavailable.
            RunOnMainThread([listener] {
              if (listener->active.load() &&
                  listener->generation == ohos::EnvironmentGeneration())
                listener->deliver();
            });
          },
          [](void* context) {
            delete static_cast<std::shared_ptr<Platform::Listener>*>(context);
          }) != ERR_OK) {
    delete context;
    Stop();
    return false;
  }
  if (OH_Pasteboard_Subscribe(platform->board.get(), NOTIFY_LOCAL_DATA_CHANGE,
                              platform->observer) != ERR_OK) {
    Stop();
    return false;
  }
  return true;
}
void Clipboard::Impl::Stop() {
  ohos::RemoveEnvironmentCleanup(platform->cleanup);
  platform->cleanup = 0;
  if (platform->listener)
    platform->listener->active.store(false);
  if (platform->observer) {
    if (platform->board)
      OH_Pasteboard_Unsubscribe(platform->board.get(), NOTIFY_LOCAL_DATA_CHANGE,
                                platform->observer);
    OH_PasteboardObserver_Destroy(platform->observer);
    platform->observer = nullptr;
  }
  platform->listener.reset();
  platform->board.reset();
  monitoring = false;
}
}  // namespace nativeapi
