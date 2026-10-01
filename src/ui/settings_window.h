#pragma once

#include "agent/permissions.h"
#include "ui/privacy_dashboard.h"

#include <windows.h>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace cx::agent {
class AgentCore;
}

namespace cx::browser {
class BookmarkService;
class HistoryService;
}

namespace cx::mcp {
class AllowlistDialog;
class AllowlistManager;
class McpClient;
}

namespace cx::storage {
class Database;
}

namespace cx::ui {

struct PrivacySettingSpec {
  std::string_view key;
  const wchar_t* label;
  bool default_value;
};

class SettingsWindow {
public:
  SettingsWindow(
      storage::Database& database,
      agent::PermissionManager& permissions,
      agent::AgentCore& agent,
      mcp::AllowlistManager& allowlist,
      mcp::AllowlistDialog& allowlist_dialog,
      mcp::McpClient& mcp_client,
      browser::HistoryService& history,
      browser::BookmarkService& bookmarks);

  void Show(HWND owner);
  void Refresh();

  static const std::array<PrivacySettingSpec, 3>&
      PrivacySettings();

private:
  static LRESULT CALLBACK WndProc(
      HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  void CreateControls();
  void CreatePrivacyPage();
  void CreatePermissionsPage();
  void CreateMcpPage();
  void CreateDataPage();
  void Layout();
  void ShowPage(int index);

  void HandleCommand(WORD command);
  void HandleNotify(const NMHDR* header);
  void HandlePrivacyToggle(WORD command);
  void HandlePermissionToggle(WORD command);

  bool ReadBool(
      std::string_view key,
      bool default_value) const;
  bool WriteBool(
      std::string_view key,
      bool value);

  void RefreshDashboard();
  void RefreshPrivacy();
  void RefreshPermissions();
  void RefreshMcp();
  void RefreshData();

  storage::Database& database_;
  agent::PermissionManager& permissions_;
  agent::AgentCore& agent_;
  mcp::AllowlistManager& allowlist_;
  mcp::AllowlistDialog& allowlist_dialog_;
  mcp::McpClient& mcp_client_;
  browser::HistoryService& history_;
  browser::BookmarkService& bookmarks_;
  PrivacyDashboard dashboard_;

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND tabs_ = nullptr;
  HWND dashboard_label_ = nullptr;
  HWND mcp_list_ = nullptr;
  HWND data_label_ = nullptr;

  std::array<std::vector<HWND>, 4> page_controls_;
  std::vector<std::pair<HWND, agent::Capability>>
      permission_controls_;
};

}  // namespace cx::ui
