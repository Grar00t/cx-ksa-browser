#include "config/backup_restore.h"
#include "config/config_manager.h"
#include "storage/database.h"

#include <gtest/gtest.h>

#include <sqlite3.h>
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

class ConfigTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-config-test-" +
         std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(sequence.fetch_add(1)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
    config_path_ = root_ / "config.json";
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::filesystem::path root_;
  std::filesystem::path config_path_;
};

TEST(ConfigDefaultPath, UsesAppDataCxBuildConfigJson) {
  wchar_t* appdata = nullptr;
  std::size_t length = 0;
  ASSERT_EQ(
      _wdupenv_s(&appdata, &length, L"APPDATA"),
      0);
  ASSERT_NE(appdata, nullptr);

  const std::filesystem::path expected =
      std::filesystem::path(appdata) /
      L"CX Build" / L"config.json";
  std::free(appdata);

  EXPECT_EQ(
      cx::config::ConfigManager::DefaultPath(),
      expected);
}

TEST_F(ConfigTest, DefaultsArePrivacyFirstAndCreated) {
  cx::config::ConfigManager manager(config_path_);
  EXPECT_EQ(
      manager.Load(),
      cx::config::LoadStatus::CreatedDefaults);
  EXPECT_TRUE(std::filesystem::exists(config_path_));

  const auto& settings = manager.settings();
  EXPECT_EQ(
      settings.general.startup,
      cx::config::StartupBehavior::Blank);
  EXPECT_EQ(
      settings.general.default_search,
      cx::config::SearchProvider::Disabled);
  EXPECT_EQ(
      settings.privacy.cookies,
      cx::config::CookiePolicy::BlockAll);
  EXPECT_EQ(
      settings.privacy.cache,
      cx::config::CachePolicy::MemoryOnly);
  EXPECT_FALSE(settings.advanced.developer_tools);
  EXPECT_FALSE(settings.advanced.experimental);
}

TEST_F(ConfigTest, EverySettingRoundTrips) {
  cx::config::ConfigManager manager(config_path_);
  ASSERT_NE(
      manager.Load(),
      cx::config::LoadStatus::Failed);

  auto changed = manager.settings();
  changed.general.startup =
      cx::config::StartupBehavior::RestoreSession;
  changed.general.default_search =
      cx::config::SearchProvider::DuckDuckGo;
  changed.appearance.theme =
      cx::config::Theme::Dark;
  changed.appearance.font_size_percent = 135;
  changed.privacy.cookies =
      cx::config::CookiePolicy::SessionOnly;
  changed.privacy.cache =
      cx::config::CachePolicy::SessionDisk;
  changed.advanced.developer_tools = true;
  changed.advanced.experimental = true;

  ASSERT_TRUE(manager.Set(changed));

  cx::config::ConfigManager reloaded(config_path_);
  ASSERT_EQ(
      reloaded.Load(),
      cx::config::LoadStatus::Loaded);
  EXPECT_EQ(reloaded.settings(), changed);
}

TEST_F(ConfigTest, CorruptConfigFallsBackGracefully) {
  {
    std::ofstream output(config_path_);
    output << "{this is not valid json";
  }

  cx::config::ConfigManager manager(config_path_);
  EXPECT_EQ(
      manager.Load(),
      cx::config::LoadStatus::RecoveredFromCorrupt);
  EXPECT_EQ(
      manager.settings(),
      cx::config::DefaultSettings());
  EXPECT_TRUE(std::filesystem::exists(
      std::filesystem::path(
          config_path_.wstring() + L".corrupt")));

  cx::config::ConfigManager after(config_path_);
  EXPECT_EQ(
      after.Load(),
      cx::config::LoadStatus::Loaded);
  EXPECT_EQ(
      after.settings(),
      cx::config::DefaultSettings());
}

TEST_F(ConfigTest, ValidationRejectsOutOfRangeFont) {
  auto settings = cx::config::DefaultSettings();
  settings.appearance.font_size_percent = 74;
  EXPECT_FALSE(
      cx::config::ConfigManager::Validate(settings));

  settings.appearance.font_size_percent = 200;
  EXPECT_TRUE(
      cx::config::ConfigManager::Validate(settings));

  settings.appearance.font_size_percent = 201;
  EXPECT_FALSE(
      cx::config::ConfigManager::Validate(settings));
}

TEST_F(ConfigTest, ImportExportStayLocalAndValidated) {
  cx::config::ConfigManager manager(config_path_);
  ASSERT_NE(
      manager.Load(),
      cx::config::LoadStatus::Failed);

  auto changed = manager.settings();
  changed.appearance.theme =
      cx::config::Theme::Light;
  changed.general.default_search =
      cx::config::SearchProvider::Bing;
  ASSERT_TRUE(manager.Set(changed));

  const auto exported = root_ / "export.json";
  ASSERT_TRUE(manager.ExportTo(exported));

  cx::config::ConfigManager imported(
      root_ / "imported-config.json");
  ASSERT_NE(
      imported.Load(),
      cx::config::LoadStatus::Failed);
  ASSERT_TRUE(imported.ImportFrom(exported));
  EXPECT_EQ(imported.settings(), changed);

  EXPECT_FALSE(
      cx::config::ConfigManager::IsLocalPath(
          std::filesystem::path(
              L"\\\\server\\share\\config.json")));
  EXPECT_FALSE(manager.ExportTo(
      std::filesystem::path(
          L"\\\\server\\share\\config.json")));
}

