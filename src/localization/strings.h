#pragma once

#include <string_view>

namespace cx::localization {

enum class Locale {
  English,
  Arabic,
};

enum class StringId {
  NewTab,
  Reload,
  Go,
  Bookmark,
  Settings,
  Privacy,
  Agent,
  Mcp,
  Consent,
  Denied,
  Allowed,
  LocalOnly,
  Browser,
  SettingsAndPrivacy,
  PrivacyAndSecurity,
  AgentPermissions,
  McpAllowlist,
  DataAndStorage,
  Back,
  Forward,
  Navigate,
  CloseTab,
  SaveBookmarkLocally,
};

Locale LocaleFromName(std::wstring_view name) noexcept;
Locale DetectLocale() noexcept;
Locale CurrentLocale() noexcept;
bool IsRtl(Locale locale) noexcept;

std::wstring_view Lookup(
    StringId id,
    Locale locale) noexcept;

std::wstring_view Text(StringId id) noexcept;

}  // namespace cx::localization
