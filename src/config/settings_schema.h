#pragma once

#include <string>

namespace cx::config {

enum class StartupBehavior {
  Blank,
  RestoreSession,
};

enum class SearchProvider {
  Disabled,
  DuckDuckGo,
  Bing,
  Google,
};

enum class Theme {
  System,
  Light,
  Dark,
};

enum class CookiePolicy {
  BlockAll,
  SessionOnly,
  AllowPersistent,
};

enum class CachePolicy {
  MemoryOnly,
  SessionDisk,
  Persistent,
};

struct GeneralSettings {
  StartupBehavior startup = StartupBehavior::Blank;
  SearchProvider default_search = SearchProvider::Disabled;

  bool operator==(const GeneralSettings&) const = default;
};

struct AppearanceSettings {
  Theme theme = Theme::System;
  int font_size_percent = 100;

  bool operator==(const AppearanceSettings&) const = default;
};

struct PrivacySettings {
  CookiePolicy cookies = CookiePolicy::BlockAll;
  CachePolicy cache = CachePolicy::MemoryOnly;

  bool operator==(const PrivacySettings&) const = default;
};

struct AdvancedSettings {
  bool developer_tools = false;
  bool experimental = false;

  bool operator==(const AdvancedSettings&) const = default;
};

struct Settings {
  int version = 1;
  GeneralSettings general;
  AppearanceSettings appearance;
  PrivacySettings privacy;
  AdvancedSettings advanced;

  bool operator==(const Settings&) const = default;
};

inline Settings DefaultSettings() {
  return {};
}

}  // namespace cx::config
