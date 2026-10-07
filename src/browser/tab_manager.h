#pragma once

#include "storage/database.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace cx::browser {

class TabManager {
public:
  explicit TabManager(storage::Database& database);

  bool Restore();

  std::optional<std::int64_t> CreateTab(
      std::string_view url = "about:blank",
      std::string_view title = "New Tab",
      bool activate = true);
  bool CloseTab(std::int64_t id);
  bool ActivateTab(std::int64_t id);
  bool UpdateTab(
      std::int64_t id,
      std::string_view url,
      std::string_view title);

  const std::vector<storage::Tab>& tabs() const noexcept;
  std::optional<storage::Tab> active_tab() const;
  std::int64_t active_tab_id() const noexcept;
  bool HasTab(std::int64_t id) const noexcept;

private:
  std::optional<std::size_t> FindIndex(
      std::int64_t id) const noexcept;
  bool NormalizePositions();
  static std::optional<std::int64_t> ParseId(
      std::string_view value);

  storage::Database& database_;
  std::vector<storage::Tab> tabs_;
  std::int64_t active_tab_id_ = 0;
};

}  // namespace cx::browser