TEST_F(ConfigTest, InvalidImportDoesNotReplaceCurrentSettings) {
  cx::config::ConfigManager manager(config_path_);
  ASSERT_NE(
      manager.Load(),
      cx::config::LoadStatus::Failed);

  auto changed = manager.settings();
  changed.appearance.font_size_percent = 125;
  ASSERT_TRUE(manager.Set(changed));

  const auto invalid = root_ / "invalid.json";
  {
    std::ofstream output(invalid);
    output <<
        R"({"version":1,"general":{"startup":"invalid","default_search":"disabled"},"appearance":{"theme":"system","font_size_percent":100},"privacy":{"cookies":"block_all","cache":"memory_only"},"advanced":{"developer_tools":false,"experimental":false}})";
  }

  EXPECT_FALSE(manager.ImportFrom(invalid));
  EXPECT_EQ(manager.settings(), changed);
}

TEST_F(ConfigTest, DatabaseBackupRestorePreservesSnapshot) {
  cx::storage::Database database(root_ / "data.db");
  ASSERT_TRUE(database.Open());
  ASSERT_TRUE(database.SetSetting("sentinel", "before"));
  ASSERT_GT(
      database.AddHistory(
          "https://backup.test",
          "Before", 100),
      0);
  ASSERT_GT(
      database.AddBookmark(
          "https://bookmark.test",
          "Before"),
      0);

  const auto backup = root_ / "backup.db";
  ASSERT_TRUE(
      cx::config::BackupRestore::BackupDatabase(
          database, backup));
  ASSERT_TRUE(
      cx::config::BackupRestore::VerifyDatabase(
          backup));

  ASSERT_TRUE(database.SetSetting("sentinel", "after"));
  ASSERT_TRUE(database.SetSetting("mutation", "present"));
  ASSERT_TRUE(database.ClearHistory());

  ASSERT_TRUE(
      cx::config::BackupRestore::RestoreDatabase(
          database, backup));

  ASSERT_EQ(
      database.GetSetting("sentinel").value_or(""),
      "before");
  EXPECT_FALSE(
      database.GetSetting("mutation").has_value());
  ASSERT_EQ(database.ListHistory().size(), 1u);
  ASSERT_EQ(database.ListBookmarks().size(), 1u);
  EXPECT_EQ(
      database.ListBookmarks().front().title,
      "Before");
}

TEST_F(ConfigTest, CorruptBackupIsRejectedWithoutDataLoss) {
  cx::storage::Database database(root_ / "data.db");
  ASSERT_TRUE(database.Open());
  ASSERT_TRUE(database.SetSetting(
      "sentinel", "keep-me"));

  const auto corrupt = root_ / "corrupt.db";
  {
    std::ofstream output(corrupt, std::ios::binary);
    output << "not sqlite";
  }

  EXPECT_FALSE(
      cx::config::BackupRestore::RestoreDatabase(
          database, corrupt));
  EXPECT_TRUE(database.IsOpen());
  EXPECT_EQ(
      database.GetSetting("sentinel").value_or(""),
      "keep-me");
}

TEST_F(ConfigTest, ValidNonCxSqliteIsRejectedWithoutDataLoss) {
  cx::storage::Database database(root_ / "data.db");
  ASSERT_TRUE(database.Open());
  ASSERT_TRUE(database.SetSetting(
      "sentinel", "preserve"));

  const auto unrelated = root_ / "unrelated.db";
  sqlite3* raw = nullptr;
  ASSERT_EQ(
      sqlite3_open(unrelated.string().c_str(), &raw),
      SQLITE_OK);
  ASSERT_NE(raw, nullptr);
  ASSERT_EQ(
      sqlite3_exec(
          raw,
          "CREATE TABLE unrelated(id INTEGER PRIMARY KEY);",
          nullptr, nullptr, nullptr),
      SQLITE_OK);
  ASSERT_EQ(sqlite3_close(raw), SQLITE_OK);

  EXPECT_FALSE(
      cx::config::BackupRestore::VerifyDatabase(
          unrelated));
  EXPECT_FALSE(
      cx::config::BackupRestore::RestoreDatabase(
          database, unrelated));
  EXPECT_TRUE(database.IsOpen());
  EXPECT_EQ(
      database.GetSetting("sentinel").value_or(""),
      "preserve");
}

TEST_F(ConfigTest, RemoteBackupPathsAreRejectedByDesign) {
  cx::storage::Database database(root_ / "data.db");
  ASSERT_TRUE(database.Open());
  ASSERT_TRUE(database.SetSetting(
      "sentinel", "local-only"));

  const std::filesystem::path remote(
      L"\\\\server\\share\\cx-backup.db");
  EXPECT_FALSE(
      cx::config::BackupRestore::BackupDatabase(
          database, remote));
  EXPECT_FALSE(
      cx::config::BackupRestore::RestoreDatabase(
          database, remote));
  EXPECT_EQ(
      database.GetSetting("sentinel").value_or(""),
      "local-only");
}

}  // namespace
