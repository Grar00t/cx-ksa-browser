#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace cx::storage {

struct Tab {
  std::int64_t id = 0;
  int position = 0;
  std::string url;
  std::string title;
  bool pinned = false;
};

struct HistoryEntry {
  std::int64_t id = 0;
  std::string url;
  std::string title;
  std::int64_t visited_at = 0;
};

class Database {
public:
  explicit Database(std::filesystem::path path = DefaultPath());
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  static std::filesystem::path DefaultPath();

  bool Open();
  void Close();
  bool IsOpen() const noexcept;
  const std::filesystem::path& path() const noexcept;
  int SchemaVersion() const;

  bool SetSetting(std::string_view key, std::string_view value);
  std::optional<std::string> GetSetting(std::string_view key) const;
  bool DeleteSetting(std::string_view key);

  std::int64_t AddTab(int position, std::string_view url,
                      std::string_view title, bool pinned);
  bool UpdateTab(std::int64_t id, int position, std::string_view url,
                 std::string_view title, bool pinned);
  std::optional<Tab> GetTab(std::int64_t id) const;
  std::vector<Tab> ListTabs() const;
  bool DeleteTab(std::int64_t id);
  std::int64_t AddHistory(std::string_view url, std::string_view title,
                          std::int64_t visited_at);
  std::vector<HistoryEntry> ListHistory(std::size_t limit = 100) const;
  bool DeleteHistory(std::int64_t id);
  bool ClearHistory();

  bool SetPermission(std::string_view capability, bool granted);
  std::optional<bool> GetPermission(std::string_view capability) const;
  bool RevokeAllPermissions();

  bool BeginTransaction();
  bool Commit();
  bool Rollback();
  bool InTransaction() const noexcept;

private:
  bool Configure();
  bool RunMigrations();
  bool Exec(std::string_view sql) const;

  std::filesystem::path path_;
  sqlite3* db_ = nullptr;
  bool in_transaction_ = false;
};

class Transaction {
public:
  explicit Transaction(Database& database);
  ~Transaction();

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  bool ok() const noexcept;
  bool Commit();
  void Rollback();

private:
  Database* database_ = nullptr;
  bool active_ = false;
};

}  // namespace cx::storage
