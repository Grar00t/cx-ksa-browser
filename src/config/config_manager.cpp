#include "config/config_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

namespace cx::config {
namespace {

constexpr std::uintmax_t kMaxConfigBytes =
    1024 * 1024;

class Cursor {
public:
  explicit Cursor(std::string_view input)
      : input_(input) {}

  void Skip() {
    while (position_ < input_.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   input_[position_]))) {
      ++position_;
    }
  }

  bool Consume(char expected) {
    Skip();
    if (position_ >= input_.size() ||
        input_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  bool String(std::string* output) {
    Skip();
    if (position_ >= input_.size() ||
        input_[position_] != '"') {
      return false;
    }
    ++position_;
    output->clear();

    while (position_ < input_.size()) {
      const char ch = input_[position_++];
      if (ch == '"') {
        return true;
      }
      if (static_cast<unsigned char>(ch) < 0x20) {
        return false;
      }
      if (ch != '\\') {
        output->push_back(ch);
        continue;
      }

      if (position_ >= input_.size()) {
        return false;
      }
      const char escaped = input_[position_++];
      switch (escaped) {
        case '"': output->push_back('"'); break;
        case '\\': output->push_back('\\'); break;
        case '/': output->push_back('/'); break;
        case 'b': output->push_back('\b'); break;
        case 'f': output->push_back('\f'); break;
        case 'n': output->push_back('\n'); break;
        case 'r': output->push_back('\r'); break;
        case 't': output->push_back('\t'); break;
        default: return false;
      }
    }
    return false;
  }

  bool Integer(int* output) {
    Skip();
    if (position_ >= input_.size()) {
      return false;
    }

    bool negative = false;
    if (input_[position_] == '-') {
      negative = true;
      ++position_;
    }

    if (position_ >= input_.size() ||
        !std::isdigit(
            static_cast<unsigned char>(
                input_[position_]))) {
      return false;
    }

    long long value = 0;
    while (position_ < input_.size() &&
           std::isdigit(
               static_cast<unsigned char>(
                   input_[position_]))) {
      const int digit =
          input_[position_] - '0';
      if (value >
          (std::numeric_limits<int>::max() -
           digit) /
              10) {
        return false;
      }
      value = value * 10 + digit;
      ++position_;
    }

    if (negative) {
      value = -value;
    }
    if (value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
      return false;
    }
    *output = static_cast<int>(value);
    return true;
  }

  bool Boolean(bool* output) {
    Skip();
    if (input_.substr(position_, 4) == "true") {
      position_ += 4;
      *output = true;
      return true;
    }
    if (input_.substr(position_, 5) == "false") {
      position_ += 5;
      *output = false;
      return true;
    }
    return false;
  }

  bool End() {
    Skip();
    return position_ == input_.size();
  }

  bool NextMemberOrEnd() {
    Skip();
    if (position_ < input_.size() &&
        input_[position_] == '}') {
      ++position_;
      return false;
    }
    return Consume(',');
  }

private:
  std::string_view input_;
  std::size_t position_ = 0;
};

const char* ToString(StartupBehavior value) {
  switch (value) {
    case StartupBehavior::Blank: return "blank";
    case StartupBehavior::RestoreSession:
      return "restore_session";
  }
  return "";
}

const char* ToString(SearchProvider value) {
  switch (value) {
    case SearchProvider::Disabled: return "disabled";
    case SearchProvider::DuckDuckGo: return "duckduckgo";
    case SearchProvider::Bing: return "bing";
    case SearchProvider::Google: return "google";
  }
  return "";
}

const char* ToString(Theme value) {
  switch (value) {
    case Theme::System: return "system";
    case Theme::Light: return "light";
    case Theme::Dark: return "dark";
  }
  return "";
}

const char* ToString(CookiePolicy value) {
  switch (value) {
    case CookiePolicy::BlockAll: return "block_all";
    case CookiePolicy::SessionOnly: return "session_only";
    case CookiePolicy::AllowPersistent:
      return "allow_persistent";
  }
  return "";
}

const char* ToString(CachePolicy value) {
  switch (value) {
    case CachePolicy::MemoryOnly: return "memory_only";
    case CachePolicy::SessionDisk: return "session_disk";
    case CachePolicy::Persistent: return "persistent";
  }
  return "";
}

bool ParseStartup(
    std::string_view value,
    StartupBehavior* output) {
  if (value == "blank") {
    *output = StartupBehavior::Blank;
    return true;
  }
  if (value == "restore_session") {
    *output = StartupBehavior::RestoreSession;
    return true;
  }
  return false;
}

bool ParseSearch(
    std::string_view value,
    SearchProvider* output) {
  if (value == "disabled") {
    *output = SearchProvider::Disabled;
    return true;
  }
  if (value == "duckduckgo") {
    *output = SearchProvider::DuckDuckGo;
    return true;
  }
  if (value == "bing") {
    *output = SearchProvider::Bing;
    return true;
  }
  if (value == "google") {
    *output = SearchProvider::Google;
    return true;
  }
  return false;
}

bool ParseTheme(
    std::string_view value,
    Theme* output) {
  if (value == "system") {
    *output = Theme::System;
    return true;
  }
  if (value == "light") {
    *output = Theme::Light;
    return true;
  }
  if (value == "dark") {
    *output = Theme::Dark;
    return true;
  }
  return false;
}

bool ParseCookies(
    std::string_view value,
    CookiePolicy* output) {
  if (value == "block_all") {
    *output = CookiePolicy::BlockAll;
    return true;
  }
  if (value == "session_only") {
    *output = CookiePolicy::SessionOnly;
    return true;
  }
  if (value == "allow_persistent") {
    *output = CookiePolicy::AllowPersistent;
    return true;
  }
  return false;
}

bool ParseCache(
    std::string_view value,
    CachePolicy* output) {
  if (value == "memory_only") {
    *output = CachePolicy::MemoryOnly;
    return true;
  }
  if (value == "session_disk") {
    *output = CachePolicy::SessionDisk;
    return true;
  }
  if (value == "persistent") {
    *output = CachePolicy::Persistent;
    return true;
  }
  return false;
}

bool ParseGeneral(
    Cursor* cursor,
    GeneralSettings* settings) {
  if (!cursor->Consume('{')) {
    return false;
  }
  bool startup = false;
  bool search = false;

  while (true) {
    if (cursor->Consume('}')) {
      return startup && search;
    }

    std::string key;
    if (!cursor->String(&key) ||
        !cursor->Consume(':')) {
      return false;
    }

    std::string value;
    if (!cursor->String(&value)) {
      return false;
    }

    if (key == "startup") {
      if (startup ||
          !ParseStartup(value, &settings->startup)) {
        return false;
      }
      startup = true;
    } else if (key == "default_search") {
      if (search ||
          !ParseSearch(
              value,
              &settings->default_search)) {
        return false;
      }
      search = true;
    } else {
      return false;
    }

    if (cursor->Consume('}')) {
      return startup && search;
    }
    if (!cursor->Consume(',')) {
      return false;
    }
  }
}

bool ParseAppearance(
    Cursor* cursor,
    AppearanceSettings* settings) {
  if (!cursor->Consume('{')) {
    return false;
  }
  bool theme = false;
  bool font = false;

  while (true) {
    if (cursor->Consume('}')) {
      return theme && font;
    }

    std::string key;
    if (!cursor->String(&key) ||
        !cursor->Consume(':')) {
      return false;
    }

    if (key == "theme") {
      if (theme) {
        return false;
      }
      std::string value;
      if (!cursor->String(&value) ||
          !ParseTheme(value, &settings->theme)) {
        return false;
      }
      theme = true;
    } else if (key == "font_size_percent") {
      if (font ||
          !cursor->Integer(
              &settings->font_size_percent)) {
        return false;
      }
      font = true;
    } else {
      return false;
    }

    if (cursor->Consume('}')) {
      return theme && font;
    }
    if (!cursor->Consume(',')) {
      return false;
    }
  }
}

bool ParsePrivacy(
    Cursor* cursor,
    PrivacySettings* settings) {
  if (!cursor->Consume('{')) {
    return false;
  }
  bool cookies = false;
  bool cache = false;

  while (true) {
    if (cursor->Consume('}')) {
      return cookies && cache;
    }

    std::string key;
    if (!cursor->String(&key) ||
        !cursor->Consume(':')) {
      return false;
    }

    std::string value;
    if (!cursor->String(&value)) {
      return false;
    }

    if (key == "cookies") {
      if (cookies ||
          !ParseCookies(value, &settings->cookies)) {
        return false;
      }
      cookies = true;
    } else if (key == "cache") {
      if (cache ||
          !ParseCache(value, &settings->cache)) {
        return false;
      }
      cache = true;
    } else {
      return false;
    }

    if (cursor->Consume('}')) {
      return cookies && cache;
    }
    if (!cursor->Consume(',')) {
      return false;
    }
  }
}

bool ParseAdvanced(
    Cursor* cursor,
    AdvancedSettings* settings) {
  if (!cursor->Consume('{')) {
    return false;
  }
  bool devtools = false;
  bool experimental = false;

  while (true) {
    if (cursor->Consume('}')) {
      return devtools && experimental;
    }

    std::string key;
    if (!cursor->String(&key) ||
        !cursor->Consume(':')) {
      return false;
    }

    if (key == "developer_tools") {
      if (devtools ||
          !cursor->Boolean(
              &settings->developer_tools)) {
        return false;
      }
      devtools = true;
    } else if (key == "experimental") {
      if (experimental ||
          !cursor->Boolean(
              &settings->experimental)) {
        return false;
      }
      experimental = true;
    } else {
      return false;
    }

    if (cursor->Consume('}')) {
      return devtools && experimental;
    }
    if (!cursor->Consume(',')) {
      return false;
    }
  }
}

bool ParseRoot(
    std::string_view json,
    Settings* settings) {
  Cursor cursor(json);
  if (!cursor.Consume('{')) {
    return false;
  }

  bool version = false;
  bool general = false;
  bool appearance = false;
  bool privacy = false;
  bool advanced = false;

  Settings parsed = DefaultSettings();

  while (true) {
    if (cursor.Consume('}')) {
      break;
    }

    std::string key;
    if (!cursor.String(&key) ||
        !cursor.Consume(':')) {
      return false;
    }

    if (key == "version") {
      if (version ||
          !cursor.Integer(&parsed.version)) {
        return false;
      }
      version = true;
    } else if (key == "general") {
      if (general ||
          !ParseGeneral(
              &cursor, &parsed.general)) {
        return false;
      }
      general = true;
    } else if (key == "appearance") {
      if (appearance ||
          !ParseAppearance(
              &cursor, &parsed.appearance)) {
        return false;
      }
      appearance = true;
    } else if (key == "privacy") {
      if (privacy ||
          !ParsePrivacy(
              &cursor, &parsed.privacy)) {
        return false;
      }
      privacy = true;
    } else if (key == "advanced") {
      if (advanced ||
          !ParseAdvanced(
              &cursor, &parsed.advanced)) {
        return false;
      }
      advanced = true;
    } else {
      return false;
    }

    if (cursor.Consume('}')) {
      break;
    }
    if (!cursor.Consume(',')) {
      return false;
    }
  }

  if (!cursor.End() ||
      !version || !general || !appearance ||
      !privacy || !advanced) {
    return false;
  }

  *settings = parsed;
  return true;
}

}  // namespace

ConfigManager::ConfigManager(
    std::filesystem::path path)
    : path_(std::move(path)) {}

std::filesystem::path ConfigManager::DefaultPath() {
  wchar_t* appdata = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(
          &appdata, &length, L"APPDATA") == 0 &&
      appdata != nullptr && length > 1) {
    std::filesystem::path result =
        std::filesystem::path(appdata) /
        L"CX Build" / L"config.json";
    std::free(appdata);
    return result;
  }
  std::free(appdata);
  return std::filesystem::temp_directory_path() /
      "CX Build" / "config.json";
}

