#include "browser/tab_manager.h"
#include "config/config_manager.h"
#include "storage/database.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

class PerformanceTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-performance-test-" +
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

TEST_F(PerformanceTest, NewTabP95StaysBelowOneHundredMilliseconds) {
  cx::browser::TabManager tabs(*database_);
  ASSERT_TRUE(tabs.Restore());

  double samples[40]{};
  for (int i = 0; i < 40; ++i) {
    const auto begin = std::chrono::steady_clock::now();
    const auto id = tabs.CreateTab(
        "about:blank",
        "Benchmark " + std::to_string(i));
    const auto end = std::chrono::steady_clock::now();
    ASSERT_TRUE(id.has_value());
    samples[i] =
        std::chrono::duration<double, std::milli>(
            end - begin).count();
  }

  std::sort(std::begin(samples), std::end(samples));
  const double p95 = samples[37];
  std::cout << "P10_NEW_TAB_P95_MS=" << p95 << "\n";
  EXPECT_LT(p95, 100.0);
}

TEST_F(PerformanceTest, ConfigSaveLoadOneHundredCyclesUnderOneSecond) {
  const auto path = root_ / "config.json";
  cx::config::ConfigManager manager(path);
  ASSERT_NE(
      manager.Load(),
      cx::config::LoadStatus::Failed);

  const auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < 100; ++i) {
    auto settings = manager.settings();
    settings.appearance.font_size_percent =
        75 + (i % 126);
    ASSERT_TRUE(manager.Set(settings));

    cx::config::ConfigManager reloaded(path);
    ASSERT_EQ(
        reloaded.Load(),
        cx::config::LoadStatus::Loaded);
  }
  const auto end = std::chrono::steady_clock::now();

  const double elapsed =
      std::chrono::duration<double, std::milli>(
          end - begin).count();
  std::cout << "P10_CONFIG_100_ROUNDTRIPS_MS="
            << elapsed << "\n";
  EXPECT_LT(elapsed, 1000.0);
}

}  // namespace
