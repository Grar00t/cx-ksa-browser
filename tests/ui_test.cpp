#include "agent/agent_core.h"
#include "agent/permissions.h"
#include "browser/history_service.h"
#include "browser/tab_manager.h"
#include "mcp/allowlist_manager.h"
#include "mcp/mcp_client.h"
#include "mcp/rate_limiter.h"
#include "storage/database.h"
#include "ui/privacy_dashboard.h"
#include "ui/settings_window.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

class UiFakePrompt final : public cx::agent::ConsentPrompt {
public:
  bool Request(
      HWND,
      const cx::agent::CapabilityDescriptor&) override {
    return false;
  }
};

class PrivacyUiTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-p07-test-" +
         std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(sequence.fetch_add(1)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);

    database_ = std::make_unique<cx::storage::Database>(
        root_ / "data.db");
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

TEST(P07SettingsSchema, PrivacySettingsAreVisibleAndPrivacyFirst) {
  const auto& settings =
      cx::ui::SettingsWindow::PrivacySettings();
  ASSERT_EQ(settings.size(), 3u);

  EXPECT_EQ(settings[0].key, "privacy.save_history");
  EXPECT_FALSE(settings[0].default_value);
  EXPECT_NE(std::wstring(settings[0].label).size(), 0u);

  EXPECT_EQ(settings[1].key, "privacy.restore_session");
  EXPECT_FALSE(settings[1].default_value);
  EXPECT_NE(std::wstring(settings[1].label).size(), 0u);

  EXPECT_EQ(
      settings[2].key,
      "privacy.clear_history_on_exit");
  EXPECT_TRUE(settings[2].default_value);
  EXPECT_NE(std::wstring(settings[2].label).size(), 0u);
}

TEST_F(PrivacyUiTest, MissingHistorySettingDoesNotPersistVisits) {
  cx::browser::HistoryService history(*database_);
  ASSERT_TRUE(history.RecordVisit(
      "https://privacy.test", "Private"));
  EXPECT_TRUE(history.Recent().empty());

  ASSERT_TRUE(database_->SetSetting(
      "privacy.save_history", "1"));
  ASSERT_TRUE(history.RecordVisit(
      "https://privacy.test", "Saved"));
  ASSERT_EQ(history.Recent().size(), 1u);
}

TEST_F(PrivacyUiTest, MissingRestoreSettingDiscardsPriorSession) {
  cx::browser::TabManager first(*database_);
  ASSERT_TRUE(first.Restore());
  ASSERT_TRUE(first.CreateTab(
      "https://privacy.test/old", "Old").has_value());
  ASSERT_EQ(first.tabs().size(), 2u);

  database_->Close();
  ASSERT_TRUE(database_->Open());

  cx::browser::TabManager second(*database_);
  ASSERT_TRUE(second.Restore());
  ASSERT_EQ(second.tabs().size(), 1u);
  EXPECT_EQ(second.active_tab()->url, "about:blank");
}

TEST_F(PrivacyUiTest, DashboardReflectsPermissionsAndMcpAllowlist) {
  UiFakePrompt prompt;
  cx::agent::PermissionManager permissions(
      *database_, prompt);
  ASSERT_TRUE(permissions.SetGranted(
      cx::agent::Capability::ReadPage, true));

  cx::agent::LocalLogger logger(
      root_ / "logs" / "dashboard.log");
  ASSERT_TRUE(logger.Open());
  cx::agent::AgentCore agent(permissions, logger);

  cx::mcp::AllowlistManager allowlist(
      root_ / "config" / "mcp_allowlist.json");
  ASSERT_TRUE(allowlist.Load());

  wchar_t module[32768]{};
  const DWORD length = GetModuleFileNameW(
      nullptr, module,
      static_cast<DWORD>(std::size(module)));
  ASSERT_GT(length, 0u);

  cx::mcp::ServerConfig server;
  server.id = "local-test";
  const std::wstring module_path(module, length);
  const int utf8_size = WideCharToMultiByte(
      CP_UTF8, 0, module_path.data(),
      static_cast<int>(module_path.size()),
      nullptr, 0, nullptr, nullptr);
  ASSERT_GT(utf8_size, 0);
  server.command.resize(
      static_cast<std::size_t>(utf8_size));
  ASSERT_EQ(
      WideCharToMultiByte(
          CP_UTF8, 0, module_path.data(),
          static_cast<int>(module_path.size()),
          server.command.data(), utf8_size,
          nullptr, nullptr),
      utf8_size);
  ASSERT_TRUE(allowlist.AddOrUpdate(server));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, allowlist, limiter, logger);

  cx::ui::PrivacyDashboard dashboard(
      permissions, allowlist, client, root_);
  const std::wstring summary = dashboard.Summary();
  EXPECT_NE(
      summary.find(L"browser.read_page"),
      std::wstring::npos);
  EXPECT_NE(
      summary.find(L"MCP active: none"),
      std::wstring::npos);
  EXPECT_NE(
      summary.find(L"MCP allowlist entries: 1"),
      std::wstring::npos);
}

TEST_F(PrivacyUiTest, LocalSizeRefreshDoesNotBlockUiThread) {
  for (int i = 0; i < 200; ++i) {
    std::ofstream output(
        root_ / ("file-" + std::to_string(i) + ".dat"),
        std::ios::binary);
    output << std::string(4096, 'x');
  }

  UiFakePrompt prompt;
  cx::agent::PermissionManager permissions(
      *database_, prompt);
  cx::agent::LocalLogger logger(
      root_ / "logs" / "size.log");
  ASSERT_TRUE(logger.Open());
  cx::agent::AgentCore agent(permissions, logger);
  cx::mcp::AllowlistManager allowlist(
      root_ / "config" / "mcp_allowlist.json");
  ASSERT_TRUE(allowlist.Load());
  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, allowlist, limiter, logger);

  cx::ui::PrivacyDashboard dashboard(
      permissions, allowlist, client, root_);

  const auto start = std::chrono::steady_clock::now();
  dashboard.RefreshLocalSizeAsync(
      nullptr, WM_APP + 90);
  const auto elapsed =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - start)
          .count();

  EXPECT_LT(elapsed, 100.0);
}

}  // namespace