LoadStatus ConfigManager::Load() {
  settings_ = DefaultSettings();

  std::error_code error;
  if (!std::filesystem::exists(path_, error)) {
    if (error || !Save()) {
      return LoadStatus::Failed;
    }
    return LoadStatus::CreatedDefaults;
  }

  std::string contents;
  Settings parsed;
  if (ReadFile(path_, &contents) &&
      Parse(contents, &parsed) &&
      Validate(parsed)) {
    settings_ = parsed;
    return LoadStatus::Loaded;
  }

  const auto corrupt =
      std::filesystem::path(
          path_.wstring() + L".corrupt");
  CopyFileW(
      path_.c_str(), corrupt.c_str(), FALSE);

  settings_ = DefaultSettings();
  if (!Save()) {
    return LoadStatus::Failed;
  }
  return LoadStatus::RecoveredFromCorrupt;
}

bool ConfigManager::Save() const {
  if (!Validate(settings_) ||
      !IsLocalPath(path_)) {
    return false;
  }
  return WriteFileAtomic(
      path_, Serialize(settings_));
}

const Settings&
ConfigManager::settings() const noexcept {
  return settings_;
}

Settings&
ConfigManager::mutable_settings() noexcept {
  return settings_;
}

void ConfigManager::ResetToDefaults() {
  settings_ = DefaultSettings();
}

