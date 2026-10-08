#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace cx::agent {

struct ScheduledTab {
  std::int64_t tab_id = 0;
  int priority = 0;
  std::chrono::milliseconds remaining{0};
  bool paused = false;
};

class ParallelTabScheduler {
public:
  explicit ParallelTabScheduler(bool enabled = false);

  bool AddOrUpdate(
      std::int64_t tab_id,
      int priority,
      std::chrono::milliseconds budget);
  bool Pause(std::int64_t tab_id);
  bool Resume(std::int64_t tab_id);
  bool Remove(std::int64_t tab_id);
  std::optional<ScheduledTab> Next();
  bool Charge(
      std::int64_t tab_id,
      std::chrono::milliseconds elapsed);
  std::optional<ScheduledTab> Get(std::int64_t tab_id) const;
  bool enabled() const noexcept;

private:
  struct Entry {
    ScheduledTab task;
    long long credit = 0;
  };

  bool enabled_ = false;
  std::unordered_map<std::int64_t, Entry> entries_;
};

}  // namespace cx::agent
