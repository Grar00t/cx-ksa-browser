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
#include <optional>
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

std::optional<std::size_t> CountOwnedEstablishedTcp(
    DWORD pid, ULONG family) {
  ULONG bytes = 0;
  if (GetExtendedTcpTable(
          nullptr, &bytes, FALSE, family,
          TCP_TABLE_OWNER_PID_ALL, 0) !=
      ERROR_INSUFFICIENT_BUFFER) {
    return std::nullopt;
  }

  std::vector<unsigned char> buffer(bytes);
  if (family == AF_INET) {
    auto* table =
        reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(
            buffer.data());
    if (GetExtendedTcpTable(
            table, &bytes, FALSE, family,
            TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
      return std::nullopt;
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

  if (family == AF_INET6) {
    auto* table =
        reinterpret_cast<PMIB_TCP6TABLE_OWNER_PID>(
            buffer.data());
    if (GetExtendedTcpTable(
            table, &bytes, FALSE, family,
            TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
      return std::nullopt;
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

  return std::nullopt;
}

std::optional<std::size_t> CountOwnedUdpEndpoints(
    DWORD pid, ULONG family) {
  ULONG bytes = 0;
  if (GetExtendedUdpTable(
          nullptr, &bytes, FALSE, family,
          UDP_TABLE_OWNER_PID, 0) !=
      ERROR_INSUFFICIENT_BUFFER) {
    return std::nullopt;
  }

  std::vector<unsigned char> buffer(bytes);
  if (family == AF_INET) {
    auto* table =
        reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(
            buffer.data());
    if (GetExtendedUdpTable(
            table, &bytes, FALSE, family,
            UDP_TABLE_OWNER_PID, 0) != NO_ERROR) {
      return std::nullopt;
    }

    std::size_t count = 0;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
      if (table->table[i].dwOwningPid == pid) {
        ++count;
      }
    }
    return count;
  }

  if (family == AF_INET6) {
    auto* table =
        reinterpret_cast<PMIB_UDP6TABLE_OWNER_PID>(
            buffer.data());
    if (GetExtendedUdpTable(
            table, &bytes, FALSE, family,
            UDP_TABLE_OWNER_PID, 0) != NO_ERROR) {
      return std::nullopt;
    }

    std::size_t count = 0;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
      if (table->table[i].dwOwningPid == pid) {
        ++count;
      }
    }
    return count;
  }

  return std::nullopt;
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
  const auto tcp4_before = CountOwnedEstablishedTcp(pid, AF_INET);
  const auto tcp6_before = CountOwnedEstablishedTcp(pid, AF_INET6);
  const auto udp4_before = CountOwnedUdpEndpoints(pid, AF_INET);
  const auto udp6_before = CountOwnedUdpEndpoints(pid, AF_INET6);
  ASSERT_TRUE(tcp4_before.has_value());
  ASSERT_TRUE(tcp6_before.has_value());
  ASSERT_TRUE(udp4_before.has_value());
  ASSERT_TRUE(udp6_before.has_value());

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

  const auto tcp4_after = CountOwnedEstablishedTcp(pid, AF_INET);
  const auto tcp6_after = CountOwnedEstablishedTcp(pid, AF_INET6);
  const auto udp4_after = CountOwnedUdpEndpoints(pid, AF_INET);
  const auto udp6_after = CountOwnedUdpEndpoints(pid, AF_INET6);
  ASSERT_TRUE(tcp4_after.has_value());
  ASSERT_TRUE(tcp6_after.has_value());
  ASSERT_TRUE(udp4_after.has_value());
  ASSERT_TRUE(udp6_after.has_value());

  EXPECT_EQ(*tcp4_after, *tcp4_before);
  EXPECT_EQ(*tcp6_after, *tcp6_before);
  EXPECT_EQ(*udp4_after, *udp4_before);
  EXPECT_EQ(*udp6_after, *udp6_before);
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
