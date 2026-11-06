#include "../../device_info.h"
namespace nativeapi {
namespace {
class WasmDeviceInfoImpl final : public DeviceInfo::Impl {
 public:
  std::string GetName() const override { return ""; }
  std::string GetModel() const override { return ""; }
  std::string GetManufacturer() const override { return ""; }
  std::string GetOsName() const override { return ""; }
  std::string GetOsVersion() const override { return ""; }
  std::string GetKernelVersion() const override { return ""; }
  std::string GetArchitecture() const override {
#ifdef __wasm64__
    return "wasm64";
#else
    return "wasm32";
#endif
  }
};
}  // namespace
DeviceInfo::DeviceInfo() : pimpl_(std::make_unique<WasmDeviceInfoImpl>()) {}
DeviceInfo::~DeviceInfo() = default;
}  // namespace nativeapi
