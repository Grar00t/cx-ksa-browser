#include "browser/bookmark_service.h"
#include "browser/history_service.h"
#include "browser/navigation_controller.h"
#include "browser/tab_manager.h"
#include "storage/database.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path CrashWriterPath() {
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(),
      static_cast<DWORD>(buffer.size()));
  EXPECT_GT(length, 0u);
  return std::filesystem::path(
             std::wstring(buffer.data(), length))
      .parent_path() / L"browser_crash_writer.exe";
}

class FakeSurface final
    : public cx::browser::NavigationSurface {
public:
  bool NavigateTo(
      std::wstring_view url) override {
    if (!allow_navigation) {
      return false;
    }
    navigations.emplace_back(url);
    return true;
  }

  bool ReloadPage() override {
    if (!allow_reload) {
      return false;
    }
    ++reloads;
    return true;
  }

  bool allow_navigation = true;
  bool allow_reload = true;
  int reloads = 0;
  std::vector<std::wstring> navigations;
};

class BrowserTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-browser-test-" +
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

TEST_F(BrowserTest, SessionRestoresImmediatelyPersistedTabsAfterReopen) {
  ASSERT_TRUE(database_->SetSetting(
      "privacy.restore_session", "1"));
  cx::browser::TabManager first(*database_);
  ASSERT_TRUE(first.Restore());
  ASSERT_EQ(first.tabs().size(), 1u);

  const auto two = first.CreateTab(
      "https://example.test/two", "Two");
  const auto three = first.CreateTab(
      "https://example.test/three", "Three");
  ASSERT_TRUE(two.has_value());
  ASSERT_TRUE(three.has_value());

  ASSERT_TRUE(first.ActivateTab(*two));
  ASSERT_TRUE(first.UpdateTab(
      *two, "https://example.test/two/final", "Two Final"));
  const auto expected_active = first.active_tab_id();

  database_->Close();
  ASSERT_TRUE(database_->Open());

  cx::browser::TabManager restored(*database_);
  ASSERT_TRUE(restored.Restore());
  ASSERT_EQ(restored.tabs().size(), 3u);
  EXPECT_EQ(restored.active_tab_id(), expected_active);

  const auto active = restored.active_tab();
  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(
      active->url,
      "https://example.test/two/final");
  EXPECT_EQ(active->title, "Two Final");
}


TEST_F(BrowserTest, ClosingActiveMiddleTabActivatesRightNeighbor) {
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());

  const auto middle = tabs.CreateTab(
      "https://middle.test", "Middle");
  const auto right = tabs.CreateTab(
      "https://right.test", "Right");
  ASSERT_TRUE(middle.has_value());
  ASSERT_TRUE(right.has_value());

  ASSERT_TRUE(tabs.ActivateTab(*middle));
  ASSERT_EQ(tabs.active_tab_id(), *middle);

  ASSERT_TRUE(tabs.CloseTab(*middle));
  EXPECT_EQ(tabs.active_tab_id(), *right);
  ASSERT_TRUE(tabs.active_tab().has_value());
  EXPECT_EQ(tabs.active_tab()->title, "Right");
  EXPECT_EQ(tabs.tabs().size(), 2u);
}

TEST_F(BrowserTest, SupportsMoreThanTenTabsAndReturnsToOne) {
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());

  for (int i = 0; i < 12; ++i) {
    const auto id = tabs.CreateTab(
        "about:blank",
        "Tab " + std::to_string(i + 2));
    ASSERT_TRUE(id.has_value());
  }
  EXPECT_EQ(tabs.tabs().size(), 13u);
  EXPECT_EQ(database_->ListTabs().size(), 13u);

  while (tabs.tabs().size() > 1) {
    ASSERT_TRUE(
        tabs.CloseTab(tabs.tabs().back().id));
  }
  EXPECT_EQ(tabs.tabs().size(), 1u);
  EXPECT_EQ(database_->ListTabs().size(), 1u);
}

TEST_F(BrowserTest, AbruptExitRestoresCommittedSession) {
  ASSERT_TRUE(database_->SetSetting(
      "privacy.restore_session", "1"));
  database_->Close();

  const auto helper = CrashWriterPath();
  ASSERT_TRUE(std::filesystem::exists(helper));

  std::wstring command_line =
      L"\"" + helper.wstring() + L"\" \"" +
      (root_ / "data.db").wstring() + L"\"";

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  ASSERT_TRUE(CreateProcessW(
      helper.c_str(), command_line.data(),
      nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
      nullptr, nullptr, &startup, &process));

  ASSERT_EQ(
      WaitForSingleObject(process.hProcess, 5000),
      WAIT_OBJECT_0);
  DWORD exit_code = 0;
  ASSERT_TRUE(GetExitCodeProcess(
      process.hProcess, &exit_code));
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  ASSERT_EQ(exit_code, 77u);

  ASSERT_TRUE(database_->Open());
  cx::browser::TabManager restored(*database_);
  ASSERT_TRUE(restored.Restore());
  ASSERT_EQ(restored.tabs().size(), 3u);

  const auto active = restored.active_tab();
  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(
      active->url,
      "https://crash.test/second");
}

