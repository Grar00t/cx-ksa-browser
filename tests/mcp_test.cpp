#include "agent/agent_core.h"
#include "agent/permissions.h"
#include "mcp/allowlist_manager.h"
#include "mcp/mcp_client.h"
#include "mcp/rate_limiter.h"
#include "storage/database.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

class McpFakePrompt final : public cx::agent::ConsentPrompt {
public:
  explicit McpFakePrompt(std::vector<bool> answers)
      : answers_(std::move(answers)) {}

  bool Request(
      HWND,
      const cx::agent::CapabilityDescriptor& capability) override {
    requested_.push_back(capability.capability);

    if (next_ >= answers_.size()) {
      return false;
    }
    return answers_[next_++];
  }

  std::size_t count() const noexcept {
    return requested_.size();
  }

private:
  std::vector<bool> answers_;
  std::vector<cx::agent::Capability> requested_;
  std::size_t next_ = 0;
};

std::string WideToUtf8(std::wstring_view value) {
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0,
      nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string output(static_cast<std::size_t>(size), '\0');
  EXPECT_EQ(
      WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), output.data(), size,
          nullptr, nullptr),
      size);

  return output;
}

std::filesystem::path TestServerPath() {
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(),
      static_cast<DWORD>(buffer.size()));
  EXPECT_GT(length, 0u);
  return std::filesystem::path(
             std::wstring(buffer.data(), length))
      .parent_path() / L"mcp_test_server.exe";
}

class McpTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-mcp-test-" +
         std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(sequence.fetch_add(1)));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);

    database_ = std::make_unique<cx::storage::Database>(
        root_ / "data.db");
    ASSERT_TRUE(database_->Open());

    allowlist_ = std::make_unique<cx::mcp::AllowlistManager>(

        root_ / "config" / "mcp_allowlist.json");
    ASSERT_TRUE(allowlist_->Load());

    logger_ = std::make_unique<cx::agent::LocalLogger>(
        root_ / "logs" / "mcp.log");
    ASSERT_TRUE(logger_->Open());
    ASSERT_TRUE(std::filesystem::exists(TestServerPath()));
  }

  void TearDown() override {
    logger_.reset();
    allowlist_.reset();
    database_.reset();
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  cx::mcp::ServerConfig EchoServer(
      std::string id = "echo") const {
    cx::mcp::ServerConfig server;
    server.id = std::move(id);
    server.command =
        WideToUtf8(TestServerPath().wstring());
    return server;
  }

  std::string ReadLog() const {
    std::ifstream input(root_ / "logs" / "mcp.log");
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());

  }

  std::filesystem::path root_;
  std::unique_ptr<cx::storage::Database> database_;
  std::unique_ptr<cx::mcp::AllowlistManager> allowlist_;
  std::unique_ptr<cx::agent::LocalLogger> logger_;
};

TEST_F(McpTest, MissingAllowlistStartsEmptyAndPersists) {
  EXPECT_TRUE(allowlist_->List().empty());
  EXPECT_TRUE(std::filesystem::exists(allowlist_->path()));

  auto server = EchoServer();
  server.args = {"--flag", "hello world", "quote\"value"};
  ASSERT_TRUE(allowlist_->AddOrUpdate(server));

  cx::mcp::AllowlistManager reloaded(allowlist_->path());
  ASSERT_TRUE(reloaded.Load());
  const auto stored = reloaded.Find("echo");
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(*stored, server);
}

TEST_F(McpTest, NetworkAndNonFixedExecutablePathsAreRejected) {
  auto server = EchoServer();

  server.command = R"(\\server\share\remote.exe)";
  EXPECT_FALSE(cx::mcp::AllowlistManager::ValidateServer(server));

  bool exercised_non_fixed_drive = false;
  for (wchar_t letter = L'Z'; letter >= L'D'; --letter) {
    const std::wstring root{letter, L':', L'\\'};
    if (GetDriveTypeW(root.c_str()) == DRIVE_FIXED) {
      continue;
    }

    server.command = WideToUtf8(root + L"remote.exe");
    EXPECT_FALSE(cx::mcp::AllowlistManager::ValidateServer(server));
    exercised_non_fixed_drive = true;
    break;
  }
  EXPECT_TRUE(exercised_non_fixed_drive);
}

