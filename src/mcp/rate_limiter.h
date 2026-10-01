#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cx::mcp {

class RateLimiter {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit RateLimiter(
      std::size_t max_requests_per_window = 10,
      std::chrono::milliseconds window =
          std::chrono::seconds(1));

  bool Allow(std::string_view server_id);
  bool AllowAt(std::string_view server_id, TimePoint now);

  std::size_t limit() const noexcept;
  std::chrono::milliseconds window() const noexcept;
  void Reset(std::string_view server_id);

private:
  std::size_t max_requests_per_window_;
  std::chrono::milliseconds window_;
  std::unordered_map<std::string, std::deque<TimePoint>> requests_;
  mutable std::mutex mutex_;
};

}  // namespace cx::mcp
