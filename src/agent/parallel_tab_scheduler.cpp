#include "agent/parallel_tab_scheduler.h"

#include <algorithm>
#include <limits>

namespace cx::agent {

ParallelTabScheduler::ParallelTabScheduler(bool enabled)
    : enabled_(enabled) {}

bool ParallelTabScheduler::AddOrUpdate(
    std::int64_t tab_id,
    int priority,
    std::chrono::milliseconds budget) {
  if (!enabled_ || tab_id <= 0 || priority < 0 ||
      priority > 9 ||
      budget <= std::chrono::milliseconds::zero()) {
    return false;
  }
  entries_[tab_id] = Entry{
      ScheduledTab{tab_id, priority, budget, false}, 0};
  return true;
}

bool ParallelTabScheduler::Pause(std::int64_t tab_id) {
  const auto iterator = entries_.find(tab_id);
  if (iterator == entries_.end() || iterator->second.task.paused) {
    return false;
  }
  iterator->second.task.paused = true;
  return true;
}

bool ParallelTabScheduler::Resume(std::int64_t tab_id) {
  const auto iterator = entries_.find(tab_id);
  if (iterator == entries_.end() || !iterator->second.task.paused ||
      iterator->second.task.remaining.count() <= 0) {
    return false;
  }
  iterator->second.task.paused = false;
  return true;
}

bool ParallelTabScheduler::Remove(std::int64_t tab_id) {
  return entries_.erase(tab_id) == 1;
}

std::optional<ScheduledTab> ParallelTabScheduler::Next() {
  if (!enabled_) {
    return std::nullopt;
  }

  long long total_weight = 0;
  Entry* selected = nullptr;
  for (auto& [tab_id, entry] : entries_) {
    if (entry.task.paused ||
        entry.task.remaining <= std::chrono::milliseconds::zero()) {
      continue;
    }
    const long long weight = entry.task.priority + 1;
    entry.credit += weight;
    total_weight += weight;
    if (!selected ||
        entry.credit > selected->credit ||
        (entry.credit == selected->credit &&
         tab_id < selected->task.tab_id)) {
      selected = &entry;
    }
  }
  if (!selected) {
    return std::nullopt;
  }
  selected->credit -= total_weight;
  return selected->task;
}

bool ParallelTabScheduler::Charge(
    std::int64_t tab_id,
    std::chrono::milliseconds elapsed) {
  const auto iterator = entries_.find(tab_id);
  if (!enabled_ || iterator == entries_.end() ||
      elapsed < std::chrono::milliseconds::zero()) {
    return false;
  }
  auto& remaining = iterator->second.task.remaining;
  remaining = elapsed >= remaining
      ? std::chrono::milliseconds::zero()
      : remaining - elapsed;
  if (remaining == std::chrono::milliseconds::zero()) {
    iterator->second.task.paused = true;
  }
  return true;
}

std::optional<ScheduledTab> ParallelTabScheduler::Get(
    std::int64_t tab_id) const {
  const auto iterator = entries_.find(tab_id);
  return iterator == entries_.end()
      ? std::nullopt
      : std::optional<ScheduledTab>(iterator->second.task);
}

bool ParallelTabScheduler::enabled() const noexcept {
  return enabled_;
}

}  // namespace cx::agent