TEST_F(BrowserTest, NewTabCreationStaysUnderOneHundredMilliseconds) {
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());

  double max_ms = 0.0;
  for (int i = 0; i < 12; ++i) {
    const auto begin =
        std::chrono::steady_clock::now();
    const auto id = tabs.CreateTab(
        "about:blank",
        "Perf " + std::to_string(i));
    const auto end =
        std::chrono::steady_clock::now();
    ASSERT_TRUE(id.has_value());

    const double elapsed =
        std::chrono::duration<double, std::milli>(
            end - begin).count();
    if (elapsed > max_ms) {
      max_ms = elapsed;
    }
  }

  std::cout << "P06_MAX_NEW_TAB_MS=" << max_ms << "\n";
  EXPECT_LT(max_ms, 100.0);
}

TEST_F(BrowserTest, LogicalTabStressDoesNotLeakProcessHandles) {
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());

  DWORD before = 0;
  ASSERT_TRUE(GetProcessHandleCount(
      GetCurrentProcess(), &before));

  for (int cycle = 0; cycle < 25; ++cycle) {
    for (int i = 0; i < 12; ++i) {
      ASSERT_TRUE(tabs.CreateTab().has_value());
    }
    while (tabs.tabs().size() > 1) {
      ASSERT_TRUE(
          tabs.CloseTab(tabs.tabs().back().id));
    }
  }

  DWORD after = 0;
  ASSERT_TRUE(GetProcessHandleCount(
      GetCurrentProcess(), &after));
  EXPECT_LE(after, before + 2);
  EXPECT_EQ(tabs.tabs().size(), 1u);
  EXPECT_EQ(database_->ListTabs().size(), 1u);
}

TEST_F(BrowserTest, LocalHistorySearchMatchesTitleAndUrl) {
  ASSERT_GT(database_->AddHistory(
      "https://example.test/alpha",
      "Alpha Notes", 100), 0);
  ASSERT_GT(database_->AddHistory(
      "https://docs.test/openai",
      "Reference", 200), 0);
  ASSERT_GT(database_->AddHistory(
      "https://other.test/page",
      "OpenAI Local Browser", 300), 0);

  cx::browser::HistoryService history(*database_);

  const auto by_url = history.Search("docs.test");
  ASSERT_EQ(by_url.size(), 1u);
  EXPECT_EQ(
      by_url.front().url,
      "https://docs.test/openai");

  const auto by_title = history.Search("local browser");

  ASSERT_EQ(by_title.size(), 1u);
  EXPECT_EQ(
      by_title.front().title,
      "OpenAI Local Browser");
}

TEST_F(BrowserTest, BookmarksAreLocalAndUniqueByUrl) {
  cx::browser::BookmarkService bookmarks(*database_);

  const auto first = bookmarks.Add(
      "https://example.test", "Example");
  ASSERT_GT(first, 0);

  const auto updated = bookmarks.Add(
      "https://example.test", "Updated Example");
  EXPECT_EQ(updated, first);

  auto rows = bookmarks.List();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows.front().title, "Updated Example");

  EXPECT_TRUE(bookmarks.Remove(first));
  EXPECT_TRUE(bookmarks.List().empty());
}

TEST_F(BrowserTest, NavigationMaintainsPerTabBackForwardState) {
  ASSERT_TRUE(database_->SetSetting(
      "privacy.save_history", "1"));
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());
  cx::browser::HistoryService history(*database_);

  cx::browser::NavigationController navigation(
      tabs, history);
  FakeSurface surface;
  navigation.AttachSurface(&surface);

  std::uint64_t next_navigation_id = 1;
  const auto finish_navigation =
      [&](std::string_view url,
          std::string_view title,
          bool success = true) {
        const std::uint64_t navigation_id =
            next_navigation_id++;
        navigation.OnNavigationStarted(
            navigation_id, url);
        navigation.OnNavigationCompleted(
            navigation_id, success, url, title);
      };

  ASSERT_TRUE(navigation.ActivateTab(
      tabs.active_tab_id()));
  ASSERT_FALSE(surface.navigations.empty());
  EXPECT_EQ(surface.navigations.back(), L"about:blank");
  finish_navigation("about:blank", "New Tab");

  ASSERT_TRUE(
      navigation.NavigateAddress(L"example.test/one"));
  EXPECT_EQ(
      surface.navigations.back(),
      L"https://example.test/one");
  finish_navigation(
      "https://example.test/one", "One");

  ASSERT_TRUE(navigation.NavigateUrl(
      "https://example.test/two"));
  finish_navigation(
      "https://example.test/two", "Two");

  EXPECT_TRUE(navigation.CanGoBack());
  ASSERT_TRUE(navigation.Back());

  EXPECT_EQ(
      surface.navigations.back(),
      L"https://example.test/one");
  finish_navigation(
      "https://example.test/one", "One");
  EXPECT_TRUE(navigation.CanGoForward());

  ASSERT_TRUE(navigation.Forward());
  EXPECT_EQ(
      surface.navigations.back(),
      L"https://example.test/two");
  finish_navigation(
      "https://example.test/two", "Two");

  ASSERT_TRUE(navigation.Reload());
  EXPECT_EQ(surface.reloads, 1);
  finish_navigation(
      "https://example.test/two", "Two");

  const auto visits = history.Search("example.test");
  EXPECT_GE(visits.size(), 4u);
}

