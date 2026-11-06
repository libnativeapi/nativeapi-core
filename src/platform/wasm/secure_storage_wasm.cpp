// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../secure_storage.h"

namespace nativeapi {

class SecureStorage::Impl {
 public:
  explicit Impl(const std::string& scope) : scope_(scope) {}

  ~Impl() = default;

  bool Set(const std::string& key, const std::string& value) { return false; }

  std::string Get(const std::string& key, const std::string& default_value) const {
    return default_value;
  }

  bool Remove(const std::string& key) { return false; }

  bool Clear() { return false; }

  bool Contains(const std::string& key) const { return false; }

  std::vector<std::string> GetKeys() const { return {}; }

  size_t GetSize() const { return 0; }

  std::map<std::string, std::string> GetAll() const { return {}; }

  std::string GetScope() const { return scope_; }

 private:
  std::string scope_;
};

SecureStorage::SecureStorage() : SecureStorage("default") {}

SecureStorage::SecureStorage(const std::string& scope) : pimpl_(std::make_unique<Impl>(scope)) {}

SecureStorage::~SecureStorage() = default;

bool SecureStorage::Set(const std::string& key, const std::string& value) {
  return pimpl_->Set(key, value);
}

std::string SecureStorage::Get(const std::string& key, const std::string& default_value) const {
  return pimpl_->Get(key, default_value);
}

bool SecureStorage::Remove(const std::string& key) {
  return pimpl_->Remove(key);
}

bool SecureStorage::Clear() {
  return pimpl_->Clear();
}

bool SecureStorage::Contains(const std::string& key) const {
  return pimpl_->Contains(key);
}

std::vector<std::string> SecureStorage::GetKeys() const {
  return pimpl_->GetKeys();
}

size_t SecureStorage::GetSize() const {
  return pimpl_->GetSize();
}

std::map<std::string, std::string> SecureStorage::GetAll() const {
  return pimpl_->GetAll();
}

std::string SecureStorage::GetScope() const {
  return pimpl_->GetScope();
}

bool SecureStorage::IsAvailable() {
  return false;
}

}  // namespace nativeapi
