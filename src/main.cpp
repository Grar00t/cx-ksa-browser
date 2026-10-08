#include "agent/agent_core.h"
#include "agent/permissions.h"
#include "ui/athar_sound.h"
#include "ui/permission_dialog.h"
#include "ui/settings_window.h"
#include "app_window.h"
#include "browser/bookmark_service.h"
#include "browser/history_service.h"
#include "browser/navigation_controller.h"
#include "browser/tab_manager.h"
#include "mcp/allowlist_dialog.h"
#include "mcp/allowlist_manager.h"
#include "mcp/mcp_client.h"
#include "mcp/rate_limiter.h"
#include "storage/database.h"

#include <windows.h>

int WINAPI wWinMain(
    HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  cx::storage::Database database;
  if (!database.Open()) {
    MessageBoxW(
        nullptr, L"Failed to open local CX database.",
        L"CX Build", MB_ICONERROR);
    return 3;
  }

  cx::browser::TabManager tabs(database);
  if (!tabs.Restore()) {
    MessageBoxW(
        nullptr, L"Failed to restore local browser session.",
        L"CX Build", MB_ICONERROR);
    return 4;
  }
  cx::browser::HistoryService history(database);
  cx::browser::BookmarkService bookmarks(database);
  cx::browser::NavigationController navigation(tabs, history);

  cx::agent::LocalLogger agent_logger;
  if (!agent_logger.Open()) {
    MessageBoxW(
        nullptr, L"Failed to open local CX agent log.",
        L"CX Build", MB_ICONERROR);
    return 5;
  }

  const auto mcp_log_path =
      cx::agent::LocalLogger::DefaultPath().parent_path() /
      L"mcp.log";
  cx::agent::LocalLogger mcp_logger(mcp_log_path);
  if (!mcp_logger.Open()) {
    MessageBoxW(
        nullptr, L"Failed to open local CX MCP log.",
        L"CX Build", MB_ICONERROR);
    return 6;
  }

  cx::ui::PermissionDialog permission_dialog;
  cx::agent::PermissionManager permissions(
      database, permission_dialog);
  cx::agent::AgentCore agent(permissions, agent_logger);

  cx::mcp::AllowlistManager mcp_allowlist;
  if (!mcp_allowlist.Load()) {
    mcp_logger.Log(
        "mcp_allowlist_load_failed",
        "fail-closed: no servers loaded");
  }

  cx::mcp::RateLimiter mcp_rate_limiter(10);
  cx::mcp::McpClient mcp_client(
      agent, mcp_allowlist, mcp_rate_limiter, mcp_logger);
  cx::mcp::AllowlistDialog mcp_dialog(
      mcp_allowlist, mcp_client);

  cx::ui::SettingsWindow settings_window(
      database, permissions, agent,
      mcp_allowlist, mcp_dialog, mcp_client,
      history, bookmarks);

  const auto athar_enabled =
      database.GetSetting(cx::ui::kAtharStartupSetting);
  if (!athar_enabled.has_value() ||
      *athar_enabled == "1") {
    cx::ui::AtharSound::Instance().Play();
  }

  AppWindow app(
      agent, permissions,
      permission_dialog, settings_window,
      mcp_dialog, mcp_client,
      tabs, navigation, history, bookmarks);

  const int result = app.Run(instance, show_command);

  const auto clear_history =
      database.GetSetting("privacy.clear_history_on_exit");
  if (!clear_history.has_value() ||
      *clear_history == "1") {
    history.Clear();
  }
  return result;
}
