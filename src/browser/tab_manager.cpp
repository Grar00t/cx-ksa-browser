#include "browser/tab_manager.h"

#include <algorithm>
#include <charconv>
#include <string>

namespace cx::browser {
namespace {

constexpr std::string_view kActiveTabSetting =
    "browser.active_tab_id";

std::string IdToString(std::int64_t id) {
  return std::to_string(id);
}

}  // namespace

TabManager::TabManager(storage::Database& database)
    : database_(database) {}

bool TabManager::Restore() {
  tabs_ = database_.ListTabs();
  if (!NormalizePositions()) {
    tabs_.clear();
    active_tab_id_ = 0;
    return false;
  }

  if (tabs_.empty()) {
    return CreateTab().has_value();
  }

  const auto stored =
      database_.GetSetting(kActiveTabSetting);
  if (stored.has_value()) {
    const auto parsed = ParseId(*stored);
    if (parsed.has_value() && HasTab(*parsed)) {
      active_tab_id_ = *parsed;
      return true;
    }
  }

  active_tab_id_ = tabs_.front().id;
  return database_.SetSetting(
      kActiveTabSetting, IdToString(active_tab_id_));
}

std::optional<std::int64_t> TabManager::CreateTab(
    std::string_view url,
    std::string_view title,
    bool activate) {
  if (url.empty()) {
    url = "about:blank";
  }
  if (title.empty()) {
    title = "New Tab";
  }

  storage::Transaction transaction(database_);
  if (!transaction.ok()) {
    return std::nullopt;
  }

  const int position =
      static_cast<int>(tabs_.size());
  const std::int64_t id =
      database_.AddTab(position, url, title, false);
  if (id <= 0) {
    return std::nullopt;
  }

  if (activate &&
      !database_.SetSetting(
          kActiveTabSetting, IdToString(id))) {
    return std::nullopt;
  }

  if (!transaction.Commit()) {
    return std::nullopt;
  }

  storage::Tab tab;
  tab.id = id;
  tab.position = position;
  tab.url = std::string(url);
  tab.title = std::string(title);

  tab.pinned = false;
  tabs_.push_back(std::move(tab));
  if (activate) {
    active_tab_id_ = id;
  } else if (active_tab_id_ == 0) {
    active_tab_id_ = id;
  }
  return id;
}

bool TabManager::CloseTab(std::int64_t id) {
  const auto index = FindIndex(id);
  if (!index.has_value()) {
    return false;
  }

  if (tabs_.size() == 1) {
    storage::Transaction transaction(database_);
    if (!transaction.ok() ||
        !database_.DeleteTab(id)) {
      return false;
    }

    const std::int64_t replacement =
        database_.AddTab(
            0, "about:blank", "New Tab", false);

    if (replacement <= 0 ||
        !database_.SetSetting(
            kActiveTabSetting,
            IdToString(replacement)) ||
        !transaction.Commit()) {
      return false;
    }

    tabs_.clear();
    storage::Tab blank;
    blank.id = replacement;
    blank.position = 0;
    blank.url = "about:blank";
    blank.title = "New Tab";
    tabs_.push_back(std::move(blank));
    active_tab_id_ = replacement;
    return true;
  }

  std::int64_t next_active = active_tab_id_;
  if (id == active_tab_id_) {
    if (*index + 1 < tabs_.size()) {
      next_active = tabs_[*index + 1].id;
    } else {
      next_active = tabs_[*index - 1].id;
    }
  }

  storage::Transaction transaction(database_);
  if (!transaction.ok() ||
      !database_.DeleteTab(id)) {
    return false;
  }

  for (std::size_t i = *index + 1;
       i < tabs_.size(); ++i) {
    const auto& tab = tabs_[i];
    if (!database_.UpdateTab(
            tab.id, static_cast<int>(i - 1),
            tab.url, tab.title, tab.pinned)) {
      return false;
    }
  }

  if (next_active != active_tab_id_ &&
      !database_.SetSetting(
          kActiveTabSetting,
          IdToString(next_active))) {
    return false;
  }

  if (!transaction.Commit()) {
    return false;
  }

  tabs_.erase(tabs_.begin() +
              static_cast<std::ptrdiff_t>(*index));
  for (std::size_t i = *index;
       i < tabs_.size(); ++i) {
    tabs_[i].position = static_cast<int>(i);
  }
  active_tab_id_ = next_active;
  return true;
}

bool TabManager::ActivateTab(std::int64_t id) {
  if (!HasTab(id)) {
    return false;
  }
  if (id == active_tab_id_) {
    return true;
  }
  if (!database_.SetSetting(
          kActiveTabSetting, IdToString(id))) {
    return false;
  }
  active_tab_id_ = id;
  return true;
}

bool TabManager::UpdateTab(
    std::int64_t id,
    std::string_view url,

    std::string_view title) {
  const auto index = FindIndex(id);
  if (!index.has_value() || url.empty()) {
    return false;
  }

  auto& tab = tabs_[*index];
  const std::string effective_title =
      title.empty() ? tab.title : std::string(title);

  if (!database_.UpdateTab(
          tab.id, tab.position, url,
          effective_title, tab.pinned)) {
    return false;
  }

  tab.url = std::string(url);
  tab.title = effective_title;
  return true;
}

const std::vector<storage::Tab>&
TabManager::tabs() const noexcept {
  return tabs_;
}

std::optional<storage::Tab>
TabManager::active_tab() const {
  const auto index = FindIndex(active_tab_id_);

  if (!index.has_value()) {
    return std::nullopt;
  }
  return tabs_[*index];
}

std::int64_t
TabManager::active_tab_id() const noexcept {
  return active_tab_id_;
}

bool TabManager::HasTab(
    std::int64_t id) const noexcept {
  return FindIndex(id).has_value();
}

std::optional<std::size_t>
TabManager::FindIndex(
    std::int64_t id) const noexcept {
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].id == id) {
      return i;
    }
  }
  return std::nullopt;
}

bool TabManager::NormalizePositions() {

  bool needs_update = false;
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].position != static_cast<int>(i)) {
      needs_update = true;
      break;
    }
  }
  if (!needs_update) {
    return true;
  }

  storage::Transaction transaction(database_);
  if (!transaction.ok()) {
    return false;
  }
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    const auto& tab = tabs_[i];
    if (!database_.UpdateTab(
            tab.id, static_cast<int>(i),
            tab.url, tab.title, tab.pinned)) {
      return false;
    }
  }
  if (!transaction.Commit()) {
    return false;
  }
  for (std::size_t i = 0; i < tabs_.size(); ++i) {

    tabs_[i].position = static_cast<int>(i);
  }
  return true;
}

std::optional<std::int64_t>
TabManager::ParseId(std::string_view value) {
  if (value.empty()) {
    return std::nullopt;
  }

  std::int64_t id = 0;
  const char* begin = value.data();
  const char* end = value.data() + value.size();
  const auto result =
      std::from_chars(begin, end, id);
  if (result.ec != std::errc() ||
      result.ptr != end || id <= 0) {
    return std::nullopt;
  }
  return id;
}

}  // namespace cx::browser
