#include "mcp/rate_limiter.h"

#include <algorithm>

namespace cx::mcp {
namespace {

std::string RateKey(
    std::string_view server_id,
    std::string_view tool_id,
    std::int64_t tab_id) {
  return std::string(server_id) + "\n" +
      std::string(tool_id) + "\n" +
      std::to_string(tab_id);
}

}  // namespace


RateLimiter::RateLimiter(
    std::size_t max_requests_per_window,
    std::chrono::milliseconds window)
    : max_requests_per_window_(
          std::max<std::size_t>(1, max_requests_per_window)),
      window_(std::max(window, std::chrono::milliseconds(1))) {}

bool RateLimiter::Allow(std::string_view server_id) {
  return Allow(server_id, {}, 0);
}

bool RateLimiter::AllowAt(
    std::string_view server_id, TimePoint now) {
  return AllowAt(server_id, {}, 0, now);
}

bool RateLimiter::Allow(
    std::string_view server_id,
    std::string_view tool_id,
    std::int64_t tab_id) {
  return AllowAt(
      server_id, tool_id, tab_id, Clock::now());
}

bool RateLimiter::AllowAt(
    std::string_view server_id,
    std::string_view tool_id,
    std::int64_t tab_id,
    TimePoint now) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& queue = requests_[
      RateKey(server_id, tool_id, tab_id)];

  const auto cutoff = now - window_;
  while (!queue.empty() && queue.front() <= cutoff) {
    queue.pop_front();
  }
  if (queue.size() >= max_requests_per_window_) {
    return false;
  }

  queue.push_back(now);
  return true;
}

std::size_t RateLimiter::limit() const noexcept {
  return max_requests_per_window_;
}

std::chrono::milliseconds RateLimiter::window() const noexcept {
  return window_;
}

void RateLimiter::Reset(std::string_view server_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string prefix =
      std::string(server_id) + "\n";
  std::erase_if(
      requests_,
      [&prefix](const auto& entry) {
        return entry.first.starts_with(prefix);
      });
}

}  // namespace cx::mcp
