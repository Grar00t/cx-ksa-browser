#include "config/backup_restore.h"

#include "config/config_manager.h"
#include "storage/database.h"
#include "storage/migrations.h"

#include <sqlite3.h>
#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace cx::config {
namespace {

std::string PathUtf8(
    const std::filesystem::path& path) {
  const std::wstring wide = path.wstring();
  if (wide.empty()) {
    return {};
  }

  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS,
      wide.data(), static_cast<int>(wide.size()),
      nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }

  std::string utf8(
      static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS,
          wide.data(), static_cast<int>(wide.size()),
          utf8.data(), size, nullptr, nullptr) != size) {
    return {};
  }
  return utf8;
}

void Close(sqlite3** db) {
  if (*db) {
    sqlite3_close_v2(*db);
    *db = nullptr;
  }
}

bool CanPrepare(sqlite3* db, const char* sql) {
  sqlite3_stmt* statement = nullptr;
  const bool ok =
      sqlite3_prepare_v2(
          db, sql, -1, &statement, nullptr) == SQLITE_OK;
  if (statement) {
    sqlite3_finalize(statement);
  }
  return ok;
}

}  // namespace

bool BackupRestore::BackupDatabase(
    const storage::Database& database,
    const std::filesystem::path& destination) {
  if (!database.IsOpen() ||
      !ConfigManager::IsLocalPath(destination)) {
    return false;
  }

  const auto temporary =
      std::filesystem::path(
          destination.wstring() + L".tmp");
  std::error_code error;
  std::filesystem::remove(temporary, error);

  if (!CopyDatabase(
          database.path(), temporary) ||
      !VerifyDatabase(temporary)) {
    std::filesystem::remove(
        temporary, error);
    return false;
  }

  const auto parent = destination.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(
        parent, error);
    if (error) {
      std::filesystem::remove(
          temporary, error);
      return false;
    }
  }

  if (!MoveFileExW(
          temporary.c_str(),
          destination.c_str(),
          MOVEFILE_REPLACE_EXISTING |
              MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(
        temporary, error);
    return false;
  }
  return true;
}

