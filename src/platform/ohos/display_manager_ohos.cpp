#include <dlfcn.h>
#include <hilog/log.h>
#include <multimodalinput/oh_input_manager.h>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <mutex>
#include <optional>
#include "../../display_manager.h"
#include "display_ohos.h"

namespace nativeapi {
namespace {
enum class DisplayNotice { Changed, Added, Removed, Area };
struct DisplayObservation {
  std::atomic<bool> active{true};
  std::atomic<uint32_t> pending{0};
  std::array<std::optional<uint32_t>, 4> listeners;
  std::unordered_map<std::string, std::shared_ptr<Display>> known;
  std::function<void(DisplayNotice, uint64_t)> dispatch;
};
struct DisplayMonitoring {
  std::mutex transition;
  std::mutex mutex;
  std::shared_ptr<DisplayObservation> current;
};
DisplayMonitoring& Monitoring() {
  static DisplayMonitoring state;
  return state;
}
void Notice(DisplayNotice kind, uint64_t id) {
  std::shared_ptr<DisplayObservation> observation;
  {
    std::lock_guard<std::mutex> lock(Monitoring().mutex);
    observation = Monitoring().current;
  }
  if (!observation || !observation->active.load())
    return;
  // Never invoke subscribers on the SDK's IPC thread or fall back to that
  // thread when the owning UI dispatcher is unavailable.
  ++observation->pending;
  if (!RunOnMainThread([observation, kind, id] {
        --observation->pending;
        if (observation->active.load())
          observation->dispatch(kind, id);
      }))
    --observation->pending;
}
void Changed(uint64_t id) {
  Notice(DisplayNotice::Changed, id);
}
void Added(uint64_t id) {
  Notice(DisplayNotice::Added, id);
}
void Removed(uint64_t id) {
  Notice(DisplayNotice::Removed, id);
}
void AreaChanged(uint64_t id) {
  Notice(DisplayNotice::Area, id);
}
using RegisterDisplayListener = NativeDisplayManager_ErrorCode (*)(void (*)(uint64_t), uint32_t*);
using UnregisterDisplayListener = NativeDisplayManager_ErrorCode (*)(uint32_t);
const std::array<RegisterDisplayListener, 4> registrations = {
    OH_NativeDisplayManager_RegisterDisplayChangeListener,
    OH_NativeDisplayManager_RegisterDisplayAddListener,
    OH_NativeDisplayManager_RegisterDisplayRemoveListener,
    OH_NativeDisplayManager_RegisterAvailableAreaChangeListener};
const std::array<UnregisterDisplayListener, 4> unregistrations = {
    OH_NativeDisplayManager_UnregisterDisplayChangeListener,
    OH_NativeDisplayManager_UnregisterDisplayAddListener,
    OH_NativeDisplayManager_UnregisterDisplayRemoveListener,
    OH_NativeDisplayManager_UnregisterAvailableAreaChangeListener};
const std::array<void (*)(uint64_t), 4> callbacks = {Changed, Added, Removed, AreaChanged};
}  // namespace
namespace ohos {
uint32_t PendingDisplayNotifications() {
  std::lock_guard<std::mutex> lock(Monitoring().mutex);
  const auto& observation = Monitoring().current;
  return observation && observation->active.load() ? observation->pending.load() : 0;
}
}  // namespace ohos

DisplayManager::DisplayManager() {
  // Construct the registry before this singleton completes construction, so
  // its mutexes outlive our destructor and final SDK unregistrations.
  (void)Monitoring();
}
DisplayManager::~DisplayManager() {
  ShutdownEmitter();
  StopEventListening();
}

bool DisplayManager::EnumerateNativeDisplays(std::vector<NativeDisplayInfo>& natives) {
  NativeDisplayManager_DisplaysInfo* all = nullptr;
  if (OH_NativeDisplayManager_CreateAllDisplays(&all) != DISPLAY_MANAGER_OK || !all)
    return false;
  std::unique_ptr<NativeDisplayManager_DisplaysInfo,
                  decltype(&OH_NativeDisplayManager_DestroyAllDisplays)>
      owned(all, OH_NativeDisplayManager_DestroyAllDisplays);
  NativeDisplayManager_DisplayInfo* primary = nullptr;
  OH_NativeDisplayManager_CreatePrimaryDisplay(&primary);
  ohos::DisplayInfo primary_info(primary, OH_NativeDisplayManager_DestroyDisplay);
  natives.clear();
  for (uint32_t i = 0; i < all->displaysLength; ++i) {
    const auto& info = all->displaysInfo[i];
    if (info.isAlive)
      natives.push_back({std::to_string(info.id), ohos::DisplayToken(info.id),
                         primary_info && primary_info->id == info.id});
  }
  return true;
}

void DisplayManager::StartEventListening() {
  auto& state = Monitoring();
  std::lock_guard<std::mutex> transition(state.transition);
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.current)
      return;
  }
  std::vector<NativeDisplayInfo> natives;
  auto observation = std::make_shared<DisplayObservation>();
  if (EnumerateNativeDisplays(natives)) {
    auto displays = Reconcile(natives, nullptr, nullptr);
    for (size_t i = 0; i < natives.size(); ++i)
      observation->known.emplace(natives[i].key, displays[i]);
  } else {
    // Still subscribe: a later SDK callback can recover from a failed initial
    // snapshot. Preserve identities already discovered by ordinary queries.
    observation->known = displays_;
  }
  std::weak_ptr<DisplayObservation> weak = observation;
  observation->dispatch = CreateGuardedCallback<DisplayNotice, uint64_t>(
      std::function<void(DisplayNotice, uint64_t)>([this, weak](DisplayNotice kind, uint64_t id) {
        auto observation = weak.lock();
        if (!observation || !observation->active.load())
          return;
        const auto key = std::to_string(id);
        const auto announced = observation->known.find(key);
        const auto cached = displays_.find(key);
        const bool remove_cached = kind == DisplayNotice::Removed &&
                                   announced != observation->known.end() &&
                                   cached != displays_.end() && announced->second == cached->second;
        std::vector<NativeDisplayInfo> natives;
        if (!EnumerateNativeDisplays(natives)) {
          // Enumeration failure cannot remove the whole topology. An explicit
          // SDK removal still authoritatively disconnects that one identity.
          if (kind == DisplayNotice::Removed && announced != observation->known.end()) {
            auto removed = announced->second;
            observation->known.erase(announced);
            if (remove_cached)
              displays_.erase(key);
            Emit<DisplayRemovedEvent>(removed);
          }
          return;
        }
        if (remove_cached)
          displays_.erase(key);  // Preserve a newer object already found after reconnection.
        const auto displays = Reconcile(natives, nullptr, nullptr);
        std::unordered_map<std::string, std::shared_ptr<Display>> current;
        for (size_t i = 0; i < natives.size(); ++i)
          current.emplace(natives[i].key, displays[i]);
        const auto previous = std::move(observation->known);
        observation->known = current;
        // Keep an independent announced snapshot: a user's GetAll call must
        // not consume an addition/removal before this queued SDK notice runs.
        for (const auto& item : previous) {
          const auto found = current.find(item.first);
          if (found == current.end() || found->second != item.second) {
            if (!observation->active.load())
              return;
            Emit<DisplayRemovedEvent>(item.second);
          }
        }
        for (const auto& item : current) {
          const auto found = previous.find(item.first);
          if (found == previous.end() || found->second != item.second) {
            if (!observation->active.load())
              return;
            Emit<DisplayAddedEvent>(item.second);
          }
        }
        const auto found = current.find(key);
        const auto old = previous.find(key);
        if (observation->active.load() &&
            (kind == DisplayNotice::Changed || kind == DisplayNotice::Area) &&
            found != current.end() && old != previous.end() && found->second == old->second)
          Emit<DisplayChangedEvent>(found->second);
      }));
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.current = observation;
  }
  for (size_t i = 0; i < registrations.size(); ++i) {
    uint32_t index = 0;
    const auto code = registrations[i](callbacks[i], &index);
    if (code == DISPLAY_MANAGER_OK)
      observation->listeners[i] = index;
    OH_LOG_Print(LOG_APP, code == DISPLAY_MANAGER_OK ? LOG_INFO : LOG_WARN, 1, "nativeapi-display",
                 "SDK display observer %{public}zu register=%{public}d", i, int(code));
  }
}

