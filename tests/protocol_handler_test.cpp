#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../src/foundation/dispatcher.h"
#include "../src/protocol_handler.h"
#include "../src/protocol_handler_dispatch.h"

// Exercises the shared ProtocolHandler state through the host bridge that the
// Flutter plugin uses: launch URL capture, late launch URLs, the bounded queue
// for URLs that arrive without a listener, and scheme validation. Native URL
// sources (Apple events, intents, WM_COPYDATA) are verified in the example apps.

namespace {

std::mutex mutex;
std::vector<std::function<void()>> queued;

void Drain() {
  for (;;) {
    std::vector<std::function<void()>> batch;
    {
      std::lock_guard<std::mutex> lock(mutex);
      batch.swap(queued);
    }
    if (batch.empty())
      return;
    for (auto& fn : batch)
      fn();
  }
}

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
  }
}

}  // namespace

int main() {
  using namespace nativeapi;
  const auto ui = std::this_thread::get_id();
  SetMainThreadDispatcher(
      [](std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(mutex);
        queued.push_back(std::move(fn));
        return true;
      },
      [ui] { return std::this_thread::get_id() == ui; });

  auto& handler = ProtocolHandler::GetInstance();
  std::vector<std::string> received;
  auto listen = [&] {
    return handler.AddListener<ProtocolUrlReceivedEvent>(
        [&](const ProtocolUrlReceivedEvent& event) { received.push_back(event.GetUrl()); });
  };

  Check(!handler.GetInitialUrl(), "a test run has no launch URL");

  // The launch URL arrives after the answer above: it becomes the initial URL
  // and is queued as an event as well.
  nativeapi_protocol_handler_host_deliver("app://launch", 1);
  Check(handler.GetInitialUrl().value_or("") == "app://launch", "late launch URL is kept");

  // A second launch-flagged URL is an ordinary received URL.
  nativeapi_protocol_handler_host_deliver("app://second", 1);
  Check(handler.GetInitialUrl().value_or("") == "app://launch", "initial URL is set once");
  nativeapi_protocol_handler_host_deliver(nullptr, 0);
  nativeapi_protocol_handler_host_deliver("", 0);

  // 2 queued so far; 40 more overflow the 32-entry queue.
  for (int i = 0; i < 40; ++i)
    nativeapi_protocol_handler_host_deliver(("app://n/" + std::to_string(i)).c_str(), 0);
  Drain();
  Check(received.empty(), "nothing is emitted without a listener");

  auto id = listen();
  Drain();
  Check(received.size() == 32, "the first listener gets the 32 newest queued URLs");
  Check(received.front() == "app://n/8" && received.back() == "app://n/39",
        "queued URLs keep their order and drop the oldest");

  received.clear();
  nativeapi_protocol_handler_host_deliver("app://live", 0);
  Check(received.empty(), "events are emitted asynchronously");
  Drain();
  Check(received.size() == 1 && received[0] == "app://live", "a listener receives live URLs");

  received.clear();
  Check(handler.RemoveListener(id), "listener removal");
  nativeapi_protocol_handler_host_deliver("app://later", 0);
  Drain();
  Check(received.empty(), "a removed listener is not called");
  id = listen();
  Drain();
  Check(received.size() == 1 && received[0] == "app://later",
        "URLs received between listeners are delivered to the next one");
  handler.RemoveListener(id);

  for (const char* scheme : {"", "1app", "my app", "app:", "-app", "app/x"})
    Check(!handler.Register(scheme), "invalid schemes are rejected");

  Drain();
  SetMainThreadDispatcher(nullptr, nullptr);
  std::cout << "protocol_handler_test: PASS" << std::endl;
  return 0;
}
