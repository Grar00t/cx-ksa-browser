#include "agent/agent_core.h"
#include "agent/permissions.h"
#include "browser/bookmark_service.h"
#include "browser/history_service.h"
#include "browser/tab_manager.h"
#include "mcp/allowlist_dialog.h"
#include "mcp/allowlist_manager.h"
#include "mcp/mcp_client.h"
#include "mcp/rate_limiter.h"
#include "localization/strings.h"
#include "storage/database.h"
#include "ui/agent_workspace.h"
#include "ui/athar_sound.h"
#include "ui/context_graph.h"
#include "ui/design_tokens.h"
#include "ui/najdi_theme.h"
#include "ui/privacy_dashboard.h"
#include "ui/settings_window.h"

#include <gtest/gtest.h>

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {


double RelativeLuminance(COLORREF color) {
  const auto linear = [](BYTE value) {
    const double channel =
        static_cast<double>(value) / 255.0;
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear(GetRValue(color)) +
      0.7152 * linear(GetGValue(color)) +
      0.0722 * linear(GetBValue(color));
}

double ContrastRatio(COLORREF first, COLORREF second) {
  const double first_luminance = RelativeLuminance(first);
  const double second_luminance = RelativeLuminance(second);
  const double lighter =
      (std::max)(first_luminance, second_luminance);
  const double darker =
      (std::min)(first_luminance, second_luminance);
  return (lighter + 0.05) / (darker + 0.05);
}

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


TEST(DesignSystemTest, TokensRespectNajdiConstraints) {
  using namespace cx::ui::design;

  EXPECT_LE(Radius::Control, 4);
  EXPECT_LE(Radius::Surface, 4);
  EXPECT_EQ(Border::Standard, 1);
  EXPECT_LE(Density::ControlHeight, 32);
  EXPECT_STREQ(Typography::Family, L"Segoe UI");
  EXPECT_STREQ(
      Typography::ArabicFallbackFamily,
      L"Tahoma");
  EXPECT_NE(Color::Background, Color::Text);
  EXPECT_NE(Focus::Primary, Focus::Secondary);
  EXPECT_EQ(
      SettingsLayout::PageContentStart,
      SettingsLayout::PageLabelStart + Spacing::Md);
}



TEST(AgentWorkspaceTest, RunningBlocksAllPointerTargetsExceptEmergencyStop) {
  cx::ui::AgentWorkspacePolicy policy;
  EXPECT_FALSE(policy.shield_visible());
  EXPECT_FALSE(policy.emergency_stop_visible());

  policy.SetAgentRunning(true);

  EXPECT_TRUE(policy.agent_running());
  EXPECT_TRUE(policy.shield_visible());
  EXPECT_TRUE(policy.emergency_stop_visible());
  EXPECT_TRUE(policy.BlocksPointer(
      cx::ui::WorkspacePointerTarget::BrowserChrome));
  EXPECT_TRUE(policy.BlocksPointer(
      cx::ui::WorkspacePointerTarget::BrowserContent));
  EXPECT_TRUE(policy.BlocksPointer(
      cx::ui::WorkspacePointerTarget::AgentPanel));
  EXPECT_FALSE(policy.BlocksPointer(
      cx::ui::WorkspacePointerTarget::EmergencyStop));

  policy.SetAgentRunning(false);
  EXPECT_FALSE(policy.BlocksPointer(
      cx::ui::WorkspacePointerTarget::BrowserContent));
}

TEST(ContextGraphTest, RemovesPathsQueriesFragmentsAndCredentials) {
  EXPECT_EQ(
      cx::ui::SafeOriginLabel(
          "https://user:secret@Example.COM:8443/private?q=token#part"),
      "https://example.com:8443");
  EXPECT_EQ(
      cx::ui::SafeOriginLabel("javascript:alert(1)"),
      "Local or blocked");
  EXPECT_EQ(
      cx::ui::SafeOriginLabel("about:blank"),
      "Local new tab");
}

TEST(ContextGraphTest, GroupsTabsByOriginAndMarksActiveTab) {
  const std::vector<cx::ui::ContextTabInput> tabs{
      {11, "https://example.test/a?secret=1", "Alpha", false},
      {12, "https://example.test/b", "Beta", true},
      {13, "https://other.test/", "Other", false},
  };

  const auto graph = cx::ui::BuildContextGraph(tabs);
  EXPECT_EQ(graph.edges.size(), 3u);
  EXPECT_EQ(graph.nodes.size(), 5u);

  const auto active = std::find_if(
      graph.nodes.begin(), graph.nodes.end(),
      [](const auto& node) { return node.active; });
  ASSERT_NE(active, graph.nodes.end());
  EXPECT_EQ(active->tab_id, 12);
  EXPECT_EQ(active->label, "Beta");

  for (const auto& node : graph.nodes) {
    EXPECT_EQ(node.label.find("secret"), std::string::npos);
    EXPECT_GE(node.x, 0.0F);
    EXPECT_LE(node.x, 1.0F);
    EXPECT_GE(node.y, 0.0F);
    EXPECT_LE(node.y, 1.0F);
  }
}

TEST(AtharSoundTest, GeneratesEightSecondStereoPcmWithSignal) {
  const auto wave = cx::ui::AtharSound::BuildWave();
  constexpr std::size_t expected_frames = 48000u * 8u;
  ASSERT_EQ(wave.size(), 44u + expected_frames * 4u);
  ASSERT_GE(wave.size(), 48u);

  EXPECT_EQ(
      std::string(wave.begin(), wave.begin() + 4),
      "RIFF");
  EXPECT_EQ(
      std::string(wave.begin() + 8, wave.begin() + 12),
      "WAVE");
  EXPECT_EQ(wave[22], 2u);
  EXPECT_EQ(wave[23], 0u);
  EXPECT_EQ(wave[24], 0x80u);
  EXPECT_EQ(wave[25], 0xBBu);
  EXPECT_EQ(wave[34], 16u);
  EXPECT_EQ(wave[35], 0u);

  std::uint16_t peak = 0;
  for (std::size_t i = 44; i + 1 < wave.size(); i += 2) {
    const std::uint16_t encoded =
        static_cast<std::uint16_t>(wave[i]) |
        (static_cast<std::uint16_t>(wave[i + 1]) << 8);
    const std::int16_t sample =
        static_cast<std::int16_t>(encoded);
    const std::uint16_t magnitude =
        sample == (std::numeric_limits<std::int16_t>::min)()
            ? 32768u
            : static_cast<std::uint16_t>(
                  sample < 0 ? -sample : sample);
    peak = (std::max)(peak, magnitude);
  }
  EXPECT_GT(peak, 2048u);
}

TEST(ContextGraphTest, CapsTabCountToKeepTheViewQuiet) {
  std::vector<cx::ui::ContextTabInput> tabs;
  for (std::int64_t id = 1; id <= 30; ++id) {
    tabs.push_back({
        id,
        "https://example.test/" + std::to_string(id),
        "Tab " + std::to_string(id),
        id == 30});
  }

  const auto graph = cx::ui::BuildContextGraph(tabs, 12);
  EXPECT_EQ(graph.edges.size(), 12u);
  EXPECT_EQ(graph.nodes.size(), 13u);
  const auto active = std::find_if(
      graph.nodes.begin(), graph.nodes.end(),
      [](const auto& node) { return node.active; });
  ASSERT_NE(active, graph.nodes.end());
  EXPECT_EQ(active->tab_id, 30);
}

TEST(DesignSystemTest, AgentWorkspaceStaysCompactAndLeavesBrowserUsable) {
  using namespace cx::ui::design;
  EXPECT_LE(AgentWorkspaceLayout::PanelWidth, 320);
  EXPECT_GE(AgentWorkspaceLayout::MinimumBrowserWidth, 480);
  EXPECT_EQ(AgentWorkspaceLayout::ActivityRail, 2);
  EXPECT_GT(AgentWorkspaceLayout::ShieldAlpha, 0);
  EXPECT_LT(AgentWorkspaceLayout::ShieldAlpha, 96);
}

TEST(DesignSystemTest, SpaceTealTokensAreExactAndStateColorsDistinct) {
  using namespace cx::ui::design;

  EXPECT_EQ(Color::Background, RGB(11, 16, 32));
  EXPECT_EQ(Color::Surface, RGB(18, 26, 46));
  EXPECT_EQ(Color::SurfaceRaised, RGB(26, 37, 64));
  EXPECT_EQ(Color::Input, RGB(10, 15, 28));
  EXPECT_EQ(Color::Border, RGB(38, 52, 79));
  EXPECT_EQ(Color::Text, RGB(232, 241, 255));
  EXPECT_EQ(Color::MutedText, RGB(159, 176, 204));
  EXPECT_EQ(Color::AccentTurquoise, RGB(31, 209, 198));
  EXPECT_EQ(Color::AccentTurquoiseDim, RGB(19, 143, 137));
  EXPECT_EQ(Color::AgentActive, RGB(125, 227, 255));
  EXPECT_EQ(Color::Danger, RGB(255, 107, 122));
  EXPECT_EQ(Border::Focus, 1);
  EXPECT_NE(Color::AgentActive, Color::Danger);
  EXPECT_NE(Color::AccentTurquoise, Color::Danger);
}

TEST(DesignSystemTest, BodyTextMeetsWcagAaAcrossSurfaces) {
  using namespace cx::ui::design;
  constexpr std::array<COLORREF, 2> text_colors{{
      Color::Text, Color::MutedText}};
  constexpr std::array<COLORREF, 4> surfaces{{
      Color::Background,
      Color::Surface,
      Color::SurfaceRaised,
      Color::Input}};

  for (const COLORREF text : text_colors) {
    for (const COLORREF surface : surfaces) {
      EXPECT_GE(ContrastRatio(text, surface), 4.5);
    }
  }
}

TEST(LocalizationTest, CoreStringsHaveEnglishAndArabicWithEnglishFallback) {
  using cx::localization::Locale;
  using cx::localization::StringId;

  constexpr std::array<StringId, 12> required{{
      StringId::NewTab,
      StringId::Reload,
      StringId::Go,
      StringId::Bookmark,
      StringId::Settings,
      StringId::Privacy,
      StringId::Agent,
      StringId::Mcp,
      StringId::Consent,
      StringId::Denied,
      StringId::Allowed,
      StringId::LocalOnly,
  }};

  for (const auto id : required) {
    EXPECT_FALSE(
        cx::localization::Lookup(id, Locale::English).empty());
    EXPECT_FALSE(
        cx::localization::Lookup(id, Locale::Arabic).empty());
  }

  EXPECT_EQ(
      cx::localization::Lookup(
          StringId::NewTab, Locale::English),
      L"New Tab");
  EXPECT_EQ(
      cx::localization::Lookup(
          StringId::NewTab, Locale::Arabic),
      L"\u0639\u0644\u0627\u0645\u0629 \u062a\u0628\u0648\u064a\u0628 \u062c\u062f\u064a\u062f\u0629");

  EXPECT_EQ(
      cx::localization::LocaleFromName(L"ar-SA"),
      Locale::Arabic);
  EXPECT_EQ(
      cx::localization::LocaleFromName(L"AR"),
      Locale::Arabic);
  EXPECT_EQ(
      cx::localization::LocaleFromName(L"fr-FR"),
      Locale::English);
  EXPECT_TRUE(cx::localization::IsRtl(Locale::Arabic));
  EXPECT_FALSE(cx::localization::IsRtl(Locale::English));
}

TEST(DesignSystemTest, RtlDirectionCanBeAppliedAndRemoved) {
  HWND window = CreateWindowExW(
      0, L"STATIC", L"",
      WS_OVERLAPPED,
      0, 0, 100, 100,
      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ASSERT_NE(window, nullptr);

  cx::ui::theme::ApplyLayoutDirection(window, true);
  const LONG_PTR rtl =
      GetWindowLongPtrW(window, GWL_EXSTYLE);
  EXPECT_NE(rtl & WS_EX_LAYOUTRTL, 0);
  EXPECT_NE(rtl & WS_EX_RTLREADING, 0);

  cx::ui::theme::ApplyLayoutDirection(window, false);
  const LONG_PTR ltr =
      GetWindowLongPtrW(window, GWL_EXSTYLE);
  EXPECT_EQ(ltr & WS_EX_LAYOUTRTL, 0);
  EXPECT_EQ(ltr & WS_EX_RTLREADING, 0);

  DestroyWindow(window);
}

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


TEST_F(PrivacyUiTest, SettingsWindowCreatesFourPagesAndPersistsToggle) {
  UiFakePrompt prompt;
  cx::agent::PermissionManager permissions(
      *database_, prompt);
  cx::agent::LocalLogger logger(
      root_ / "logs" / "settings.log");
  ASSERT_TRUE(logger.Open());
  cx::agent::AgentCore agent(permissions, logger);

  cx::mcp::AllowlistManager allowlist(
      root_ / "config" / "mcp_allowlist.json");
  ASSERT_TRUE(allowlist.Load());
  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, allowlist, limiter, logger);
  cx::mcp::AllowlistDialog allowlist_dialog(
      allowlist, client);
  cx::browser::HistoryService history(*database_);
  cx::browser::BookmarkService bookmarks(*database_);

  cx::ui::SettingsWindow window(
      *database_, permissions, agent,
      allowlist, allowlist_dialog, client,
      history, bookmarks,
      cx::localization::Locale::English);

  window.Show(nullptr);
  HWND hwnd = FindWindowW(
      L"CXBuildSettingsWindow",
      L"CX Settings & Privacy");
  ASSERT_NE(hwnd, nullptr);

  HWND tabs = FindWindowExW(
      hwnd, nullptr, WC_TABCONTROLW, nullptr);
  ASSERT_NE(tabs, nullptr);
  EXPECT_EQ(TabCtrl_GetItemCount(tabs), 4);

  HWND privacy = FindWindowExW(
      hwnd, nullptr, L"BUTTON",
      L"Save browsing history locally");
  ASSERT_NE(privacy, nullptr);
  SendMessageW(
      privacy, BM_SETCHECK, BST_CHECKED, 0);
  SendMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(6001, BN_CLICKED),
      reinterpret_cast<LPARAM>(privacy));
  EXPECT_EQ(
      database_->GetSetting(
          "privacy.save_history").value_or(""),
      "1");

  HWND athar = FindWindowExW(
      hwnd, nullptr, L"BUTTON",
      L"Play CX - ATHAR at startup (local audio only)");
  ASSERT_NE(athar, nullptr);
  EXPECT_EQ(
      SendMessageW(athar, BM_GETCHECK, 0, 0),
      BST_CHECKED);
  SendMessageW(
      athar, BM_SETCHECK, BST_UNCHECKED, 0);
  SendMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(6050, BN_CLICKED),
      reinterpret_cast<LPARAM>(athar));
  EXPECT_EQ(
      database_->GetSetting(
          cx::ui::kAtharStartupSetting).value_or(""),
      "0");

  HWND agent_run = FindWindowExW(
      hwnd, nullptr, L"BUTTON",
      L"Start the agent  [agent.run]");
  ASSERT_NE(agent_run, nullptr);
  SendMessageW(
      agent_run, BM_SETCHECK, BST_CHECKED, 0);
  SendMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(6100, BN_CLICKED),
      reinterpret_cast<LPARAM>(agent_run));
  EXPECT_TRUE(permissions.IsGranted(
      cx::agent::Capability::AgentRun));

  for (int index = 0; index < 4; ++index) {
    TabCtrl_SetCurSel(tabs, index);
    NMHDR header{};
    header.hwndFrom = tabs;
    header.code = TCN_SELCHANGE;
    SendMessageW(
        hwnd, WM_NOTIFY, 0,
        reinterpret_cast<LPARAM>(&header));
  }

  SendMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(6203, BN_CLICKED), 0);
  SendMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(6303, BN_CLICKED), 0);

  RECT before{};
  ASSERT_TRUE(GetWindowRect(hwnd, &before));
  SetWindowPos(
      hwnd, nullptr,
      before.left, before.top,
      760, 620,
      SWP_NOZORDER | SWP_NOACTIVATE);
  SendMessageW(hwnd, WM_SIZE, 0, 0);

  window.Refresh();

  SendMessageW(hwnd, WM_CLOSE, 0, 0);
  EXPECT_FALSE(IsWindowVisible(hwnd));

  DestroyWindow(hwnd);
}




