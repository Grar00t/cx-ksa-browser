#include "agent/permissions.h"
#include "browser/bookmark_service.h"
#include "browser/history_service.h"
#include "browser/tab_manager.h"
#include "config/config_manager.h"
#include "storage/database.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace {

class AlwaysAllowPrompt final : public cx::agent::ConsentPrompt {
public:
  bool Request(
      HWND,
      const cx::agent::CapabilityDescriptor&) override {
    ++requests;
    return true;
  }

  int requests = 0;
};

class IntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-integration-test-" +
         std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(sequence.fetch_add(1)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::filesystem::path root_;
};

TEST_F(IntegrationTest, LocalStatePersistsAcrossSubsystems) {
  const auto db_path = root_ / "data.db";
  const auto config_path = root_ / "config.json";

  cx::storage::Database database(db_path);
  ASSERT_TRUE(database.Open());
  ASSERT_EQ(database.SchemaVersion(), 4);

  cx::config::ConfigManager config(config_path);
  ASSERT_NE(config.Load(), cx::config::LoadStatus::Failed);

  auto settings = config.settings();
  settings.general.startup =
      cx::config::StartupBehavior::RestoreSession;
  settings.appearance.theme = cx::config::Theme::Dark;
  settings.appearance.font_size_percent = 125;
  ASSERT_TRUE(config.Set(settings));

  ASSERT_TRUE(database.SetSetting(
      "privacy.restore_session", "1"));
  ASSERT_TRUE(database.SetSetting(
      "privacy.save_history", "1"));

  cx::browser::TabManager tabs(database);
  ASSERT_TRUE(tabs.Restore());
  ASSERT_EQ(tabs.tabs().size(), 1u);

  const auto second = tabs.CreateTab(
      "https://example.test/integration",
      "Integration");
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(tabs.ActivateTab(*second));

  cx::browser::HistoryService history(database);
  ASSERT_TRUE(history.RecordVisit(
      "https://example.test/integration",
      "Integration"));

  cx::browser::BookmarkService bookmarks(database);
  const auto bookmark_id = bookmarks.Add(
      "https://example.test/integration",
      "Integration");
  ASSERT_GT(bookmark_id, 0);

  AlwaysAllowPrompt prompt;
  cx::agent::PermissionManager permissions(
      database, prompt);
  ASSERT_TRUE(permissions.Ensure(
      nullptr, cx::agent::Capability::AgentRun));
  ASSERT_EQ(prompt.requests, 1);

  database.Close();

  cx::storage::Database reopened(db_path);
  ASSERT_TRUE(reopened.Open());
  EXPECT_EQ(reopened.SchemaVersion(), 4);

  cx::browser::TabManager restored_tabs(reopened);
  ASSERT_TRUE(restored_tabs.Restore());
  EXPECT_EQ(restored_tabs.tabs().size(), 2u);
  EXPECT_EQ(restored_tabs.active_tab_id(), *second);

  cx::browser::HistoryService restored_history(
      reopened);
  ASSERT_EQ(
      restored_history.Search("integration").size(),
      1u);

  cx::browser::BookmarkService restored_bookmarks(
      reopened);
  ASSERT_EQ(restored_bookmarks.List().size(), 1u);

  AlwaysAllowPrompt second_prompt;
  cx::agent::PermissionManager restored_permissions(
      reopened, second_prompt);
  EXPECT_TRUE(restored_permissions.IsGranted(
      cx::agent::Capability::AgentRun));
  EXPECT_EQ(second_prompt.requests, 0);

  cx::config::ConfigManager restored_config(
      config_path);
  ASSERT_EQ(
      restored_config.Load(),
      cx::config::LoadStatus::Loaded);
  EXPECT_EQ(
      restored_config.settings().general.startup,
      cx::config::StartupBehavior::RestoreSession);
  EXPECT_EQ(
      restored_config.settings().appearance.theme,
      cx::config::Theme::Dark);
  EXPECT_EQ(
      restored_config.settings().appearance.font_size_percent,
      125);
}

}  // namespace