TEST_F(BrowserTest, NavigationCompletionUpdatesOriginatingTabById) {
  ASSERT_TRUE(database_->SetSetting(
      "privacy.save_history", "1"));
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());
  cx::browser::HistoryService history(*database_);

  cx::browser::NavigationController navigation(
      tabs, history);
  FakeSurface surface;
  navigation.AttachSurface(&surface);

  const auto first_id = tabs.active_tab_id();
  ASSERT_TRUE(navigation.NavigateUrl(
      "https://first.test/start"));
  navigation.OnNavigationStarted(
      101, "https://first.test/start");

  const auto second_id = tabs.CreateTab(
      "about:blank", "Second", false);
  ASSERT_TRUE(second_id.has_value());
  ASSERT_TRUE(navigation.ActivateTab(*second_id));
  navigation.OnNavigationStarted(
      202, "about:blank");
  navigation.OnNavigationCompleted(
      202, true, "about:blank", "Second");

  navigation.OnNavigationCompleted(
      101, true,
      "https://first.test/final",
      "First Final");

  const auto first = std::find_if(
      tabs.tabs().begin(),
      tabs.tabs().end(),
      [first_id](const auto& tab) {
        return tab.id == first_id;
      });
  const auto second = std::find_if(
      tabs.tabs().begin(),
      tabs.tabs().end(),
      [second_id](const auto& tab) {
        return tab.id == *second_id;
      });

  ASSERT_NE(first, tabs.tabs().end());
  ASSERT_NE(second, tabs.tabs().end());
  EXPECT_EQ(
      first->url,
      "https://first.test/final");
  EXPECT_EQ(first->title, "First Final");
  EXPECT_EQ(second->url, "about:blank");
  EXPECT_EQ(second->title, "Second");
}

TEST(NavigationAddressTest, NormalizesHostsAndRejectsUnsafeSchemes) {

  const auto host =
      cx::browser::NavigationController::NormalizeAddress(
          L"  example.com/path  ");
  ASSERT_TRUE(host.has_value());
  EXPECT_EQ(*host, "https://example.com/path");

  const auto secure =
      cx::browser::NavigationController::NormalizeAddress(
          L"https://example.com");
  ASSERT_TRUE(secure.has_value());
  EXPECT_EQ(*secure, "https://example.com");

  const auto search =
      cx::browser::NavigationController::NormalizeAddress(
          L"privacy browser");
  ASSERT_TRUE(search.has_value());
  EXPECT_EQ(*search, "https://duckduckgo.com/?q=privacy+browser");

  const auto google =
      cx::browser::NavigationController::NormalizeAddress(
          L"!g browser security");
  ASSERT_TRUE(google.has_value());
  EXPECT_EQ(
      *google,
      "https://www.google.com/search?q=browser+security");

  const auto bing =
      cx::browser::NavigationController::NormalizeAddress(
          L"!b Arabic search");
  ASSERT_TRUE(bing.has_value());
  EXPECT_EQ(
      *bing,
      "https://www.bing.com/search?q=Arabic+search");

  const auto arabic =
      cx::browser::NavigationController::NormalizeAddress(
          L"\u0628\u062d\u062b \u0639\u0631\u0628\u064a");
  ASSERT_TRUE(arabic.has_value());
  EXPECT_EQ(
      *arabic,
      "https://duckduckgo.com/?q=%D8%A8%D8%AD%D8%AB+"
      "%D8%B9%D8%B1%D8%A8%D9%8A");

  EXPECT_TRUE(
      cx::browser::NavigationController::IsAllowedUrl(
          L"file:///C:/Users/A/report.txt"));
  EXPECT_TRUE(
      cx::browser::NavigationController::IsAllowedUrl(
          L"file://localhost/C:/Users/A/report.txt"));
  EXPECT_TRUE(
      cx::browser::NavigationController::IsAllowedUrl(
          L"about:blank"));

  EXPECT_FALSE(
      cx::browser::NavigationController::IsAllowedUrl(
          L"file://server/share/report.txt"));
  EXPECT_FALSE(
      cx::browser::NavigationController::IsAllowedUrl(
          L"file://localhost.evil/share/report.txt"));
  EXPECT_FALSE(
      cx::browser::NavigationController::IsAllowedUrl(
          L"about:blankevil"));

  EXPECT_FALSE(
      cx::browser::NavigationController::NormalizeAddress(
          L"javascript:alert(1)").has_value());
  EXPECT_FALSE(
      cx::browser::NavigationController::NormalizeAddress(
          L"file://server/share/report.txt").has_value());
  EXPECT_FALSE(
      cx::browser::NavigationController::NormalizeAddress(
          L"   ").has_value());
}