TEST_F(PrivacyUiTest, ArabicSettingsWindowUsesRtlAndLocalizedTabs) {
  UiFakePrompt prompt;
  cx::agent::PermissionManager permissions(
      *database_, prompt);
  cx::agent::LocalLogger logger(
      root_ / "logs" / "settings-ar.log");
  ASSERT_TRUE(logger.Open());
  cx::agent::AgentCore agent(permissions, logger);

  cx::mcp::AllowlistManager allowlist(
      root_ / "config" / "mcp_allowlist_ar.json");
  ASSERT_TRUE(allowlist.Load());
  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, allowlist, limiter, logger);
  cx::mcp::AllowlistDialog allowlist_dialog(
      allowlist, client);
  cx::browser::HistoryService history(*database_);
  cx::browser::BookmarkService bookmarks(*database_);

  cx::ui::SettingsWindow window(
      *database_, permissions, agent,
      allowlist, allowlist_dialog, client,
      history, bookmarks,
      cx::localization::Locale::Arabic);

  window.Show(nullptr);
  HWND hwnd = FindWindowW(
      L"CXBuildSettingsWindow", nullptr);
  ASSERT_NE(hwnd, nullptr);

  const LONG_PTR style =
      GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  EXPECT_NE(style & WS_EX_LAYOUTRTL, 0);
  EXPECT_NE(style & WS_EX_RTLREADING, 0);

  wchar_t title[256]{};
  GetWindowTextW(
      hwnd, title,
      static_cast<int>(std::size(title)));
  std::wstring expected_title = L"CX ";
  expected_title += std::wstring(
      cx::localization::Lookup(
          cx::localization::StringId::SettingsAndPrivacy,
          cx::localization::Locale::Arabic));
  EXPECT_EQ(title, expected_title);

  HWND tabs = FindWindowExW(
      hwnd, nullptr, WC_TABCONTROLW, nullptr);
  ASSERT_NE(tabs, nullptr);
  EXPECT_NE(
      GetWindowLongPtrW(tabs, GWL_EXSTYLE) &
          WS_EX_LAYOUTRTL,
      0);

  wchar_t first_tab[256]{};
  TCITEMW item{};
  item.mask = TCIF_TEXT;
  item.pszText = first_tab;
  item.cchTextMax =
      static_cast<int>(std::size(first_tab));
  ASSERT_NE(
      SendMessageW(
          tabs, TCM_GETITEMW, 0,
          reinterpret_cast<LPARAM>(&item)),
      0);
  EXPECT_EQ(
      std::wstring_view(first_tab),
      cx::localization::Lookup(
          cx::localization::StringId::PrivacyAndSecurity,
          cx::localization::Locale::Arabic));

  DestroyWindow(hwnd);
}