TEST_F(McpTest, Utf8BomConfigLoads) {
  const auto path = root_ / "config" / "bom.json";
  {
    std::ofstream output(path, std::ios::binary);
    output << "\xEF\xBB\xBF"
           << "{\"version\":1,\"servers\":[]}";
  }

  cx::mcp::AllowlistManager manager(path);
  EXPECT_TRUE(manager.Load());
  EXPECT_TRUE(manager.List().empty());
}

TEST_F(McpTest, InvalidJsonFailsClosed) {
  const auto path = root_ / "config" / "invalid.json";
  {
    std::ofstream output(path);
    output << "{\"version\":1,\"servers\":[";
  }

  cx::mcp::AllowlistManager invalid(path);
  EXPECT_FALSE(invalid.Load());
  EXPECT_TRUE(invalid.List().empty());
}

TEST_F(McpTest, ServerOutsideAllowlistIsRejectedBeforeMcpConsent) {
  McpFakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  cx::agent::AgentCore agent(permissions, *logger_);
  ASSERT_TRUE(agent.Start(nullptr));
  ASSERT_EQ(prompt.count(), 1u);

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, *allowlist_, limiter, *logger_);

  EXPECT_FALSE(client.Start(nullptr, "not-listed"));
  EXPECT_FALSE(client.IsRunning());
  EXPECT_EQ(prompt.count(), 1u);

  const std::string log = ReadLog();
  EXPECT_NE(
      log.find("mcp_server_denied_not_allowlisted"),
      std::string::npos);
}

TEST_F(McpTest, NoServerSpawnsBeforeExplicitStart) {
  const auto marker = root_ / "server-started.txt";
  auto server = EchoServer();
  server.args = {
      "--marker", WideToUtf8(marker.wstring())};
  ASSERT_TRUE(allowlist_->AddOrUpdate(server));

  McpFakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);

  cx::agent::AgentCore agent(permissions, *logger_);
  ASSERT_TRUE(agent.Start(nullptr));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, *allowlist_, limiter, *logger_);

  Sleep(100);
  EXPECT_FALSE(std::filesystem::exists(marker));

  ASSERT_TRUE(client.Start(nullptr, "echo"));
  for (int i = 0;
       i < 50 && !std::filesystem::exists(marker);
       ++i) {
    Sleep(10);
  }
  EXPECT_TRUE(std::filesystem::exists(marker));
  EXPECT_TRUE(client.IsRunning());
}

TEST_F(McpTest, StdioRoundTripWorksAndRequestIsLoggedLocally) {
  ASSERT_TRUE(allowlist_->AddOrUpdate(EchoServer()));

  McpFakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  cx::agent::AgentCore agent(permissions, *logger_);
  ASSERT_TRUE(agent.Start(nullptr));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, *allowlist_, limiter, *logger_);
  ASSERT_TRUE(client.Start(nullptr, "echo"));

  const std::string request =
      R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
  std::string response;
  ASSERT_TRUE(client.SendRequest(
      "echo", request, &response,
      std::chrono::seconds(2)));
  EXPECT_EQ(response, request);

  const std::string log = ReadLog();
  EXPECT_NE(
      log.find("\tmcp_request\tserver=echo bytes="),
      std::string::npos);
  EXPECT_NE(
      log.find("\tmcp_response_received\tserver=echo bytes="),
      std::string::npos);
  EXPECT_EQ(log.find(request), std::string::npos);
}

TEST_F(McpTest, DiscardedResponseIsDrainedBeforeNextRequest) {
  ASSERT_TRUE(allowlist_->AddOrUpdate(EchoServer()));

  McpFakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  cx::agent::AgentCore agent(permissions, *logger_);
  ASSERT_TRUE(agent.Start(nullptr));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, *allowlist_, limiter, *logger_);
  ASSERT_TRUE(client.Start(nullptr, "echo"));

  const std::string first =
      R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
  const std::string second =
      R"({"jsonrpc":"2.0","id":2,"method":"ping"})";
  ASSERT_TRUE(client.SendRequest(
      "echo", first, nullptr, std::chrono::seconds(2)));

  std::string response;
  ASSERT_TRUE(client.SendRequest(
      "echo", second, &response, std::chrono::seconds(2)));
  EXPECT_EQ(response, second);
}