bool BackupRestore::RestoreDatabase(
    storage::Database& database,
    const std::filesystem::path& backup) {
  if (!database.IsOpen() ||
      !ConfigManager::IsLocalPath(backup) ||
      !VerifyDatabase(backup)) {
    return false;
  }

  const auto target = database.path();
  const auto staged =
      std::filesystem::path(
          target.wstring() + L".restore.tmp");
  const auto safety =
      std::filesystem::path(
          target.wstring() + L".pre-restore");

  std::error_code error;
  std::filesystem::remove(staged, error);
  std::filesystem::remove(safety, error);

  if (!CopyDatabase(backup, staged) ||
      !VerifyDatabase(staged)) {
    std::filesystem::remove(staged, error);
    return false;
  }

  database.Close();

  std::filesystem::remove(
      std::filesystem::path(
          target.wstring() + L"-wal"),
      error);
  std::filesystem::remove(
      std::filesystem::path(
          target.wstring() + L"-shm"),
      error);

  const bool had_original =
      std::filesystem::exists(target, error) &&
      !error;

  if (had_original &&
      !MoveFileExW(
          target.c_str(),
          safety.c_str(),
          MOVEFILE_REPLACE_EXISTING |
              MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(staged, error);
    database.Open();
    return false;
  }

  if (!MoveFileExW(
          staged.c_str(),
          target.c_str(),
          MOVEFILE_REPLACE_EXISTING |
              MOVEFILE_WRITE_THROUGH)) {
    if (had_original) {
      MoveFileExW(
          safety.c_str(),
          target.c_str(),
          MOVEFILE_REPLACE_EXISTING |
              MOVEFILE_WRITE_THROUGH);
    }
    database.Open();
    std::filesystem::remove(staged, error);
    return false;
  }

  if (!database.Open() ||
      !VerifyDatabase(target)) {
    database.Close();
    std::filesystem::remove(target, error);
    if (had_original) {
      MoveFileExW(
          safety.c_str(),
          target.c_str(),
          MOVEFILE_REPLACE_EXISTING |
              MOVEFILE_WRITE_THROUGH);
    }
    database.Open();
    return false;
  }

  std::filesystem::remove(safety, error);
  return true;
}

bool BackupRestore::VerifyDatabase(
    const std::filesystem::path& path) {
  if (!ConfigManager::IsLocalPath(path)) {
    return false;
  }

  const std::string utf8 = PathUtf8(path);
  if (utf8.empty()) {
    return false;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open_v2(
          utf8.c_str(),
          &db,
          SQLITE_OPEN_READONLY |
              SQLITE_OPEN_FULLMUTEX,
          nullptr) != SQLITE_OK) {
    Close(&db);
    return false;
  }

  sqlite3_stmt* statement = nullptr;
  const bool prepared =
      sqlite3_prepare_v2(
          db,
          "PRAGMA integrity_check;",
          -1,
          &statement,
          nullptr) == SQLITE_OK;
  bool ok = false;
  if (prepared &&
      sqlite3_step(statement) == SQLITE_ROW) {
    const unsigned char* text =
        sqlite3_column_text(statement, 0);
    ok = text &&
        std::string(
            reinterpret_cast<const char*>(text)) ==
            "ok";
  }

  if (statement) {
    sqlite3_finalize(statement);
    statement = nullptr;
  }

  if (ok) {
    ok = sqlite3_prepare_v2(
        db,
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type='table' AND name IN "
        "('settings','tabs','history','permissions',"
        "'bookmarks','schema_migrations','scoped_permissions');",
        -1,
        &statement,
        nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW &&
        sqlite3_column_int(statement, 0) == 7;
  }

  if (statement) {
    sqlite3_finalize(statement);
    statement = nullptr;
  }

  if (ok) {
    ok = sqlite3_prepare_v2(
        db,
        "SELECT COALESCE(MAX(version), 0) "
        "FROM schema_migrations;",
        -1,
        &statement,
        nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW &&
        sqlite3_column_int(statement, 0) ==
            storage::kMigrations.back().version;
  }

  if (statement) {
    sqlite3_finalize(statement);
    statement = nullptr;
  }

  if (ok) {
    constexpr std::array<const char*, 7> schema_probes{{
        "SELECT key, value, updated_at FROM settings LIMIT 0;",
        "SELECT id, position, url, title, pinned, created_at, updated_at "
        "FROM tabs LIMIT 0;",
        "SELECT id, url, title, visited_at FROM history LIMIT 0;",
        "SELECT capability, granted, updated_at FROM permissions LIMIT 0;",
        "SELECT id, url, title, created_at FROM bookmarks LIMIT 0;",
        "SELECT version, applied_at FROM schema_migrations LIMIT 0;",
        "SELECT tab_id, origin, capability, granted, updated_at "
        "FROM scoped_permissions LIMIT 0;",
    }};
    for (const char* probe : schema_probes) {
      if (!CanPrepare(db, probe)) {
        ok = false;
        break;
      }
    }
  }

  Close(&db);
  return ok;
}

bool BackupRestore::CopyDatabase(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
  if (!ConfigManager::IsLocalPath(source) ||
      !ConfigManager::IsLocalPath(destination)) {
    return false;
  }

  const std::string source_utf8 =
      PathUtf8(source);
  const std::string destination_utf8 =
      PathUtf8(destination);
  if (source_utf8.empty() ||
      destination_utf8.empty()) {
    return false;
  }

  std::error_code error;
  const auto parent = destination.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(
        parent, error);
    if (error) {
      return false;
    }
  }
  std::filesystem::remove(
      destination, error);

  sqlite3* source_db = nullptr;
  sqlite3* destination_db = nullptr;

  if (sqlite3_open_v2(
          source_utf8.c_str(),
          &source_db,
          SQLITE_OPEN_READONLY |
              SQLITE_OPEN_FULLMUTEX,
          nullptr) != SQLITE_OK) {
    Close(&source_db);
    return false;
  }

  if (sqlite3_open_v2(
          destination_utf8.c_str(),
          &destination_db,
          SQLITE_OPEN_READWRITE |
              SQLITE_OPEN_CREATE |
              SQLITE_OPEN_FULLMUTEX,
          nullptr) != SQLITE_OK) {
    Close(&source_db);
    Close(&destination_db);
    return false;
  }

  sqlite3_backup* backup =
      sqlite3_backup_init(
          destination_db, "main",
          source_db, "main");
  if (!backup) {
    Close(&source_db);
    Close(&destination_db);
    return false;
  }

  int rc = SQLITE_OK;
  for (int attempt = 0;
       attempt < 100;
       ++attempt) {
    rc = sqlite3_backup_step(backup, -1);
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_BUSY &&
        rc != SQLITE_LOCKED) {
      break;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(5));
  }

  const int finish =
      sqlite3_backup_finish(backup);
  const int destination_error =
      sqlite3_errcode(destination_db);

  Close(&source_db);
  Close(&destination_db);

  return rc == SQLITE_DONE &&
      finish == SQLITE_OK &&
      destination_error == SQLITE_OK;
}

}  // namespace cx::config