TEST_F(PrivacyUiTest, DashboardAcceptsSizeAndFormatsUnits) {
  UiFakePrompt prompt;
  cx::agent::PermissionManager permissions(
      *database_, prompt);
  ASSERT_TRUE(permissions.SetGranted(
      cx::agent::Capability::ReadPage, true));
  ASSERT_TRUE(permissions.SetGranted(
      cx::agent::Capability::ManageTabs, true));

  cx::agent::LocalLogger logger(
      root_ / "logs" / "dashboard-units.log");
  ASSERT_TRUE(logger.Open());
  cx::agent::AgentCore agent(permissions, logger);
  cx::mcp::AllowlistManager allowlist(
      root_ / "config" / "dashboard-allowlist.json");
  ASSERT_TRUE(allowlist.Load());
  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, allowlist, limiter, logger);

  cx::ui::PrivacyDashboard dashboard(
      permissions, allowlist, client, root_);

  EXPECT_FALSE(dashboard.AcceptLocalSizeResult(0));
  EXPECT_FALSE(dashboard.local_size().has_value());
  EXPECT_EQ(dashboard.local_root(), root_);

  for (const auto bytes : {
           std::uintmax_t{512},
           std::uintmax_t{2048},
           std::uintmax_t{3} * 1024 * 1024,
           std::uintmax_t{4} * 1024 * 1024 * 1024}) {
    auto* value = new std::uintmax_t(bytes);
    ASSERT_TRUE(dashboard.AcceptLocalSizeResult(
        reinterpret_cast<LPARAM>(value)));
    ASSERT_TRUE(dashboard.local_size().has_value());
    EXPECT_EQ(*dashboard.local_size(), bytes);
    const auto summary = dashboard.Summary();
    EXPECT_NE(
        summary.find(L"Enabled permissions: 2"),
        std::wstring::npos);
    EXPECT_EQ(
        summary.find(L"calculating..."),
        std::wstring::npos);
  }

  EXPECT_NE(
      dashboard.Summary().find(L"4.00 GiB"),
      std::wstring::npos);
}

