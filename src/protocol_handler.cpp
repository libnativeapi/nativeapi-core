#include "protocol_handler.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

#include "protocol_handler_dispatch.h"

namespace nativeapi {

namespace {

constexpr size_t kMaxPendingUrls = 32;

std::atomic<bool> g_host_attached{false};

// RFC 3986: ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ), lower-cased.
std::optional<std::string> NormalizeScheme(const std::string& scheme) {
  if (scheme.empty())
    return std::nullopt;
  std::string normalized;
  normalized.reserve(scheme.size());
  for (size_t i = 0; i < scheme.size(); ++i) {
    char c = scheme[i];
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
    const bool alpha = c >= 'a' && c <= 'z';
    const bool other = (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
    if (!alpha && (i == 0 || !other))
      return std::nullopt;
    normalized.push_back(c);
  }
  return normalized;
}

}  // namespace

class ProtocolHandler::Impl {
 public:
  explicit Impl(ProtocolHandler* owner) : owner_(owner) {}

  void Deliver(const std::string& url, bool is_launch) {
    if (url.empty())
      return;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (is_launch && !initial_url_ && !received_any_) {
        initial_url_ = url;
        // Already answered "no launch URL": deliver it as an event as well,
        // so it is not lost to a caller that read too early.
        if (!initial_queried_)
          return;
      }
      received_any_ = true;
      if (!listening_) {
        if (pending_.size() == kMaxPendingUrls)
          pending_.pop_front();
        pending_.push_back(url);
        return;
      }
    }
    owner_->EmitAsync<ProtocolUrlReceivedEvent>(url);
  }

  void StartListening() {
    std::vector<std::string> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      listening_ = true;
      pending.assign(pending_.begin(), pending_.end());
      pending_.clear();
    }
    for (auto& url : pending)
      owner_->EmitAsync<ProtocolUrlReceivedEvent>(std::move(url));
  }

  void StopListening() {
    std::lock_guard<std::mutex> lock(mutex_);
    listening_ = false;
  }

  void SetInitialUrlIfUnset(std::optional<std::string> url) {
    if (!url || url->empty())
      return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initial_url_ && !received_any_)
      initial_url_ = std::move(url);
  }

  std::optional<std::string> GetInitialUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initial_url_)
      initial_queried_ = true;
    return initial_url_;
  }

 private:
  ProtocolHandler* owner_;
  mutable std::mutex mutex_;
  std::optional<std::string> initial_url_;
  std::deque<std::string> pending_;
  bool received_any_ = false;
  bool listening_ = false;
  mutable bool initial_queried_ = false;
};

ProtocolHandler& ProtocolHandler::GetInstance() {
  static ProtocolHandler instance;
  return instance;
}

bool ProtocolHandler::IsSupported() {
  return detail::protocol_platform::IsSupported();
}

ProtocolHandler::ProtocolHandler() : pimpl_(std::make_unique<Impl>(this)) {
  pimpl_->SetInitialUrlIfUnset(detail::protocol_platform::ReadLaunchUrl());
  detail::protocol_platform::StartCapture();
}

ProtocolHandler::~ProtocolHandler() {
  ShutdownEmitter();
}

bool ProtocolHandler::Register(const std::string& scheme) {
  auto normalized = NormalizeScheme(scheme);
  if (!normalized || !IsSupported())
    return false;
  try {
    return detail::protocol_platform::Register(*normalized);
  } catch (...) {
    return false;
  }
}

std::optional<std::string> ProtocolHandler::GetInitialUrl() const {
  return pimpl_->GetInitialUrl();
}

void ProtocolHandler::StartEventListening() {
  pimpl_->StartListening();
}

void ProtocolHandler::StopEventListening() {
  pimpl_->StopListening();
}

namespace detail {

void ProtocolHandlerDispatch::Deliver(const std::string& url, bool is_launch) {
  ProtocolHandler::GetInstance().pimpl_->Deliver(url, is_launch);
}

void ProtocolHandlerDispatch::MarkHostAttached() {
  g_host_attached.store(true);
}

bool ProtocolHandlerDispatch::IsHostAttached() {
  return g_host_attached.load();
}

}  // namespace detail

}  // namespace nativeapi

extern "C" void nativeapi_protocol_handler_host_attach(void) {
  nativeapi::detail::ProtocolHandlerDispatch::MarkHostAttached();
  nativeapi::ProtocolHandler::GetInstance();
}

extern "C" void nativeapi_protocol_handler_host_deliver(const char* url, int is_launch) {
  if (!url)
    return;
  nativeapi::detail::ProtocolHandlerDispatch::MarkHostAttached();
  nativeapi::detail::ProtocolHandlerDispatch::Deliver(url, is_launch != 0);
}