bool ConfigManager::Set(
    const Settings& settings) {
  if (!Validate(settings)) {
    return false;
  }

  const Settings previous = settings_;
  settings_ = settings;
  if (!Save()) {
    settings_ = previous;
    return false;
  }
  return true;
}

bool ConfigManager::ImportFrom(
    const std::filesystem::path& source) {
  if (!IsLocalPath(source)) {
    return false;
  }

  std::string contents;
  Settings imported;
  if (!ReadFile(source, &contents) ||
      !Parse(contents, &imported) ||
      !Validate(imported)) {
    return false;
  }

  return Set(imported);
}

bool ConfigManager::ExportTo(
    const std::filesystem::path& destination) const {
  if (!IsLocalPath(destination) ||
      !Validate(settings_)) {
    return false;
  }
  return WriteFileAtomic(
      destination, Serialize(settings_));
}

const std::filesystem::path&
ConfigManager::path() const noexcept {
  return path_;
}

bool ConfigManager::Validate(
    const Settings& settings) {
  if (settings.version != 1) {
    return false;
  }
  if (settings.appearance.font_size_percent < 75 ||
      settings.appearance.font_size_percent > 200) {
    return false;
  }

  switch (settings.general.startup) {
    case StartupBehavior::Blank:
    case StartupBehavior::RestoreSession:
      break;
    default:
      return false;
  }

  switch (settings.general.default_search) {
    case SearchProvider::Disabled:
    case SearchProvider::DuckDuckGo:
    case SearchProvider::Bing:
    case SearchProvider::Google:
      break;
    default:
      return false;
  }

  switch (settings.appearance.theme) {
    case Theme::System:
    case Theme::Light:
    case Theme::Dark:
      break;
    default:
      return false;
  }

  switch (settings.privacy.cookies) {
    case CookiePolicy::BlockAll:
    case CookiePolicy::SessionOnly:
    case CookiePolicy::AllowPersistent:
      break;
    default:
      return false;
  }

  switch (settings.privacy.cache) {
    case CachePolicy::MemoryOnly:
    case CachePolicy::SessionDisk:
    case CachePolicy::Persistent:
      break;
    default:
      return false;
  }

  return true;
}