void DisplayManager::StopEventListening() {
  auto& state = Monitoring();
  std::lock_guard<std::mutex> transition(state.transition);
  std::shared_ptr<DisplayObservation> observation;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    observation = std::move(state.current);
    if (observation)
      observation->active.store(false);
  }
  if (!observation)
    return;
  for (size_t i = 0; i < unregistrations.size(); ++i) {
    if (!observation->listeners[i])
      continue;
    const auto code = unregistrations[i](*observation->listeners[i]);
    OH_LOG_Print(LOG_APP, code == DISPLAY_MANAGER_OK ? LOG_INFO : LOG_WARN, 1, "nativeapi-display",
                 "SDK display observer %{public}zu unregister=%{public}d", i, int(code));
  }
}

Point DisplayManager::GetCursorPosition() {
  // OH_Input_GetPointerLocation arrives with a newer input SDK than the pinned
  // one, whose header does not declare it. Resolve it at runtime: an older
  // runtime only loses the cursor position instead of failing to build.
  using PointerLocationQuery = int32_t (*)(int32_t*, double*, double*);
  static const auto pointer_location = [] {
    auto query =
        reinterpret_cast<PointerLocationQuery>(dlsym(RTLD_DEFAULT, "OH_Input_GetPointerLocation"));
    if (!query) {
      // Nothing else here calls into libohinput, so it is not necessarily a
      // load-time dependency of this library.
      if (void* input = dlopen("libohinput.so", RTLD_LAZY | RTLD_LOCAL))
        query = reinterpret_cast<PointerLocationQuery>(dlsym(input, "OH_Input_GetPointerLocation"));
    }
    return query;
  }();
  if (!pointer_location)
    return {};
  int32_t display_id = -1;
  double x = 0, y = 0;
  if (pointer_location(&display_id, &x, &y) != INPUT_SUCCESS || display_id < 0 ||
      !std::isfinite(x) || !std::isfinite(y))
    return {};
  auto display = ohos::ReadDisplay(ohos::DisplayToken(static_cast<uint32_t>(display_id)));
  NativeDisplayManager_DisplayInfo* raw_primary = nullptr;
  OH_NativeDisplayManager_CreatePrimaryDisplay(&raw_primary);
  ohos::DisplayInfo primary(raw_primary, OH_NativeDisplayManager_DestroyDisplay);
  if (!display || !primary)
    return {};
  const auto scale = ohos::DisplayScale(*display);
  if (display->id == primary->id)
    return {x / scale, y / scale};
  // The input API reports local display pixels. A secondary display also needs
  // its desktop origin; older SDKs cannot supply that origin reliably.
  using PositionQuery = NativeDisplayManager_ErrorCode (*)(uint64_t, int32_t*, int32_t*);
  static const auto position = reinterpret_cast<PositionQuery>(
      dlsym(RTLD_DEFAULT, "OH_NativeDisplayManager_GetDisplayPosition"));
  int32_t dx = 0, dy = 0, px = 0, py = 0;
  if (!position || position(display->id, &dx, &dy) != DISPLAY_MANAGER_OK ||
      position(primary->id, &px, &py) != DISPLAY_MANAGER_OK)
    return {};
  const auto primary_scale = ohos::DisplayScale(*primary);
  return {(dx + x) / scale - px / primary_scale, (dy + y) / scale - py / primary_scale};
}

}  // namespace nativeapi
