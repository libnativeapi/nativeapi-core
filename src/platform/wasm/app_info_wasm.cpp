#include "../../app_info.h"
namespace nativeapi {
namespace {
class WasmAppInfoImpl final : public AppInfo::Impl {
 public:
  std::string GetName() const override { return ""; }
  std::string GetIdentifier() const override { return ""; }
  std::string GetVersion() const override { return ""; }
  std::string GetBuildNumber() const override { return ""; }
};
}  // namespace
AppInfo::AppInfo() : pimpl_(std::make_unique<WasmAppInfoImpl>()) {}
AppInfo::~AppInfo() = default;
}  // namespace nativeapi
