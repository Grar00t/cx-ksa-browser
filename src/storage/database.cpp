#include "storage/database.h"

#include "storage/migrations.h"

#include <sqlite3.h>

#include <cstdlib>
#include <limits>
#include <system_error>
#include <utility>

namespace cx::storage {
namespace {

class Statement {
public:
  Statement(sqlite3* db, const char* sql) {
    if (db && sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      stmt_ = nullptr;
    }
  }

  ~Statement() {
    if (stmt_) {
      sqlite3_finalize(stmt_);
    }
  }

  sqlite3_stmt* get() const noexcept { return stmt_; }
  bool ok() const noexcept { return stmt_ != nullptr; }

private:
  sqlite3_stmt* stmt_ = nullptr;
};

bool BindText(sqlite3_stmt* stmt, int index, std::string_view value) {
  if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  return sqlite3_bind_text(stmt, index, value.data(),
                           static_cast<int>(value.size()),
                           SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string ColumnText(sqlite3_stmt* stmt, int column) {
  const auto* text = sqlite3_column_text(stmt, column);
  if (!text) {
    return {};
  }
  const int bytes = sqlite3_column_bytes(stmt, column);
  return std::string(reinterpret_cast<const char*>(text),
                     static_cast<std::size_t>(bytes));
}

bool StepDone(sqlite3_stmt* stmt) {
  return stmt && sqlite3_step(stmt) == SQLITE_DONE;
}

}  // namespace

Database::Database(std::filesystem::path path) : path_(std::move(path)) {}

Database::~Database() {
  Close();
}

std::filesystem::path Database::DefaultPath() {
#ifdef _WIN32
  wchar_t* appdata = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&appdata, &length, L"APPDATA") == 0 &&
      appdata != nullptr && length > 1) {
    std::filesystem::path result =
        std::filesystem::path(appdata) / L"CX Build" / L"data.db";
    std::free(appdata);
    return result;
  }
  std::free(appdata);
#endif
  return std::filesystem::temp_directory_path() /
         "CX Build" / "data.db";
}

bool Database::Open() {
  if (db_) {
    return true;
  }

  std::error_code error;
  const auto parent = path_.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return false;
    }
  }

#ifdef _WIN32
  const int rc = sqlite3_open16(path_.c_str(), &db_);
#else
  const int rc = sqlite3_open(path_.string().c_str(), &db_);
#endif
  if (rc != SQLITE_OK || !db_) {
    Close();
    return false;
  }

  sqlite3_extended_result_codes(db_, 1);
  if (!Configure() || !RunMigrations()) {
    Close();
    return false;
  }
  return true;
}

void Database::Close() {
  if (!db_) {
    return;
  }
  if (in_transaction_) {
    Rollback();
  }
  sqlite3_close_v2(db_);
  db_ = nullptr;
}

bool Database::IsOpen() const noexcept {
  return db_ != nullptr;
}

const std::filesystem::path& Database::path() const noexcept {
  return path_;
}

bool Database::Configure() {
  if (!db_) {
    return false;
  }
  if (sqlite3_busy_timeout(db_, 5000) != SQLITE_OK) {
    return false;
  }
  return Exec("PRAGMA foreign_keys=ON;") &&
         Exec("PRAGMA journal_mode=WAL;") &&
         Exec("PRAGMA synchronous=FULL;");
}

