#pragma once

#include "storage/database.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace cx::browser {

class BookmarkService {
public:
  explicit BookmarkService(
      storage::Database& database);

  std::int64_t Add(
      std::string_view url,
      std::string_view title);
  std::vector<storage::Bookmark> List() const;
  bool Remove(std::int64_t id);

private:
  storage::Database& database_;
};

}  // namespace cx::browser
