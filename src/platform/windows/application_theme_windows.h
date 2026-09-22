#pragma once

#include <atomic>
#include "../../application.h"

namespace nativeapi {

// Shared by the Windows application and menu implementations. Keep the user's
// choice, rather than a resolved color, so System follows later OS changes.
inline std::atomic<Brightness> application_brightness{Brightness::System};

}  // namespace nativeapi