bool ConfigManager::IsLocalPath(
    const std::filesystem::path& path) {
  if (!path.is_absolute()) {
    return false;
  }

  const std::wstring native = path.native();
  if (native.rfind(L"\\\\", 0) == 0) {
    return false;
  }

  const std::wstring root =
      path.root_path().wstring();
  if (!root.empty() &&
      GetDriveTypeW(root.c_str()) == DRIVE_REMOTE) {
    return false;
  }
  return true;
}

bool ConfigManager::Parse(
    std::string_view json,
    Settings* settings) {
  if (!settings) {
    return false;
  }
  if (json.size() >= 3 &&
      static_cast<unsigned char>(json[0]) == 0xEF &&
      static_cast<unsigned char>(json[1]) == 0xBB &&
      static_cast<unsigned char>(json[2]) == 0xBF) {
    json.remove_prefix(3);
  }
  return ParseRoot(json, settings);
}

std::string ConfigManager::Serialize(
    const Settings& settings) {
  std::string json;
  json.reserve(512);
  json += "{\n";
  json += "  \"version\": 1,\n";
  json += "  \"general\": {\n";
  json += "    \"startup\": \"";
  json += ToString(settings.general.startup);
  json += "\",\n";
  json += "    \"default_search\": \"";
  json += ToString(settings.general.default_search);
  json += "\"\n";
  json += "  },\n";
  json += "  \"appearance\": {\n";
  json += "    \"theme\": \"";
  json += ToString(settings.appearance.theme);
  json += "\",\n";
  json += "    \"font_size_percent\": ";
  json += std::to_string(
      settings.appearance.font_size_percent);
  json += "\n";
  json += "  },\n";
  json += "  \"privacy\": {\n";
  json += "    \"cookies\": \"";
  json += ToString(settings.privacy.cookies);
  json += "\",\n";
  json += "    \"cache\": \"";
  json += ToString(settings.privacy.cache);
  json += "\"\n";
  json += "  },\n";
  json += "  \"advanced\": {\n";
  json += "    \"developer_tools\": ";
  json += settings.advanced.developer_tools
      ? "true"
      : "false";
  json += ",\n";
  json += "    \"experimental\": ";
  json += settings.advanced.experimental
      ? "true"
      : "false";
  json += "\n";
  json += "  }\n";
  json += "}\n";
  return json;
}