bool Database::Exec(std::string_view sql) const {
  if (!db_ || sql.empty() ||
      sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  std::string owned(sql);
  char* error = nullptr;
  const int rc = sqlite3_exec(db_, owned.c_str(), nullptr, nullptr, &error);
  if (error) {
    sqlite3_free(error);
  }
  return rc == SQLITE_OK;
}

int Database::SchemaVersion() const {
  Statement statement(db_,
      "SELECT COALESCE(MAX(version), 0) FROM schema_migrations;");
  if (!statement.ok() || sqlite3_step(statement.get()) != SQLITE_ROW) {
    return 0;
  }
  return sqlite3_column_int(statement.get(), 0);
}

bool Database::RunMigrations() {
  if (!Exec(
      "CREATE TABLE IF NOT EXISTS schema_migrations ("
      "version INTEGER PRIMARY KEY NOT NULL,"
      "applied_at INTEGER NOT NULL DEFAULT (unixepoch()));")) {
    return false;
  }

  int current = SchemaVersion();
  for (const auto& migration : kMigrations) {
    if (migration.version <= current) {
      continue;
    }

    Transaction transaction(*this);
    if (!transaction.ok() || !Exec(migration.sql)) {
      return false;
    }

    Statement insert(db_,
        "INSERT INTO schema_migrations(version) VALUES(?1);");
    if (!insert.ok() ||
        sqlite3_bind_int(insert.get(), 1, migration.version) != SQLITE_OK ||
        !StepDone(insert.get()) ||
        !transaction.Commit()) {
      return false;
    }
    current = migration.version;
  }
  return true;
}

bool Database::SetSetting(std::string_view key, std::string_view value) {
  Statement statement(db_,
      "INSERT INTO settings(key, value) VALUES(?1, ?2) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value, "
      "updated_at=unixepoch();");
  if (!statement.ok() ||
      !BindText(statement.get(), 1, key) ||
      !BindText(statement.get(), 2, value)) {
    return false;
  }
  return StepDone(statement.get());
}

std::optional<std::string> Database::GetSetting(std::string_view key) const {
  Statement statement(db_,
      "SELECT value FROM settings WHERE key=?1;");
  if (!statement.ok() || !BindText(statement.get(), 1, key)) {
    return std::nullopt;
  }

  const int rc = sqlite3_step(statement.get());
  if (rc != SQLITE_ROW) {
    return std::nullopt;
  }
  return ColumnText(statement.get(), 0);
}

bool Database::DeleteSetting(std::string_view key) {
  Statement statement(db_,
      "DELETE FROM settings WHERE key=?1;");
  if (!statement.ok() || !BindText(statement.get(), 1, key) ||
      !StepDone(statement.get())) {
    return false;
  }
  return sqlite3_changes(db_) == 1;
}

std::int64_t Database::AddTab(int position, std::string_view url,
                              std::string_view title, bool pinned) {
  Statement statement(db_,
      "INSERT INTO tabs(position, url, title, pinned) "
      "VALUES(?1, ?2, ?3, ?4);");
  if (!statement.ok() ||
      sqlite3_bind_int(statement.get(), 1, position) != SQLITE_OK ||
      !BindText(statement.get(), 2, url) ||
      !BindText(statement.get(), 3, title) ||
      sqlite3_bind_int(statement.get(), 4, pinned ? 1 : 0) != SQLITE_OK ||
      !StepDone(statement.get())) {
    return -1;
  }
  return sqlite3_last_insert_rowid(db_);
}

bool Database::UpdateTab(std::int64_t id, int position,
                         std::string_view url, std::string_view title,
                         bool pinned) {
  Statement statement(db_,
      "UPDATE tabs SET position=?1, url=?2, title=?3, pinned=?4, "
      "updated_at=unixepoch() WHERE id=?5;");
  if (!statement.ok() ||
      sqlite3_bind_int(statement.get(), 1, position) != SQLITE_OK ||
      !BindText(statement.get(), 2, url) ||
      !BindText(statement.get(), 3, title) ||
      sqlite3_bind_int(statement.get(), 4, pinned ? 1 : 0) != SQLITE_OK ||
      sqlite3_bind_int64(statement.get(), 5, id) != SQLITE_OK ||
      !StepDone(statement.get())) {
    return false;
  }
  return sqlite3_changes(db_) == 1;
}

std::optional<Tab> Database::GetTab(std::int64_t id) const {
  Statement statement(db_,
      "SELECT id, position, url, title, pinned "
      "FROM tabs WHERE id=?1;");
  if (!statement.ok() ||
      sqlite3_bind_int64(statement.get(), 1, id) != SQLITE_OK) {
    return std::nullopt;
  }
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return std::nullopt;
  }

  Tab tab;
  tab.id = sqlite3_column_int64(statement.get(), 0);
  tab.position = sqlite3_column_int(statement.get(), 1);
  tab.url = ColumnText(statement.get(), 2);
  tab.title = ColumnText(statement.get(), 3);
  tab.pinned = sqlite3_column_int(statement.get(), 4) != 0;
  return tab;
}

