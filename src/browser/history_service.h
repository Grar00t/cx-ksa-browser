#pragma once

#include "storage/database.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace cx::browser {

class HistoryService {
public:
  explicit HistoryService(
      storage::Database& database);

  bool RecordVisit(
      std::string_view url,
      std::string_view title);
  std::vector<storage::HistoryEntry> Search(
      std::string_view query,
      std::size_t limit = 200) const;
  std::vector<storage::HistoryEntry> Recent(
      std::size_t limit = 200) const;
  bool Clear();

private:
  storage::Database& database_;
};

}  // namespace cx::browser
