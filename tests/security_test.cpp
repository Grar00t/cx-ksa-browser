#include "agent/agent_core.h"
#include "agent/permissions.h"
#include "config/config_manager.h"
#include "mcp/allowlist_manager.h"
#include "storage/database.h"

#include <gtest/gtest.h>

#include <iphlpapi.h>
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

class DenyPrompt final : public cx::agent::ConsentPrompt {
public:
  bool Request(
      HWND,
      const cx::agent::CapabilityDescriptor&) override {
    return false;
  }
};

std::size_t CountOwnedEstablishedTcp(DWORD pid) {
  ULONG bytes = 0;
  if (GetExtendedTcpTable(
          nullptr, &bytes, FALSE, AF_INET,
          TCP_TABLE_OWNER_PID_ALL, 0) !=
      ERROR_INSUFFICIENT_BUFFER) {
    return 0;
  }

  std::vector<unsigned char> buffer(bytes);
  auto* table =
      reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(
          buffer.data());
  if (GetExtendedTcpTable(
          table, &bytes, FALSE, AF_INET,
          TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
    return 0;
  }

  std::size_t count = 0;
  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    const auto& row = table->table[i];
    if (row.dwOwningPid == pid &&
        row.dwState == MIB_TCP_STATE_ESTAB) {
      ++count;
    }
  }
  return count;
}

std::size_t CountOwnedUdpEndpoints(DWORD pid) {
  ULONG bytes = 0;
  if (GetExtendedUdpTable(
          nullptr, &bytes, FALSE, AF_INET,
          UDP_TABLE_OWNER_PID, 0) !=
      ERROR_INSUFFICIENT_BUFFER) {
    return 0;
  }

  std::vector<unsigned char> buffer(bytes);
  auto* table =
      reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(
          buffer.data());
  if (GetExtendedUdpTable(
          table, &bytes, FALSE, AF_INET,
          UDP_TABLE_OWNER_PID, 0) != NO_ERROR) {
    return 0;
  }

  std::size_t count = 0;
  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    if (table->table[i].dwOwningPid == pid) {
      ++count;
    }
  }
  return count;
}

class SecurityTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::atomic<unsigned long> sequence{0};
    root_ = std::filesystem::temp_directory_path() /
        ("cx-security-test-" +
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

TEST_F(SecurityTest, LocalCoreCreatesNoOutboundEndpoints) {
  const DWORD pid = GetCurrentProcessId();
  const auto tcp_before = CountOwnedEstablishedTcp(pid);
  const auto udp_before = CountOwnedUdpEndpoints(pid);

  cx::storage::Database database(root_ / "data.db");
  ASSERT_TRUE(database.Open());
  ASSERT_TRUE(database.SetSetting(
      "security.probe", "local"));

  cx::config::ConfigManager config(
      root_ / "config.json");
  ASSERT_NE(
      config.Load(),
      cx::config::LoadStatus::Failed);

  cx::mcp::AllowlistManager allowlist(
      root_ / "mcp_allowlist.json");
  ASSERT_TRUE(allowlist.Load());
  EXPECT_TRUE(allowlist.List().empty());

  DenyPrompt prompt;
  cx::agent::PermissionManager permissions(
      database, prompt);
  cx::agent::LocalLogger logger(
      root_ / "logs" / "agent.log");
  ASSERT_TRUE(logger.Open());
  cx::agent::AgentCore agent(
      permissions, logger);
  EXPECT_FALSE(agent.Start(nullptr));
  EXPECT_EQ(agent.state(), cx::agent::AgentState::Stopped);

  Sleep(100);

  EXPECT_EQ(
      CountOwnedEstablishedTcp(pid),
      tcp_before);
  EXPECT_EQ(
      CountOwnedUdpEndpoints(pid),
      udp_before);
}

TEST(SecurityPolicyTest, UnsafeAndRemoteInputsFailClosed) {
  EXPECT_FALSE(
      cx::config::ConfigManager::IsLocalPath(
          std::filesystem::path(
              L"\\\\server\\share\\config.json")));

  cx::mcp::ServerConfig remote_like{
      "remote",
      "https://example.test/server.exe",
      {}};
  EXPECT_FALSE(
      cx::mcp::AllowlistManager::ValidateServer(
          remote_like));

  EXPECT_FALSE(
      cx::agent::ActionAllowlist::IsListed(
          "network.connect"));
  EXPECT_FALSE(
      cx::agent::ActionAllowlist::IsListed(
          "filesystem.delete"));
}

}  // namespace