std::vector<Tab> Database::ListTabs() const {
  std::vector<Tab> tabs;
  Statement statement(db_,
      "SELECT id, position, url, title, pinned "
      "FROM tabs ORDER BY position ASC, id ASC;");
  if (!statement.ok()) {
    return tabs;
  }

  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    Tab tab;
    tab.id = sqlite3_column_int64(statement.get(), 0);
    tab.position = sqlite3_column_int(statement.get(), 1);
    tab.url = ColumnText(statement.get(), 2);
    tab.title = ColumnText(statement.get(), 3);
    tab.pinned = sqlite3_column_int(statement.get(), 4) != 0;
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

bool Database::DeleteTab(std::int64_t id) {
  Statement statement(db_,
      "DELETE FROM tabs WHERE id=?1;");
  if (!statement.ok() ||
      sqlite3_bind_int64(statement.get(), 1, id) != SQLITE_OK ||
      !StepDone(statement.get())) {
    return false;
  }
  return sqlite3_changes(db_) == 1;
}

std::int64_t Database::AddHistory(std::string_view url,
                                  std::string_view title,
                                  std::int64_t visited_at) {
  Statement statement(db_,
      "INSERT INTO history(url, title, visited_at) "
      "VALUES(?1, ?2, ?3);");
  if (!statement.ok() ||
      !BindText(statement.get(), 1, url) ||
      !BindText(statement.get(), 2, title) ||
      sqlite3_bind_int64(statement.get(), 3, visited_at) != SQLITE_OK ||
      !StepDone(statement.get())) {
    return -1;
  }
  return sqlite3_last_insert_rowid(db_);
}

std::vector<HistoryEntry> Database::ListHistory(std::size_t limit) const {
  std::vector<HistoryEntry> entries;
  Statement statement(db_,
      "SELECT id, url, title, visited_at FROM history "
      "ORDER BY visited_at DESC, id DESC LIMIT ?1;");
  if (!statement.ok()) {
    return entries;
  }

  const auto bounded =
      limit > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
          ? std::numeric_limits<std::int64_t>::max()
          : static_cast<std::int64_t>(limit);
  if (sqlite3_bind_int64(statement.get(), 1, bounded) != SQLITE_OK) {
    return entries;
  }

  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    HistoryEntry entry;
    entry.id = sqlite3_column_int64(statement.get(), 0);
    entry.url = ColumnText(statement.get(), 1);
    entry.title = ColumnText(statement.get(), 2);
    entry.visited_at = sqlite3_column_int64(statement.get(), 3);
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::vector<HistoryEntry> Database::SearchHistory(
    std::string_view query, std::size_t limit) const {
  if (query.empty()) {
    return ListHistory(limit);
  }

  std::vector<HistoryEntry> entries;
  Statement statement(db_,
      "SELECT id, url, title, visited_at FROM history "
      "WHERE url LIKE ?1 COLLATE NOCASE "
      "OR title LIKE ?1 COLLATE NOCASE "
      "ORDER BY visited_at DESC, id DESC LIMIT ?2;");
  if (!statement.ok()) {
    return entries;
  }

  const std::string pattern = "%" + std::string(query) + "%";
  const auto bounded =
      limit > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())
          ? std::numeric_limits<std::int64_t>::max()
          : static_cast<std::int64_t>(limit);
  if (!BindText(statement.get(), 1, pattern) ||
      sqlite3_bind_int64(statement.get(), 2, bounded) != SQLITE_OK) {
    return entries;
  }

  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    HistoryEntry entry;
    entry.id = sqlite3_column_int64(statement.get(), 0);
    entry.url = ColumnText(statement.get(), 1);
    entry.title = ColumnText(statement.get(), 2);
    entry.visited_at = sqlite3_column_int64(statement.get(), 3);
    entries.push_back(std::move(entry));
  }
  return entries;
}

bool Database::DeleteHistory(std::int64_t id) {
  Statement statement(db_,
      "DELETE FROM history WHERE id=?1;");
  if (!statement.ok() ||
      sqlite3_bind_int64(statement.get(), 1, id) != SQLITE_OK ||
      !StepDone(statement.get())) {
    return false;
  }
  return sqlite3_changes(db_) == 1;
}

bool Database::ClearHistory() {
  return Exec("DELETE FROM history;");
}

