// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../preferences.h"

namespace nativeapi {

class Preferences::Impl {
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

  const std::string& GetScope() const { return scope_; }

 private:
  std::string scope_;
};

Preferences::Preferences() : Preferences("default") {}

Preferences::Preferences(const std::string& scope) : pimpl_(std::make_unique<Impl>(scope)) {}

Preferences::~Preferences() = default;

bool Preferences::Set(const std::string& key, const std::string& value) {
  return pimpl_->Set(key, value);
}

std::string Preferences::Get(const std::string& key, const std::string& default_value) const {
  return pimpl_->Get(key, default_value);
}

bool Preferences::Remove(const std::string& key) {
  return pimpl_->Remove(key);
}

bool Preferences::Clear() {
  return pimpl_->Clear();
}

bool Preferences::Contains(const std::string& key) const {
  return pimpl_->Contains(key);
}

std::vector<std::string> Preferences::GetKeys() const {
  return pimpl_->GetKeys();
}

size_t Preferences::GetSize() const {
  return pimpl_->GetSize();
}

std::map<std::string, std::string> Preferences::GetAll() const {
  return pimpl_->GetAll();
}

std::string Preferences::GetScope() const {
  return pimpl_->GetScope();
}

}  // namespace nativeapi
