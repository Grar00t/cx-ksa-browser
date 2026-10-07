#include "localization/strings.h"

#include <windows.h>

#include <array>
#include <cwctype>
#include <iterator>

namespace cx::localization {
namespace {

struct Entry final {
  StringId id;
  const wchar_t* english;
  const wchar_t* arabic;
};

constexpr std::array<Entry, 23> kStrings{{
    {StringId::NewTab, L"New Tab",
     L"\u0639\u0644\u0627\u0645\u0629 \u062a\u0628\u0648\u064a\u0628 \u062c\u062f\u064a\u062f\u0629"},
    {StringId::Reload, L"Reload",
     L"\u0625\u0639\u0627\u062f\u0629 \u062a\u062d\u0645\u064a\u0644"},
    {StringId::Go, L"Go",
     L"\u0627\u0646\u062a\u0642\u0627\u0644"},
    {StringId::Bookmark, L"Bookmark",
     L"\u0625\u0634\u0627\u0631\u0629 \u0645\u0631\u062c\u0639\u064a\u0629"},
    {StringId::Settings, L"Settings",
     L"\u0627\u0644\u0625\u0639\u062f\u0627\u062f\u0627\u062a"},
    {StringId::Privacy, L"Privacy",
     L"\u0627\u0644\u062e\u0635\u0648\u0635\u064a\u0629"},
    {StringId::Agent, L"Agent",
     L"\u0627\u0644\u0648\u0643\u064a\u0644"},
    {StringId::Mcp, L"MCP", L"MCP"},
    {StringId::Consent, L"Consent",
     L"\u0627\u0644\u0645\u0648\u0627\u0641\u0642\u0629"},
    {StringId::Denied, L"Denied",
     L"\u0645\u0631\u0641\u0648\u0636"},
    {StringId::Allowed, L"Allowed",
     L"\u0645\u0633\u0645\u0648\u062d"},
    {StringId::LocalOnly, L"Local only",
     L"\u0645\u062d\u0644\u064a \u0641\u0642\u0637"},
    {StringId::Browser, L"Browser",
     L"\u0627\u0644\u0645\u062a\u0635\u0641\u062d"},
    {StringId::SettingsAndPrivacy, L"Settings & Privacy",
     L"\u0627\u0644\u0625\u0639\u062f\u0627\u062f\u0627\u062a \u0648\u0627\u0644\u062e\u0635\u0648\u0635\u064a\u0629"},
    {StringId::PrivacyAndSecurity, L"Privacy & Security",
     L"\u0627\u0644\u062e\u0635\u0648\u0635\u064a\u0629 \u0648\u0627\u0644\u0623\u0645\u0627\u0646"},
    {StringId::AgentPermissions, L"Agent Permissions",
     L"\u0623\u0630\u0648\u0646\u0627\u062a \u0627\u0644\u0648\u0643\u064a\u0644"},
    {StringId::McpAllowlist, L"MCP Allowlist",
     L"\u0642\u0627\u0626\u0645\u0629 MCP \u0627\u0644\u0645\u0633\u0645\u0648\u062d \u0628\u0647\u0627"},
    {StringId::DataAndStorage, L"Data & Storage",
     L"\u0627\u0644\u0628\u064a\u0627\u0646\u0627\u062a \u0648\u0627\u0644\u062a\u062e\u0632\u064a\u0646"},
    {StringId::Back, L"Back",
     L"\u0631\u062c\u0648\u0639"},
    {StringId::Forward, L"Forward",
     L"\u0625\u0644\u0649 \u0627\u0644\u0623\u0645\u0627\u0645"},
    {StringId::Navigate, L"Navigate",
     L"\u0627\u0646\u062a\u0642\u0627\u0644"},
    {StringId::CloseTab, L"Close Tab",
     L"\u0625\u063a\u0644\u0627\u0642 \u0639\u0644\u0627\u0645\u0629 \u0627\u0644\u062a\u0628\u0648\u064a\u0628"},
    {StringId::SaveBookmarkLocally, L"Save bookmark locally",
     L"\u062d\u0641\u0638 \u0627\u0644\u0625\u0634\u0627\u0631\u0629 \u0627\u0644\u0645\u0631\u062c\u0639\u064a\u0629 \u0645\u062d\u0644\u064a\u0627"},
}};

bool StartsWithInsensitive(
    std::wstring_view value,
    std::wstring_view prefix) noexcept {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::towlower(value[i]) !=
        std::towlower(prefix[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

Locale LocaleFromName(std::wstring_view name) noexcept {
  return StartsWithInsensitive(name, L"ar")
      ? Locale::Arabic
      : Locale::English;
}

Locale DetectLocale() noexcept {
  wchar_t override_value[32]{};
  const DWORD length = GetEnvironmentVariableW(
      L"CX_LOCALE",
      override_value,
      static_cast<DWORD>(std::size(override_value)));
  if (length > 0 && length < std::size(override_value)) {
    return LocaleFromName(
        std::wstring_view(override_value, length));
  }

  const LANGID language = GetUserDefaultUILanguage();
  return PRIMARYLANGID(language) == LANG_ARABIC
      ? Locale::Arabic
      : Locale::English;
}

Locale CurrentLocale() noexcept {
  static const Locale locale = DetectLocale();
  return locale;
}

bool IsRtl(Locale locale) noexcept {
  return locale == Locale::Arabic;
}

std::wstring_view Lookup(
    StringId id,
    Locale locale) noexcept {
  for (const auto& entry : kStrings) {
    if (entry.id != id) {
      continue;
    }
    if (locale == Locale::Arabic &&
        entry.arabic && entry.arabic[0] != L'\0') {
      return entry.arabic;
    }
    return entry.english;
  }
  return {};
}

std::wstring_view Text(StringId id) noexcept {
  return Lookup(id, CurrentLocale());
}

}  // namespace cx::localization