TEST_F(PrivacyUiTest, AllowlistDialogExercisesSelectionConnectAndRemove) {
  UiFakePrompt prompt;
  cx::agent::PermissionManager permissions(
      *database_, prompt);
  cx::agent::LocalLogger logger(
      root_ / "logs" / "allowlist-actions.log");
  ASSERT_TRUE(logger.Open());
  cx::agent::AgentCore agent(permissions, logger);

  cx::mcp::AllowlistManager allowlist(
      root_ / "config" / "actions-allowlist.json");
  ASSERT_TRUE(allowlist.Load());

  wchar_t module[32768]{};
  const DWORD length = GetModuleFileNameW(
      nullptr, module,
      static_cast<DWORD>(std::size(module)));
  ASSERT_GT(length, 0u);
  const std::wstring module_path(module, length);

  const int utf8_size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS,
      module_path.data(),
      static_cast<int>(module_path.size()),
      nullptr, 0, nullptr, nullptr);
  ASSERT_GT(utf8_size, 0);

  cx::mcp::ServerConfig server;
  server.id = "ui-actions";
  server.command.resize(
      static_cast<std::size_t>(utf8_size));
  ASSERT_EQ(
      WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS,
          module_path.data(),
          static_cast<int>(module_path.size()),
          server.command.data(), utf8_size,
          nullptr, nullptr),
      utf8_size);
  ASSERT_TRUE(allowlist.AddOrUpdate(server));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, allowlist, limiter, logger);
  cx::mcp::AllowlistDialog dialog(
      allowlist, client);

  std::thread ui_thread([&] {
    dialog.Show(nullptr);
  });

  HWND hwnd = nullptr;
  HWND list = nullptr;
  for (int attempt = 0;
       attempt < 200 && (!hwnd || !list);
       ++attempt) {
    hwnd = FindWindowW(
        L"CXBuildMcpAllowlistDialog",
        L"CX MCP Allowlist");
    if (hwnd) {
      list = FindWindowExW(
          hwnd, nullptr, L"LISTBOX", nullptr);
    }
    if (!hwnd || !list) {
      Sleep(10);
    }
  }

  ASSERT_NE(hwnd, nullptr);
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(
      SendMessageW(list, LB_GETCOUNT, 0, 0),
      1);

  PostMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(5103, BN_CLICKED), 0);
  Sleep(50);

  SendMessageW(
      list, LB_SETCURSEL, 0, 0);
  PostMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(5103, BN_CLICKED), 0);
  Sleep(50);
  EXPECT_FALSE(client.IsRunning());

  PostMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(5104, BN_CLICKED), 0);
  Sleep(20);

  SendMessageW(
      list, LB_SETCURSEL, 0, 0);
  PostMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(5102, BN_CLICKED), 0);

  HWND confirm = nullptr;
  for (int attempt = 0;
       attempt < 200 && !confirm;
       ++attempt) {
    confirm = FindWindowW(
        L"#32770",
        L"CX MCP Allowlist");
    if (!confirm) {
      Sleep(10);
    }
  }

  if (confirm) {
    PostMessageW(
        confirm, WM_COMMAND,
        MAKEWPARAM(IDOK, BN_CLICKED), 0);
    for (int attempt = 0;
         attempt < 100 &&
         allowlist.IsAllowed("ui-actions");
         ++attempt) {
      Sleep(10);
    }
  }

  PostMessageW(
      hwnd, WM_COMMAND,
      MAKEWPARAM(5105, BN_CLICKED), 0);
  ui_thread.join();

  ASSERT_NE(confirm, nullptr);
  EXPECT_FALSE(
      allowlist.IsAllowed("ui-actions"));
}

