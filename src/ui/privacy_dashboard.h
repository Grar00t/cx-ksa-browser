#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace cx::agent {
class PermissionManager;
}

namespace cx::mcp {
class AllowlistManager;
class McpClient;
}

namespace cx::ui {

class PrivacyDashboard {
public:
  PrivacyDashboard(
      agent::PermissionManager& permissions,
      mcp::AllowlistManager& allowlist,
      mcp::McpClient& mcp_client,
      std::filesystem::path local_root);

  std::wstring Summary() const;
  void RefreshLocalSizeAsync(
      HWND target,
      UINT completion_message) const;
  bool AcceptLocalSizeResult(LPARAM lparam);
  std::optional<std::uintmax_t> local_size() const noexcept;
  const std::filesystem::path& local_root() const noexcept;

private:
  static std::uintmax_t ComputeDirectorySize(
      const std::filesystem::path& root);
  static std::wstring FormatBytes(std::uintmax_t bytes);

  agent::PermissionManager& permissions_;
  mcp::AllowlistManager& allowlist_;
  mcp::McpClient& mcp_client_;
  std::filesystem::path local_root_;
  std::optional<std::uintmax_t> local_size_;
};

}  // namespace cx::ui
