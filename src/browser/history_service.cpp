#include "browser/history_service.h"

#include <chrono>

namespace cx::browser {

HistoryService::HistoryService(
    storage::Database& database)
    : database_(database) {}

bool HistoryService::RecordVisit(
    std::string_view url,
    std::string_view title) {
  if (url.empty() || url == "about:blank") {
    return true;
  }

  const auto enabled =
      database_.GetSetting("privacy.save_history");
  if (!enabled.has_value() || *enabled != "1") {
    return true;
  }

  const auto now =
      std::chrono::system_clock::now();
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(
          now.time_since_epoch()).count();

  return database_.AddHistory(
      url, title, seconds) > 0;
}

std::vector<storage::HistoryEntry>
HistoryService::Search(
    std::string_view query,
    std::size_t limit) const {
  return database_.SearchHistory(query, limit);
}

std::vector<storage::HistoryEntry>
HistoryService::Recent(std::size_t limit) const {
  return database_.ListHistory(limit);
}

bool HistoryService::Clear() {
  return database_.ClearHistory();
}

}  // namespace cx::browser