TEST_F(McpTest, ResponseTimeoutInvalidatesTransport) {
  auto server = EchoServer();
  server.args = {"--response-delay-ms", "250"};
  ASSERT_TRUE(allowlist_->AddOrUpdate(server));

  McpFakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  cx::agent::AgentCore agent(permissions, *logger_);
  ASSERT_TRUE(agent.Start(nullptr));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, *allowlist_, limiter, *logger_);
  ASSERT_TRUE(client.Start(nullptr, "echo"));

  std::string response;
  EXPECT_FALSE(client.SendRequest(
      "echo",
      R"({"jsonrpc":"2.0","id":1,"method":"slow"})",
      &response, std::chrono::milliseconds(25)));
  EXPECT_FALSE(client.IsRunning());
}

TEST_F(McpTest, WriteTimeoutInvalidatesTransport) {
  auto server = EchoServer();
  server.args = {"--startup-delay-ms", "1000"};
  ASSERT_TRUE(allowlist_->AddOrUpdate(server));

  McpFakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  cx::agent::AgentCore agent(permissions, *logger_);
  ASSERT_TRUE(agent.Start(nullptr));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, *allowlist_, limiter, *logger_);
  ASSERT_TRUE(client.Start(nullptr, "echo"));

  const std::string payload(512 * 1024, 'x');
  const std::string request =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"bulk\","
      "\"payload\":\"" + payload + "\"}";

  const auto started = std::chrono::steady_clock::now();
  std::string response;
  EXPECT_FALSE(client.SendRequest(
      "echo", request, &response, std::chrono::milliseconds(50)));
  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_LT(elapsed, std::chrono::milliseconds(750));
  EXPECT_FALSE(client.IsRunning());
}

TEST_F(McpTest, ClientEnforcesTenRequestsPerSecond) {
  ASSERT_TRUE(allowlist_->AddOrUpdate(EchoServer()));

  McpFakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  cx::agent::AgentCore agent(permissions, *logger_);
  ASSERT_TRUE(agent.Start(nullptr));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, *allowlist_, limiter, *logger_);
  ASSERT_TRUE(client.Start(nullptr, "echo"));

  for (int i = 0; i < 10; ++i) {
    const std::string request =
        "{\"jsonrpc\":\"2.0\",\"id\":" +
        std::to_string(i + 1) +
        ",\"method\":\"ping\"}";
    EXPECT_TRUE(client.SendRequest(
        "echo", request, nullptr));
  }

  EXPECT_FALSE(client.SendRequest(
      "echo",
      R"({"jsonrpc":"2.0","id":11,"method":"ping"})",
      nullptr));

  const std::string log = ReadLog();
  std::size_t request_count = 0;
  std::size_t position = 0;
  while ((position = log.find(
              "\tmcp_request\t", position)) != std::string::npos) {
    ++request_count;
    position += 1;
  }
  EXPECT_EQ(request_count, 11u);
  EXPECT_NE(
      log.find("mcp_request_rate_limited"),
      std::string::npos);
}

TEST_F(McpTest, RemovedServerCannotReceiveAnotherRequest) {

  ASSERT_TRUE(allowlist_->AddOrUpdate(EchoServer()));

  McpFakePrompt prompt({true, true});
  cx::agent::PermissionManager permissions(*database_, prompt);
  cx::agent::AgentCore agent(permissions, *logger_);
  ASSERT_TRUE(agent.Start(nullptr));

  cx::mcp::RateLimiter limiter(10);
  cx::mcp::McpClient client(
      agent, *allowlist_, limiter, *logger_);
  ASSERT_TRUE(client.Start(nullptr, "echo"));

  ASSERT_TRUE(allowlist_->Remove("echo"));
  EXPECT_FALSE(client.SendRequest(
      "echo",
      R"({"jsonrpc":"2.0","id":1,"method":"ping"})",
      nullptr));
  EXPECT_FALSE(client.IsRunning());
}