std::int64_t Database::AddBookmark(
    std::string_view url, std::string_view title) {
  Statement upsert(db_,
      "INSERT INTO bookmarks(url, title) VALUES(?1, ?2) "
      "ON CONFLICT(url) DO UPDATE SET title=excluded.title;");
  if (!upsert.ok() ||
      !BindText(upsert.get(), 1, url) ||
      !BindText(upsert.get(), 2, title) ||
      !StepDone(upsert.get())) {
    return -1;
  }

  Statement select(db_,
      "SELECT id FROM bookmarks WHERE url=?1;");
  if (!select.ok() || !BindText(select.get(), 1, url) ||
      sqlite3_step(select.get()) != SQLITE_ROW) {
    return -1;
  }
  return sqlite3_column_int64(select.get(), 0);
}

std::vector<Bookmark> Database::ListBookmarks() const {
  std::vector<Bookmark> bookmarks;
  Statement statement(db_,
      "SELECT id, url, title, created_at FROM bookmarks "
      "ORDER BY created_at DESC, id DESC;");
  if (!statement.ok()) {
    return bookmarks;
  }

  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    Bookmark bookmark;
    bookmark.id = sqlite3_column_int64(statement.get(), 0);
    bookmark.url = ColumnText(statement.get(), 1);
    bookmark.title = ColumnText(statement.get(), 2);
    bookmark.created_at = sqlite3_column_int64(statement.get(), 3);
    bookmarks.push_back(std::move(bookmark));
  }
  return bookmarks;
}

bool Database::DeleteBookmark(std::int64_t id) {
  Statement statement(db_,
      "DELETE FROM bookmarks WHERE id=?1;");
  if (!statement.ok() ||
      sqlite3_bind_int64(statement.get(), 1, id) != SQLITE_OK ||
      !StepDone(statement.get())) {
    return false;
  }
  return sqlite3_changes(db_) == 1;
}

bool Database::SetPermission(std::string_view capability, bool granted) {
  Statement statement(db_,
      "INSERT INTO permissions(capability, granted) VALUES(?1, ?2) "
      "ON CONFLICT(capability) DO UPDATE SET granted=excluded.granted, "
      "updated_at=unixepoch();");
  if (!statement.ok() ||
      !BindText(statement.get(), 1, capability) ||
      sqlite3_bind_int(statement.get(), 2, granted ? 1 : 0) != SQLITE_OK) {
    return false;
  }
  return StepDone(statement.get());
}

std::optional<bool> Database::GetPermission(
    std::string_view capability) const {
  Statement statement(db_,
      "SELECT granted FROM permissions WHERE capability=?1;");
  if (!statement.ok() || !BindText(statement.get(), 1, capability)) {
    return std::nullopt;
  }

  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return std::nullopt;
  }
  return sqlite3_column_int(statement.get(), 0) != 0;
}

bool Database::RevokeAllPermissions() {
  return Exec("UPDATE permissions SET granted=0, updated_at=unixepoch();");
}

bool Database::BeginTransaction() {
  if (!db_ || in_transaction_) {
    return false;
  }
  if (!Exec("BEGIN IMMEDIATE;")) {
    return false;
  }
  in_transaction_ = true;
  return true;
}

bool Database::Commit() {
  if (!db_ || !in_transaction_) {
    return false;
  }
  if (!Exec("COMMIT;")) {
    return false;
  }
  in_transaction_ = false;
  return true;
}

bool Database::Rollback() {
  if (!db_ || !in_transaction_) {
    return false;
  }
  const bool ok = Exec("ROLLBACK;");
  if (ok) {
    in_transaction_ = false;
  }
  return ok;
}

bool Database::InTransaction() const noexcept {
  return in_transaction_;
}

Transaction::Transaction(Database& database) : database_(&database) {
  active_ = database_->BeginTransaction();
}

Transaction::~Transaction() {
  if (active_ && database_) {
    database_->Rollback();
  }
}

bool Transaction::ok() const noexcept {
  return active_;
}

bool Transaction::Commit() {
  if (!active_ || !database_) {
    return false;
  }
  if (!database_->Commit()) {
    return false;
  }
  active_ = false;
  return true;
}

void Transaction::Rollback() {
  if (active_ && database_) {
    database_->Rollback();
    active_ = false;
  }
}

}  // namespace cx::storage
