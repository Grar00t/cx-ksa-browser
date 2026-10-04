#pragma once

#include <filesystem>

namespace cx::storage {
class Database;
}

namespace cx::config {

class BackupRestore {
public:
  static bool BackupDatabase(
      const storage::Database& database,
      const std::filesystem::path& destination);

  static bool RestoreDatabase(
      storage::Database& database,
      const std::filesystem::path& backup);

  static bool VerifyDatabase(
      const std::filesystem::path& path);

private:
  static bool CopyDatabase(
      const std::filesystem::path& source,
      const std::filesystem::path& destination);
};

}  // namespace cx::config
