#pragma once

#include "config/settings_schema.h"

#include <filesystem>
#include <string_view>

namespace cx::config {

enum class LoadStatus {
  Loaded,
  CreatedDefaults,
  RecoveredFromCorrupt,
  Failed,
};

class ConfigManager {
public:
  explicit ConfigManager(
      std::filesystem::path path = DefaultPath());

  static std::filesystem::path DefaultPath();

  LoadStatus Load();
  bool Save() const;

  const Settings& settings() const noexcept;
  Settings& mutable_settings() noexcept;
  void ResetToDefaults();

  bool Set(const Settings& settings);

  bool ImportFrom(const std::filesystem::path& source);
  bool ExportTo(const std::filesystem::path& destination) const;

  const std::filesystem::path& path() const noexcept;

  static bool Validate(const Settings& settings);
  static bool IsLocalPath(const std::filesystem::path& path);

private:
  static bool Parse(
      std::string_view json,
      Settings* settings);
  static std::string Serialize(const Settings& settings);
  static bool ReadFile(
      const std::filesystem::path& path,
      std::string* contents);
  static bool WriteFileAtomic(
      const std::filesystem::path& path,
      std::string_view contents);

  std::filesystem::path path_;
  Settings settings_ = DefaultSettings();
};

}  // namespace cx::config
