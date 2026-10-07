#include "agent/agent_core.h"
#include "agent/permissions.h"
#include "storage/database.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakePrompt final : public cx::agent::ConsentPrompt {
public:
  explicit FakePrompt(std::vector<bool> answers)
      : answers_(std::move(answers)) {}

  bool Request(
      HWND, const cx::agent::CapabilityDescriptor& capability) override {
    requested_.push_back(capability.capability);
    if (next_ >= answers_.size()) {
      return false;
    }
    return answers_[next_++];
  }

  std::size_t count() const noexcept {
    return requested_.size();
  }

  const std::vector<cx::agent::Capability>& requested() const {
    return requested_;
  }

private:
  std::vector<bool> answers_;
  std::vector<cx::agent::Capability> requested_;
  std::size_t next_ = 0;
};

class AgentTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-agent-test-" + std::to_string(GetCurrentProcessId()) + "-" +
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

  std::unique_ptr<cx::agent::LocalLogger> OpenLogger() {
    auto logger = std::make_unique<cx::agent::LocalLogger>(
        root_ / "logs" / "agent.log");
    EXPECT_TRUE(logger->Open());
    return logger;
  }

  std::filesystem::path root_;
  std::unique_ptr<cx::storage::Database> database_;
};

TEST_F(AgentTest, DefaultDeniedAndAgentDoesNotStart) {
  FakePrompt prompt({false});
  cx::agent::PermissionManager permissions(*database_, prompt);
  auto logger = OpenLogger();
  cx::agent::AgentCore agent(permissions, *logger);

  EXPECT_FALSE(permissions.IsGranted(
      cx::agent::Capability::AgentRun));
  EXPECT_FALSE(agent.Start(nullptr));
  EXPECT_EQ(agent.state(), cx::agent::AgentState::Stopped);

  EXPECT_EQ(prompt.count(), 1u);

  const auto stored = database_->GetPermission("agent.run");
  ASSERT_TRUE(stored.has_value());
  EXPECT_FALSE(*stored);
}

