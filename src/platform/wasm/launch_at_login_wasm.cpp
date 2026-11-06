// WebAssembly compile-only backend: native desktop operations are unavailable.
#include "../../launch_at_login.h"

namespace nativeapi {

class LaunchAtLogin::Impl {
 public:
  static bool IsSupported() { return false; }

  Impl() = default;

  explicit Impl(const std::string& id) : id_(id) {}

  Impl(const std::string& id, const std::string& display_name)
      : id_(id), display_name_(display_name) {}

  ~Impl() = default;

  std::string GetId() const { return id_; }
  std::string GetDisplayName() const { return display_name_; }

  bool SetDisplayName(const std::string&) { return false; }

  bool SetProgram(const std::string&, const std::vector<std::string>&) { return false; }

  std::string GetExecutablePath() const { return program_path_; }
  std::vector<std::string> GetArguments() const { return arguments_; }

  bool Enable() { return false; }
  bool Disable() { return false; }
  bool IsEnabled() const { return false; }

 private:
  std::string id_;
  std::string display_name_;
  std::string program_path_;
  std::vector<std::string> arguments_;
};

LaunchAtLogin::LaunchAtLogin() : pimpl_(std::make_unique<Impl>()) {}

LaunchAtLogin::LaunchAtLogin(const std::string& id) : pimpl_(std::make_unique<Impl>(id)) {}

LaunchAtLogin::LaunchAtLogin(const std::string& id, const std::string& display_name)
    : pimpl_(std::make_unique<Impl>(id, display_name)) {}

LaunchAtLogin::~LaunchAtLogin() = default;

bool LaunchAtLogin::IsSupported() {
  return Impl::IsSupported();
}

std::string LaunchAtLogin::GetId() const {
  return pimpl_->GetId();
}

std::string LaunchAtLogin::GetDisplayName() const {
  return pimpl_->GetDisplayName();
}

bool LaunchAtLogin::SetDisplayName(const std::string& display_name) {
  return pimpl_->SetDisplayName(display_name);
}

bool LaunchAtLogin::SetProgram(const std::string& executable_path,
                               const std::vector<std::string>& arguments) {
  return pimpl_->SetProgram(executable_path, arguments);
}

std::string LaunchAtLogin::GetExecutablePath() const {
  return pimpl_->GetExecutablePath();
}

std::vector<std::string> LaunchAtLogin::GetArguments() const {
  return pimpl_->GetArguments();
}

bool LaunchAtLogin::Enable() {
  return pimpl_->Enable();
}

bool LaunchAtLogin::Disable() {
  return pimpl_->Disable();
}

bool LaunchAtLogin::IsEnabled() const {
  return pimpl_->IsEnabled();
}

}  // namespace nativeapi