bool ConfigManager::ReadFile(
    const std::filesystem::path& path,
    std::string* contents) {
  if (!contents ||
      !IsLocalPath(path)) {
    return false;
  }

  std::error_code error;
  const auto size =
      std::filesystem::file_size(path, error);
  if (error || size > kMaxConfigBytes) {
    return false;
  }

  std::ifstream input(
      path, std::ios::binary);
  if (!input) {
    return false;
  }

  *contents = std::string{
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()};
  return input.good() || input.eof();
}

bool ConfigManager::WriteFileAtomic(
    const std::filesystem::path& path,
    std::string_view contents) {
  if (!IsLocalPath(path) ||
      contents.size() > kMaxConfigBytes) {
    return false;
  }

  std::error_code error;
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(
        parent, error);
    if (error) {
      return false;
    }
  }

  const std::filesystem::path temporary(
      path.wstring() + L".tmp");
  {
    std::ofstream output(
        temporary,
        std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    output.write(
        contents.data(),
        static_cast<std::streamsize>(
            contents.size()));
    output.flush();
    if (!output.good()) {
      output.close();
      std::filesystem::remove(
          temporary, error);
      return false;
    }
  }

  if (!MoveFileExW(
          temporary.c_str(),
          path.c_str(),
          MOVEFILE_REPLACE_EXISTING |
              MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(
        temporary, error);
    return false;
  }

  return true;
}

}  // namespace cx::config
