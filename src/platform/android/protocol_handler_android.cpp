#include <jni.h>

#include <mutex>

#include "../../protocol_handler_dispatch.h"
#include "android_runtime.h"

// Android delivers deep links as ACTION_VIEW intents to an activity the host
// owns. The host forwards them through the Java class
// dev.libnativeapi.nativeapi.ProtocolHandlerBridge, which declares
//
//   static native void nativeAttach(Context context);
//   static native void nativeDeliver(String url, boolean isLaunch);
//
// and is shipped by the Flutter plugin protocol_handler (leanflutter). Any other host
// can define the same class. Registration needs a Context, taken from the bridge
// or from a NativeActivity host initialized through android::Initialize.

namespace nativeapi::detail::protocol_platform {

namespace {

std::mutex g_mutex;
JavaVM* g_vm = nullptr;
jobject g_context = nullptr;

class BridgeEnvironment {
 public:
  explicit BridgeEnvironment(JavaVM* vm) : vm_(vm) {
    if (!vm_)
      return;
    const jint result = vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
      if (vm_->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
        env_ = nullptr;
        return;
      }
      attached_ = true;
    } else if (result != JNI_OK) {
      env_ = nullptr;
      return;
    }
    if (env_->PushLocalFrame(32) != JNI_OK) {
      android::ClearException(env_);
      if (attached_)
        vm_->DetachCurrentThread();
      env_ = nullptr;
      attached_ = false;
      return;
    }
    frame_ = true;
  }
  ~BridgeEnvironment() {
    if (frame_)
      env_->PopLocalFrame(nullptr);
    if (attached_)
      vm_->DetachCurrentThread();
  }
  BridgeEnvironment(const BridgeEnvironment&) = delete;
  BridgeEnvironment& operator=(const BridgeEnvironment&) = delete;
  JNIEnv* get() const { return env_; }

 private:
  JavaVM* vm_;
  JNIEnv* env_ = nullptr;
  bool attached_ = false;
  bool frame_ = false;
};

bool HasViewActivity(JNIEnv* env, jobject context, const std::string& scheme) {
  jclass context_class = env->GetObjectClass(context);
  jmethodID get_package_manager = env->GetMethodID(context_class, "getPackageManager",
                                                   "()Landroid/content/pm/PackageManager;");
  jmethodID get_package_name =
      env->GetMethodID(context_class, "getPackageName", "()Ljava/lang/String;");
  jclass uri_class = env->FindClass("android/net/Uri");
  jmethodID uri_parse = uri_class ? env->GetStaticMethodID(uri_class, "parse",
                                                           "(Ljava/lang/String;)Landroid/net/Uri;")
                                  : nullptr;
  jclass intent_class = env->FindClass("android/content/Intent");
  jmethodID intent_init =
      intent_class
          ? env->GetMethodID(intent_class, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V")
          : nullptr;
  jmethodID set_package =
      intent_class ? env->GetMethodID(intent_class, "setPackage",
                                      "(Ljava/lang/String;)Landroid/content/Intent;")
                   : nullptr;
  jclass manager_class = env->FindClass("android/content/pm/PackageManager");
  jmethodID query = manager_class ? env->GetMethodID(manager_class, "queryIntentActivities",
                                                     "(Landroid/content/Intent;I)Ljava/util/List;")
                                  : nullptr;
  jclass list_class = env->FindClass("java/util/List");
  jmethodID list_size = list_class ? env->GetMethodID(list_class, "size", "()I") : nullptr;
  if (android::ClearException(env) || !get_package_manager || !get_package_name || !uri_parse ||
      !intent_init || !set_package || !query || !list_size)
    return false;

  jobject manager = env->CallObjectMethod(context, get_package_manager);
  jobject package_name = env->CallObjectMethod(context, get_package_name);
  jstring uri_text = android::ToJavaString(env, scheme + "://");
  jobject uri = uri_text ? env->CallStaticObjectMethod(uri_class, uri_parse, uri_text) : nullptr;
  jstring action = android::ToJavaString(env, "android.intent.action.VIEW");
  if (android::ClearException(env) || !manager || !package_name || !uri || !action)
    return false;
  jobject intent = env->NewObject(intent_class, intent_init, action, uri);
  if (android::ClearException(env) || !intent)
    return false;
  env->CallObjectMethod(intent, set_package, package_name);
  jobject matches = env->CallObjectMethod(manager, query, intent, 0);
  if (android::ClearException(env) || !matches)
    return false;
  const jint count = env->CallIntMethod(matches, list_size);
  return !android::ClearException(env) && count > 0;
}

}  // namespace

bool IsSupported() {
  return ProtocolHandlerDispatch::IsHostAttached();
}

void StartCapture() {}

bool Register(const std::string& scheme) {
  JavaVM* vm = nullptr;
  jobject context = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    vm = g_vm;
    context = g_context;
  }
  if (vm && context) {
    BridgeEnvironment env(vm);
    return env.get() && HasViewActivity(env.get(), context, scheme);
  }
  android::Environment env;
  jobject runtime_context = android::Context();
  return env && runtime_context && HasViewActivity(env.get(), runtime_context, scheme);
}

std::optional<std::string> ReadLaunchUrl() {
  return std::nullopt;
}

}  // namespace nativeapi::detail::protocol_platform

extern "C" JNIEXPORT void JNICALL
Java_dev_libnativeapi_nativeapi_ProtocolHandlerBridge_nativeAttach(JNIEnv* env,
                                                                   jclass,
                                                                   jobject context) {
  using namespace nativeapi::detail::protocol_platform;
  JavaVM* vm = nullptr;
  if (context && env->GetJavaVM(&vm) == JNI_OK) {
    jobject retained = env->NewGlobalRef(context);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_context)
      env->DeleteGlobalRef(g_context);
    g_vm = vm;
    g_context = retained;
  }
  nativeapi_protocol_handler_host_attach();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_libnativeapi_nativeapi_ProtocolHandlerBridge_nativeDeliver(JNIEnv* env,
                                                                    jclass,
                                                                    jstring url,
                                                                    jboolean is_launch) {
  if (!url)
    return;
  const std::string value = nativeapi::android::FromJavaString(env, url);
  nativeapi_protocol_handler_host_deliver(value.c_str(), is_launch ? 1 : 0);
}