TEST(RateLimiterTest, LimitIsPerServerAndWindowResets) {
  cx::mcp::RateLimiter limiter(10);
  const auto now = cx::mcp::RateLimiter::Clock::now();

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(limiter.AllowAt("one", now));
  }
  EXPECT_FALSE(limiter.AllowAt("one", now));
  EXPECT_TRUE(limiter.AllowAt("two", now));

  EXPECT_TRUE(limiter.AllowAt(
      "one", now + std::chrono::milliseconds(1001)));
  EXPECT_EQ(limiter.limit(), 10u);
  EXPECT_EQ(
      limiter.window(), std::chrono::milliseconds(1000));
}

TEST(McpAllowlistDefaultPath, UsesAppDataConfigDirectory) {
  wchar_t* appdata = nullptr;
  std::size_t length = 0;
  ASSERT_EQ(
      _wdupenv_s(&appdata, &length, L"APPDATA"), 0);
  ASSERT_NE(appdata, nullptr);

  const auto expected =
      std::filesystem::path(appdata) /
      L"CX Build" / L"config" / L"mcp_allowlist.json";
  std::free(appdata);

  EXPECT_EQ(
      cx::mcp::AllowlistManager::DefaultPath(),
      expected);
}


TEST_F(McpTest, AllowlistValidationRejectsUnsafeShapes) {
  auto valid = EchoServer("valid.server-1");
  EXPECT_TRUE(
      cx::mcp::AllowlistManager::ValidateServer(valid));

  auto invalid = valid;
  invalid.id.clear();
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));

  invalid = valid;
  invalid.id = "bad id";
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));

  invalid = valid;
  invalid.id = std::string(65, 'x');
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));

  invalid = valid;
  invalid.command.clear();
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));

  invalid = valid;
  invalid.command = "relative.exe";
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));

  invalid = valid;
  invalid.command =
      WideToUtf8((root_ / "server.txt").wstring());
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));

  invalid = valid;
  invalid.args.assign(65, "x");
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));

  invalid = valid;
  invalid.args = {std::string(8193, 'x')};
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));

  invalid = valid;
  invalid.args = {std::string("a\0b", 3)};
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(invalid));
}

TEST_F(McpTest, AllowlistUpdateSortFindAndRemoveAreDeterministic) {
  auto zeta = EchoServer("zeta");
  zeta.args = {"first"};
  auto alpha = EchoServer("alpha");

  ASSERT_TRUE(allowlist_->AddOrUpdate(zeta));
  ASSERT_TRUE(allowlist_->AddOrUpdate(alpha));

  auto rows = allowlist_->List();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].id, "alpha");
  EXPECT_EQ(rows[1].id, "zeta");
  EXPECT_FALSE(allowlist_->Find("missing").has_value());

  zeta.args = {"updated"};
  ASSERT_TRUE(allowlist_->AddOrUpdate(zeta));
  const auto updated = allowlist_->Find("zeta");
  ASSERT_TRUE(updated.has_value());
  ASSERT_EQ(updated->args.size(), 1u);
  EXPECT_EQ(updated->args[0], "updated");

  EXPECT_FALSE(allowlist_->Remove("missing"));
  EXPECT_TRUE(allowlist_->Remove("alpha"));
  EXPECT_FALSE(allowlist_->IsAllowed("alpha"));
}

TEST_F(McpTest, MalformedAllowlistVariantsFailClosed) {
  const std::vector<std::string> invalid_json = {
      R"({"version":2,"servers":[]})",
      R"({"version":1})",
      R"({"servers":[]})",
      R"({"version":1,"servers":[],"extra":1})",
      R"({"version":1,"servers":[{"id":"x","command":"C:\\x.exe"}]})",
      R"({"version":1,"servers":[{"id":"x","command":"C:\\x.exe","args":[]},{"id":"x","command":"C:\\x.exe","args":[]}]})",
      R"({"version":1,"servers":[{"id":"bad id","command":"C:\\x.exe","args":[]}]})",
      "{\"version\":1,\"servers\":[],}"
  };

  for (std::size_t i = 0;
       i < invalid_json.size();
       ++i) {
    const auto path =
        root_ / "config" /
        ("invalid-" + std::to_string(i) + ".json");
    {
      std::ofstream output(path, std::ios::binary);
      output << invalid_json[i];
    }
    cx::mcp::AllowlistManager manager(path);
    EXPECT_FALSE(manager.Load()) << "case " << i;
    EXPECT_TRUE(manager.List().empty());
  }
}

}  // namespace
