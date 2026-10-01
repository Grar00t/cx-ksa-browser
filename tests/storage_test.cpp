#include "storage/database.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace {

class StorageTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-storage-test-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(sequence.fetch_add(1)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
    database_ = std::make_unique<cx::storage::Database>(root_ / "data.db");
    ASSERT_TRUE(database_->Open());
  }

  void TearDown() override {
    database_.reset();
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::filesystem::path root_;
  std::unique_ptr<cx::storage::Database> database_;
};

TEST(StorageDefaultPath, CreatesDatabaseUnderAppData) {
  wchar_t* appdata = nullptr;
  std::size_t length = 0;
  ASSERT_EQ(_wdupenv_s(&appdata, &length, L"APPDATA"), 0);
  ASSERT_NE(appdata, nullptr);

  const std::filesystem::path expected =
      std::filesystem::path(appdata) / L"CX Build" / L"data.db";
  std::free(appdata);

  cx::storage::Database database;
  EXPECT_EQ(database.path(), expected);
  ASSERT_TRUE(database.Open());
  EXPECT_TRUE(std::filesystem::exists(expected));
  EXPECT_EQ(database.SchemaVersion(), 2);
}

TEST_F(StorageTest, SettingsCrudWorks) {
  EXPECT_FALSE(database_->GetSetting("theme").has_value());

  ASSERT_TRUE(database_->SetSetting("theme", "dark"));
  ASSERT_TRUE(database_->GetSetting("theme").has_value());
  EXPECT_EQ(*database_->GetSetting("theme"), "dark");

  ASSERT_TRUE(database_->SetSetting("theme", "light"));
  EXPECT_EQ(*database_->GetSetting("theme"), "light");

  EXPECT_TRUE(database_->DeleteSetting("theme"));
  EXPECT_FALSE(database_->GetSetting("theme").has_value());
}

TEST_F(StorageTest, TabsCrudWorks) {
  const auto first =
      database_->AddTab(1, "about:blank", "Blank", false);
  const auto second =
      database_->AddTab(0, "cx://local", "Local", true);
  ASSERT_GT(first, 0);
  ASSERT_GT(second, 0);

  auto tab = database_->GetTab(first);
  ASSERT_TRUE(tab.has_value());
  EXPECT_EQ(tab->url, "about:blank");
  EXPECT_FALSE(tab->pinned);

  ASSERT_TRUE(database_->UpdateTab(
      first, 2, "cx://updated", "Updated", true));
  tab = database_->GetTab(first);
  ASSERT_TRUE(tab.has_value());
  EXPECT_EQ(tab->position, 2);
  EXPECT_EQ(tab->url, "cx://updated");
  EXPECT_TRUE(tab->pinned);

  const auto tabs = database_->ListTabs();
  ASSERT_EQ(tabs.size(), 2u);
  EXPECT_EQ(tabs.front().id, second);
  EXPECT_EQ(tabs.back().id, first);

  EXPECT_TRUE(database_->DeleteTab(second));
  EXPECT_FALSE(database_->GetTab(second).has_value());
}

TEST_F(StorageTest, HistoryCrudWorks) {
  const auto older =
      database_->AddHistory("https://local.invalid/a", "A", 100);
  const auto newer =
      database_->AddHistory("https://local.invalid/b", "B", 200);
  ASSERT_GT(older, 0);
  ASSERT_GT(newer, 0);

  auto history = database_->ListHistory();
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].id, newer);
  EXPECT_EQ(history[1].id, older);

  EXPECT_TRUE(database_->DeleteHistory(older));
  history = database_->ListHistory();
  ASSERT_EQ(history.size(), 1u);
  EXPECT_EQ(history[0].id, newer);

  EXPECT_TRUE(database_->ClearHistory());
  EXPECT_TRUE(database_->ListHistory().empty());
}

TEST_F(StorageTest, MigrationsAreIdempotent) {
  EXPECT_EQ(database_->SchemaVersion(), 2);
  database_->Close();
  ASSERT_TRUE(database_->Open());
  EXPECT_EQ(database_->SchemaVersion(), 2);

  ASSERT_TRUE(database_->SetSetting("after-reopen", "ok"));
  EXPECT_EQ(*database_->GetSetting("after-reopen"), "ok");
}

TEST_F(StorageTest, ExplicitRollbackIsAtomic) {
  ASSERT_TRUE(database_->BeginTransaction());
  ASSERT_TRUE(database_->SetSetting("atomic", "discard"));
  ASSERT_TRUE(database_->Rollback());
  EXPECT_FALSE(database_->GetSetting("atomic").has_value());
  EXPECT_FALSE(database_->InTransaction());
}

TEST_F(StorageTest, CommitPersistsTransaction) {
  cx::storage::Transaction transaction(*database_);
  ASSERT_TRUE(transaction.ok());
  ASSERT_TRUE(database_->SetSetting("atomic", "keep"));
  ASSERT_TRUE(transaction.Commit());

  ASSERT_TRUE(database_->GetSetting("atomic").has_value());
  EXPECT_EQ(*database_->GetSetting("atomic"), "keep");
  EXPECT_FALSE(database_->InTransaction());
}

TEST_F(StorageTest, TransactionScopeRollsBackByDefault) {
  {
    cx::storage::Transaction transaction(*database_);
    ASSERT_TRUE(transaction.ok());
    ASSERT_TRUE(database_->SetSetting("scope", "rollback"));
    ASSERT_TRUE(database_->InTransaction());
  }

  EXPECT_FALSE(database_->InTransaction());
  EXPECT_FALSE(database_->GetSetting("scope").has_value());
}

}  // namespace
