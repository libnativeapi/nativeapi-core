#include "../../clipboard_impl.h"
#include "android_runtime.h"

#include <stdexcept>

namespace nativeapi {
namespace {
constexpr unsigned Bit(ClipboardReadFormat format) {
  return 1u << static_cast<unsigned>(format);
}
jclass Class(JNIEnv* env, const char* name) {
  auto value = env->FindClass(name);
  if (android::ClearException(env) || !value)
    throw std::runtime_error("Android clipboard class unavailable");
  return value;
}
jmethodID Method(JNIEnv* env, jobject object, const char* name, const char* signature) {
  auto cls = env->GetObjectClass(object);
  auto method = cls ? env->GetMethodID(cls, name, signature) : nullptr;
  if (cls)
    env->DeleteLocalRef(cls);
  if (android::ClearException(env) || !method)
    throw std::runtime_error("Android clipboard method unavailable");
  return method;
}
void Check(JNIEnv* env) {
  if (android::ClearException(env))
    throw std::runtime_error("Android clipboard access failed");
}
jobject Board(JNIEnv* env) {
  auto context = android::Context();
  auto method = Method(env, context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  auto service = android::ToJavaString(env, "clipboard");
  auto board = env->CallObjectMethod(context, method, service);
  Check(env);
  if (!board)
    throw std::runtime_error("Android clipboard unavailable");
  return board;
}
std::string String(JNIEnv* env, jobject object) {
  if (!object)
    throw std::runtime_error("Android clipboard conversion failed");
  auto value = static_cast<jstring>(
      env->CallObjectMethod(object, Method(env, object, "toString", "()Ljava/lang/String;")));
  Check(env);
  if (!value)
    throw std::runtime_error("Android clipboard conversion failed");
  auto result = android::FromJavaString(env, value);
  env->DeleteLocalRef(value);
  return result;
}
int ApiLevel(JNIEnv* env) {
  auto cls = Class(env, "android/os/Build$VERSION");
  auto field = env->GetStaticFieldID(cls, "SDK_INT", "I");
  Check(env);
  if (!field)
    throw std::runtime_error("Android API level unavailable");
  auto level = env->GetStaticIntField(cls, field);
  Check(env);
  return level;
}
std::string Stamp(JNIEnv* env, jobject board) {
  auto description = env->CallObjectMethod(board, Method(env, board, "getPrimaryClipDescription",
                                                         "()Landroid/content/ClipDescription;"));
  Check(env);
  if (!description)
    return "empty";
  std::string result;
  if (ApiLevel(env) >= 26) {
    auto timestamp =
        env->CallLongMethod(description, Method(env, description, "getTimestamp", "()J"));
    Check(env);
    result = std::to_string(timestamp);
  } else {
    // Older releases lack metadata revisions and do not impose Android 10's
    // clipboard access restrictions. Inspect only while the host has focus.
    auto clip = env->CallObjectMethod(
        board, Method(env, board, "getPrimaryClip", "()Landroid/content/ClipData;"));
    Check(env);
    result = clip ? String(env, clip) : "empty";
    if (clip)
      env->DeleteLocalRef(clip);
  }
  env->DeleteLocalRef(description);
  return result;
}
std::shared_ptr<Image> ReadImage(JNIEnv* env, jobject resolver, jobject uri) {
  auto stream = env->CallObjectMethod(
      resolver,
      Method(env, resolver, "openInputStream", "(Landroid/net/Uri;)Ljava/io/InputStream;"), uri);
  Check(env);
  if (!stream)
    throw std::runtime_error("Android image stream unavailable");
  std::vector<unsigned char> bytes;
  try {
    auto buffer = env->NewByteArray(8192);
    Check(env);
    if (!buffer)
      throw std::runtime_error("Android image buffer unavailable");
    auto read = Method(env, stream, "read", "([B)I");
    while (true) {
      int count = env->CallIntMethod(stream, read, buffer);
      Check(env);
      if (count < 0)
        break;
      if (count == 0)
        continue;
      const auto offset = bytes.size();
      bytes.resize(offset + count);
      env->GetByteArrayRegion(buffer, 0, count, reinterpret_cast<jbyte*>(bytes.data() + offset));
      Check(env);
    }
    env->DeleteLocalRef(buffer);
  } catch (...) {
    env->CallVoidMethod(stream, Method(env, stream, "close", "()V"));
    android::ClearException(env);
    throw;
  }
  env->CallVoidMethod(stream, Method(env, stream, "close", "()V"));
  Check(env);
  env->DeleteLocalRef(stream);
  auto image = Image::FromBase64(clipboard_internal::Base64(bytes.data(), bytes.size()));
  if (!image)
    throw std::runtime_error("Android image conversion failed");
  return image;
}
ClipboardData Snapshot(JNIEnv* env, jobject board, unsigned mask) {
  const auto before = Stamp(env, board);
  ClipboardData data;
  auto clip = env->CallObjectMethod(
      board, Method(env, board, "getPrimaryClip", "()Landroid/content/ClipData;"));
  Check(env);
  if (clip) {
    int count = env->CallIntMethod(clip, Method(env, clip, "getItemCount", "()I"));
    Check(env);
    auto item_at = Method(env, clip, "getItemAt", "(I)Landroid/content/ClipData$Item;");
    auto resolver = env->CallObjectMethod(android::Context(),
                                          Method(env, android::Context(), "getContentResolver",
                                                 "()Landroid/content/ContentResolver;"));
    Check(env);
    for (int i = 0; i < count; ++i) {
      auto item = env->CallObjectMethod(clip, item_at, i);
      Check(env);
      if (!item)
        throw std::runtime_error("Android clipboard item unavailable");
      if (mask & Bit(ClipboardReadFormat::Text)) {
        auto text =
            env->CallObjectMethod(item, Method(env, item, "getText", "()Ljava/lang/CharSequence;"));
        Check(env);
        if (text && !data.text)
          data.text = String(env, text);
        if (text)
          env->DeleteLocalRef(text);
      }
      if (mask & Bit(ClipboardReadFormat::Html)) {
        auto html = static_cast<jstring>(
            env->CallObjectMethod(item, Method(env, item, "getHtmlText", "()Ljava/lang/String;")));
        Check(env);
        if (html && !data.html)
          data.html = android::FromJavaString(env, html);
        if (html)
          env->DeleteLocalRef(html);
      }
      if (mask & (Bit(ClipboardReadFormat::Image) | Bit(ClipboardReadFormat::FilePaths))) {
        auto uri = env->CallObjectMethod(item, Method(env, item, "getUri", "()Landroid/net/Uri;"));
        Check(env);
        if (uri) {
          if ((mask & Bit(ClipboardReadFormat::Image)) && !data.image) {
            auto type = static_cast<jstring>(env->CallObjectMethod(
                resolver, Method(env, resolver, "getType", "(Landroid/net/Uri;)Ljava/lang/String;"),
                uri));
            Check(env);
            if (type && android::FromJavaString(env, type).find("image/") == 0)
              data.image = ReadImage(env, resolver, uri);
            if (type)
              env->DeleteLocalRef(type);
          }
          if (mask & Bit(ClipboardReadFormat::FilePaths)) {
            auto scheme =
                env->CallObjectMethod(uri, Method(env, uri, "getScheme", "()Ljava/lang/String;"));
            auto host =
                env->CallObjectMethod(uri, Method(env, uri, "getHost", "()Ljava/lang/String;"));
            Check(env);
            if (scheme && String(env, scheme) == "file" &&
                (!host || String(env, host).empty() || String(env, host) == "localhost")) {
              auto path =
                  env->CallObjectMethod(uri, Method(env, uri, "getPath", "()Ljava/lang/String;"));
              Check(env);
              data.file_paths.push_back(String(env, path));
              env->DeleteLocalRef(path);
            }
            if (scheme)
              env->DeleteLocalRef(scheme);
            if (host)
              env->DeleteLocalRef(host);
          }
          env->DeleteLocalRef(uri);
        }
      }
      env->DeleteLocalRef(item);
    }
  }
  if (before != Stamp(env, board) || !clipboard_internal::ValidData(data))
    throw std::runtime_error("Android clipboard snapshot changed or is invalid");
  return data;
}
}  // namespace

struct Clipboard::Impl::Platform {
  std::optional<std::string> revision;
};
Clipboard::Impl::Impl(Clipboard* owner) : owner(owner), platform(std::make_unique<Platform>()) {}
Clipboard::Impl::~Impl() {
  Stop();
}
bool Clipboard::IsSupported() {
  return android::IsInitialized();
}
bool Clipboard::IsChangeMonitoringSupported() {
  return IsSupported();
}
void Clipboard::Impl::Read(unsigned mask, std::function<void(bool, ClipboardData)> callback) {
  auto complete = std::make_shared<std::function<void(bool, ClipboardData)>>(std::move(callback));
  if (!android::Post([mask, complete] {
        bool ok = false;
        ClipboardData data;
        try {
          android::Environment scope;
          if (scope && android::IsActive()) {
            data = Snapshot(scope.get(), Board(scope.get()), mask);
            ok = true;
          }
        } catch (...) {
        }
        (*complete)(ok, ok ? std::move(data) : ClipboardData{});
      }))
    (*complete)(false, {});
}
bool Clipboard::Impl::Write(const ClipboardData& data) {
  // Cross-application binary/file publication needs a host content provider;
  // never expose private paths as file: URIs or modify shared MediaStore storage.
  if (data.image || !data.file_paths.empty() || (data.html && !data.text))
    return false;
  bool result = false;
  android::RunSync([&] {
    try {
      android::Environment scope;
      if (!scope || !android::IsActive())
        return;
      auto* env = scope.get();
      auto board = Board(env);
      if (!data.text) {
        if (ApiLevel(env) < 28)
          return;
        env->CallVoidMethod(board, Method(env, board, "clearPrimaryClip", "()V"));
      } else {
        auto cls = Class(env, "android/content/ClipData");
        auto method = env->GetStaticMethodID(
            cls, data.html ? "newHtmlText" : "newPlainText",
            data.html
                ? "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;Ljava/lang/String;)Landroid/"
                  "content/ClipData;"
                : "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
        Check(env);
        if (!method)
          return;
        auto label = android::ToJavaString(env, "nativeapi");
        auto text = android::ToJavaString(env, *data.text);
        if (!label || !text)
          return;
        jobject clip;
        if (data.html) {
          auto html = android::ToJavaString(env, *data.html);
          if (!html)
            return;
          clip = env->CallStaticObjectMethod(cls, method, label, text, html);
        } else
          clip = env->CallStaticObjectMethod(cls, method, label, text);
        Check(env);
        if (!clip)
          return;
        env->CallVoidMethod(
            board, Method(env, board, "setPrimaryClip", "(Landroid/content/ClipData;)V"), clip);
      }
      Check(env);
      result = true;
    } catch (...) {
    }
  });
  return result;
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
  bool result = false;
  android::RunSync([&] {
    platform->revision.reset();
    auto sample = [this](bool notify) {
      if (!android::IsActive())
        return;
      try {
        android::Environment scope;
        if (!scope)
          return;
        const auto current = Stamp(scope.get(), Board(scope.get()));
        const bool changed = platform->revision && *platform->revision != current;
        platform->revision = current;
        if (notify && changed)
          Changed();
      } catch (...) {
      }
    };
    sample(false);
    result = android::SetClipboardPoll([sample] { sample(true); });
  });
  return result;
}
void Clipboard::Impl::Stop() {
  android::RunSync([&] {
    android::SetClipboardPoll({});
    platform->revision.reset();
  });
  monitoring = false;
}
}  // namespace nativeapi