TEST_F(PrivacyUiTest, AllowlistDialogOpensAndClosesCleanly) {
  UiFakePrompt prompt;
  cx::agent::PermissionManager permissions(
      *database_, prompt);
  cx::agent::LocalLogger logger(
      root_ / "logs" / "allowlist-ui.log");
  ASSERT_TRUE(logger.Open());
  cx::agent::AgentCore agent(permissions, logger);

  cx::mcp::AllowlistManager allowlist(
      root_ / "config" / "mcp_allowlist.json");
  ASSERT_TRUE(allowlist.Load());
  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, allowlist, limiter, logger);
  cx::mcp::AllowlistDialog dialog(
      allowlist, client);

  std::atomic<bool> entered{false};
  std::thread ui_thread([&] {
    entered.store(true);
    dialog.Show(nullptr);
  });

  while (!entered.load()) {
    Sleep(1);
  }

  HWND hwnd = nullptr;
  for (int attempt = 0;
       attempt < 100 && !hwnd;
       ++attempt) {
    hwnd = FindWindowW(
        L"CXBuildMcpAllowlistDialog",
        L"CX MCP Allowlist");
    if (!hwnd) {
      Sleep(10);
    }
  }

  HWND list = nullptr;
  if (hwnd) {
    for (int attempt = 0;
         attempt < 100 && !list;
         ++attempt) {
      list = FindWindowExW(
          hwnd, nullptr, L"LISTBOX", nullptr);
      if (!list) {
        Sleep(10);
      }
    }
  }

  const LRESULT list_count =
      list ? SendMessageW(list, LB_GETCOUNT, 0, 0) : LB_ERR;

  if (hwnd) {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
  ui_thread.join();

  ASSERT_NE(hwnd, nullptr);
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(list_count, 0);

  EXPECT_EQ(
      FindWindowW(
          L"CXBuildMcpAllowlistDialog",
          L"CX MCP Allowlist"),
      nullptr);
}

}  // namespace