TEST_F(AgentTest, GrantedStartConsentIsPersistedAndReused) {
  FakePrompt prompt({true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  auto logger = OpenLogger();
  cx::agent::AgentCore agent(permissions, *logger);

  ASSERT_TRUE(agent.Start(nullptr));
  EXPECT_EQ(agent.state(), cx::agent::AgentState::Running);
  EXPECT_EQ(prompt.count(), 1u);

  agent.Stop();
  ASSERT_TRUE(agent.Start(nullptr));
  EXPECT_EQ(prompt.count(), 1u);
  EXPECT_TRUE(permissions.IsGranted(
      cx::agent::Capability::AgentRun));
}

TEST_F(AgentTest, UnknownActionIsDeniedByAllowlist) {
  FakePrompt prompt({true});

  cx::agent::PermissionManager permissions(*database_, prompt);
  auto logger = OpenLogger();
  cx::agent::AgentCore agent(permissions, *logger);

  ASSERT_TRUE(agent.Start(nullptr));
  EXPECT_FALSE(agent.AuthorizeAction(
      nullptr, "filesystem.delete_everything"));
  EXPECT_EQ(prompt.count(), 1u);
  EXPECT_FALSE(cx::agent::ActionAllowlist::IsListed(
      "filesystem.delete_everything"));
}

TEST_F(AgentTest, AllowlistedActionRequiresSeparateConsent) {
  FakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  auto logger = OpenLogger();
  cx::agent::AgentCore agent(permissions, *logger);

  ASSERT_TRUE(agent.Start(nullptr));
  ASSERT_TRUE(agent.AuthorizeAction(
      nullptr, "browser.read_page"));
  EXPECT_EQ(prompt.count(), 2u);
  ASSERT_EQ(prompt.requested().size(), 2u);
  EXPECT_EQ(prompt.requested()[1],
            cx::agent::Capability::ReadPage);

  EXPECT_TRUE(agent.AuthorizeAction(
      nullptr, "browser.read_page"));
  EXPECT_EQ(prompt.count(), 2u);
}

TEST_F(AgentTest, EveryAllowlistedCapabilityPromptsSeparately) {
  FakePrompt prompt({true, true, true, true, true, true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  auto logger = OpenLogger();
  cx::agent::AgentCore agent(permissions, *logger);

  ASSERT_TRUE(agent.Start(nullptr));
  const std::vector<std::string_view> actions = {
      "browser.read_page",
      "browser.navigate",
      "tabs.manage",
      "clipboard.write",
      "native_messaging.connect",
      "mcp.connect",
  };
  for (const auto action : actions) {
    EXPECT_TRUE(agent.AuthorizeAction(nullptr, action));
  }
  EXPECT_EQ(prompt.count(), 7u);
  EXPECT_EQ(permissions.Granted().size(), 7u);
}

TEST_F(AgentTest, PermissionRejectionDoesNotCrashAgent) {
  FakePrompt prompt({true, false});
  cx::agent::PermissionManager permissions(*database_, prompt);
  auto logger = OpenLogger();
  cx::agent::AgentCore agent(permissions, *logger);

  ASSERT_TRUE(agent.Start(nullptr));
  EXPECT_FALSE(agent.AuthorizeAction(
      nullptr, "browser.navigate"));
  EXPECT_EQ(agent.state(), cx::agent::AgentState::Running);
  EXPECT_FALSE(permissions.IsGranted(
      cx::agent::Capability::Navigate));
}

TEST_F(AgentTest, RevokeAllReturnsPermissionsToDenied) {
  FakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  auto logger = OpenLogger();
  cx::agent::AgentCore agent(permissions, *logger);

  ASSERT_TRUE(agent.Start(nullptr));
  ASSERT_TRUE(agent.AuthorizeAction(
      nullptr, "tabs.manage"));
  ASSERT_EQ(permissions.Granted().size(), 2u);

  ASSERT_TRUE(permissions.RevokeAll());
  EXPECT_TRUE(permissions.Granted().empty());
  EXPECT_FALSE(permissions.IsGranted(
      cx::agent::Capability::AgentRun));

  EXPECT_FALSE(permissions.IsGranted(
      cx::agent::Capability::ManageTabs));
}

TEST_F(AgentTest, LocalLoggerWritesOnlyRequestedFile) {
  const auto path = root_ / "logs" / "agent.log";
  cx::agent::LocalLogger logger(path);
  ASSERT_TRUE(logger.Open());
  ASSERT_TRUE(logger.Log("unit_event", "local-only"));
  EXPECT_EQ(logger.path(), path);
  EXPECT_TRUE(std::filesystem::exists(path));

  std::ifstream input(path);
  std::string line;
  ASSERT_TRUE(static_cast<bool>(std::getline(input, line)));
  EXPECT_NE(line.find("unit_event"), std::string::npos);
  EXPECT_NE(line.find("local-only"), std::string::npos);
}



TEST(AgentDescriptorTest, IdMappingAndUnknownCapabilityBehavior) {
  const std::vector<std::pair<std::string_view, cx::agent::Capability>>
      expected = {
          {"agent.run", cx::agent::Capability::AgentRun},
          {"browser.read_page", cx::agent::Capability::ReadPage},
          {"browser.navigate", cx::agent::Capability::Navigate},
          {"tabs.manage", cx::agent::Capability::ManageTabs},
          {"clipboard.write", cx::agent::Capability::ClipboardWrite},
          {"native_messaging.connect", cx::agent::Capability::NativeMessaging},
          {"mcp.connect", cx::agent::Capability::McpConnect},
      };

  for (const auto& [id, capability] : expected) {
    const auto parsed =
        cx::agent::CapabilityFromId(id);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, capability);
    EXPECT_EQ(
        cx::agent::DescribeCapability(capability).id,
        id);
  }

  EXPECT_FALSE(
      cx::agent::CapabilityFromId(
          "unknown.capability").has_value());
  EXPECT_THROW(
      cx::agent::DescribeCapability(
          static_cast<cx::agent::Capability>(999)),
      std::invalid_argument);
}

TEST_F(AgentTest, RevokeSingleCapabilityPersistsDenied) {
  FakePrompt prompt({true});
  cx::agent::PermissionManager permissions(
      *database_, prompt);
  ASSERT_TRUE(permissions.Ensure(
      nullptr,
      cx::agent::Capability::Navigate));
  ASSERT_TRUE(permissions.IsGranted(
      cx::agent::Capability::Navigate));
  ASSERT_TRUE(permissions.Revoke(
      cx::agent::Capability::Navigate));
  EXPECT_FALSE(permissions.IsGranted(
      cx::agent::Capability::Navigate));
}

TEST(AgentDefaultLogPath, UsesAppDataCxBuildLogs) {
  wchar_t* appdata = nullptr;
  std::size_t length = 0;
  ASSERT_EQ(_wdupenv_s(&appdata, &length, L"APPDATA"), 0);
  ASSERT_NE(appdata, nullptr);

  const auto expected =
      std::filesystem::path(appdata) /
      L"CX Build" / L"logs" / L"agent.log";
  std::free(appdata);

  EXPECT_EQ(cx::agent::LocalLogger::DefaultPath(), expected);
}

}  // namespace
