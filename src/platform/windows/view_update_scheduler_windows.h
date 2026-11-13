#pragma once

#include <memory>

namespace nativeapi {
class View;
namespace detail {
bool ScheduleViewUpdate(const std::shared_ptr<View>& view);
}
}  // namespace nativeapi
