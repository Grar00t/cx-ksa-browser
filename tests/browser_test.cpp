#include "browser/bookmark_service.h"
#include "browser/history_service.h"
#include "browser/navigation_controller.h"
#include "browser/tab_manager.h"
#include "storage/database.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
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
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());
  cx::browser::HistoryService history(*database_);

  cx::browser::NavigationController navigation(
      tabs, history);
  FakeSurface surface;
  navigation.AttachSurface(&surface);

  ASSERT_TRUE(navigation.ActivateTab(
      tabs.active_tab_id()));
  ASSERT_FALSE(surface.navigations.empty());
  EXPECT_EQ(surface.navigations.back(), L"about:blank");

  ASSERT_TRUE(
      navigation.NavigateAddress(L"example.test/one"));
  EXPECT_EQ(
      surface.navigations.back(),
      L"https://example.test/one");
  navigation.OnNavigationCompleted(
      true,
      "https://example.test/one",
      "One");

  ASSERT_TRUE(navigation.NavigateUrl(
      "https://example.test/two"));
  navigation.OnNavigationCompleted(
      true,
      "https://example.test/two",
      "Two");

  EXPECT_TRUE(navigation.CanGoBack());
  ASSERT_TRUE(navigation.Back());

  EXPECT_EQ(
      surface.navigations.back(),
      L"https://example.test/one");
  navigation.OnNavigationCompleted(
      true,
      "https://example.test/one",
      "One");
  EXPECT_TRUE(navigation.CanGoForward());

  ASSERT_TRUE(navigation.Forward());
  EXPECT_EQ(
      surface.navigations.back(),
      L"https://example.test/two");
  navigation.OnNavigationCompleted(
      true,
      "https://example.test/two",
      "Two");

  ASSERT_TRUE(navigation.Reload());
  EXPECT_EQ(surface.reloads, 1);
  navigation.OnNavigationCompleted(
      true,
      "https://example.test/two",
      "Two");

  const auto visits = history.Search("example.test");
  EXPECT_GE(visits.size(), 4u);
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

  EXPECT_FALSE(
      cx::browser::NavigationController::NormalizeAddress(
          L"javascript:alert(1)").has_value());
  EXPECT_FALSE(
      cx::browser::NavigationController::NormalizeAddress(
          L"   ").has_value());
}

}  // namespace