TEST_F(BrowserTest, EdgeOperationsFailClosedAndSingleCloseReplacesBlank) {
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());
  ASSERT_EQ(tabs.tabs().size(), 1u);

  const auto first_id = tabs.active_tab_id();
  EXPECT_TRUE(tabs.ActivateTab(first_id));
  EXPECT_FALSE(tabs.ActivateTab(999999));
  EXPECT_FALSE(tabs.CloseTab(999999));
  EXPECT_FALSE(tabs.UpdateTab(999999, "https://x.test", "X"));
  EXPECT_FALSE(tabs.UpdateTab(first_id, "", "X"));

  ASSERT_TRUE(tabs.UpdateTab(
      first_id, "https://title.test", ""));
  ASSERT_TRUE(tabs.active_tab().has_value());
  EXPECT_EQ(tabs.active_tab()->title, "New Tab");

  ASSERT_TRUE(tabs.CloseTab(first_id));
  ASSERT_EQ(tabs.tabs().size(), 1u);
  EXPECT_NE(tabs.active_tab_id(), first_id);
  ASSERT_TRUE(tabs.active_tab().has_value());
  EXPECT_EQ(tabs.active_tab()->url, "about:blank");

  const auto inactive = tabs.CreateTab("", "", false);
  ASSERT_TRUE(inactive.has_value());
  EXPECT_NE(tabs.active_tab_id(), *inactive);
  const auto created = database_->GetTab(*inactive);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->url, "about:blank");
  EXPECT_EQ(created->title, "New Tab");
}

TEST_F(BrowserTest, RestoreNormalizesPositionsAndInvalidActiveId) {
  ASSERT_TRUE(database_->SetSetting(
      "privacy.restore_session", "1"));

  const auto first = database_->AddTab(
      7, "https://one.test", "One", false);
  const auto second = database_->AddTab(
      42, "https://two.test", "Two", false);
  ASSERT_GT(first, 0);
  ASSERT_GT(second, 0);

  const std::vector<std::string> invalid_ids = {
      "", "abc", "-1", "1x", "999999999999999999999999"};
  for (const auto& invalid : invalid_ids) {
    ASSERT_TRUE(database_->SetSetting(
        "browser.active_tab_id", invalid));
    cx::browser::TabManager restored(*database_);
    ASSERT_TRUE(restored.Restore());
    ASSERT_EQ(restored.tabs().size(), 2u);
    EXPECT_EQ(restored.tabs()[0].position, 0);
    EXPECT_EQ(restored.tabs()[1].position, 1);
    EXPECT_EQ(restored.active_tab_id(), first);
  }
}

TEST_F(BrowserTest, NavigationFailClosedPathsDoNotMutateState) {
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());
  cx::browser::HistoryService history(*database_);
  cx::browser::NavigationController navigation(
      tabs, history);

  EXPECT_FALSE(navigation.Back());
  EXPECT_FALSE(navigation.Forward());
  EXPECT_FALSE(navigation.Reload());
  EXPECT_FALSE(navigation.NavigateUrl(""));
  EXPECT_FALSE(navigation.NavigateUrl(
      "javascript:alert(1)"));
  EXPECT_FALSE(navigation.NavigateAddress(
      L"file:///C:/Windows/System32"));
  EXPECT_FALSE(navigation.CanGoBack());
  EXPECT_FALSE(navigation.CanGoForward());

  FakeSurface surface;
  surface.allow_navigation = false;
  surface.allow_reload = false;
  navigation.AttachSurface(&surface);
  EXPECT_FALSE(navigation.NavigateUrl(
      "https://blocked.test"));
  EXPECT_FALSE(navigation.Reload());

  navigation.ForgetTab(tabs.active_tab_id());
  navigation.OnNavigationCompleted(
      999, false,
      "https://failed.test",
      "Failed");
  EXPECT_TRUE(history.Recent().empty());
}

}  // namespace
