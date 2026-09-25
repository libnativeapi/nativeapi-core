#pragma once

// C++ only. Ownership of the user_data a binding passes with a callback; the
// generated glue wraps every C callback with one of these.

#include <memory>

#include "../foundation/dispatcher.h"
#include "common_c.h"

namespace nativeapi::capi {

/**
 * @brief A binding's user_data, released once the core can no longer call the
 *        callback it travels with.
 *
 * The generated glue creates one at the top of every function taking a
 * callback, before anything can fail, and captures it in the std::function it
 * hands the C++ API. Whatever becomes of that std::function — kept, replaced,
 * dropped because the call failed, destroyed with its owner — the last copy
 * going away releases the user_data exactly once.
 *
 * The release is posted to the main thread, never run inside the call that
 * dropped the callback: a binding's release may run arbitrary code (a Rust
 * closure's destructor), which must not execute under the lock of whatever
 * core object was letting go. Where no main-thread dispatch exists
 * (Android/OHOS) it runs inline instead. A release that cannot be posted
 * because the process is shutting down is dropped: the binding's runtime may
 * already be gone.
 */
class UserData {
 public:
  static std::shared_ptr<UserData> Make(void* data, native_release_user_data_t release) {
    return std::shared_ptr<UserData>(new UserData(data, release));
  }

  UserData(const UserData&) = delete;
  UserData& operator=(const UserData&) = delete;

  ~UserData() {
    if (!release_) {
      return;
    }
    auto release = release_;
    auto* data = data_;
    const bool posted = RunOnMainThread([release, data]() { release(data); });
    if (!posted && !IsMainThreadDispatchSupported()) {
      release(data);
    }
  }

  void* get() const { return data_; }

 private:
  UserData(void* data, native_release_user_data_t release) : data_(data), release_(release) {}

  void* data_;
  native_release_user_data_t release_;
};

}  // namespace nativeapi::capi
