#include "agent/agent_core.h"
#include "agent/consent_dialog.h"
#include "agent/permissions.h"
#include "app_window.h"
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

  cx::agent::LocalLogger agent_logger;
  if (!agent_logger.Open()) {
    MessageBoxW(
        nullptr, L"Failed to open local CX agent log.",
        L"CX Build", MB_ICONERROR);
    return 4;
  }

  const auto mcp_log_path =
      cx::agent::LocalLogger::DefaultPath().parent_path() /
      L"mcp.log";
  cx::agent::LocalLogger mcp_logger(mcp_log_path);
  if (!mcp_logger.Open()) {
    MessageBoxW(
        nullptr, L"Failed to open local CX MCP log.",
        L"CX Build", MB_ICONERROR);
    return 5;
  }

  cx::agent::ConsentDialog consent_dialog;
  cx::agent::PermissionManager permissions(
      database, consent_dialog);
  cx::agent::AgentCore agent(permissions, agent_logger);

  cx::mcp::AllowlistManager mcp_allowlist;
  if (!mcp_allowlist.Load()) {
    mcp_logger.Log(
        "mcp_allowlist_load_failed",
        "fail-closed: no servers loaded");
    MessageBoxW(
        nullptr,
        L"MCP allowlist is invalid. MCP starts fail-closed until "
        L"you replace it from MCP > Allowed Servers.",
        L"CX MCP", MB_OK | MB_ICONWARNING);
  }

  cx::mcp::RateLimiter mcp_rate_limiter(10);
  cx::mcp::McpClient mcp_client(
      agent, mcp_allowlist, mcp_rate_limiter, mcp_logger);
  cx::mcp::AllowlistDialog mcp_dialog(
      mcp_allowlist, mcp_client);

  AppWindow app(
      agent, permissions, consent_dialog, mcp_dialog, mcp_client);
  return app.Run(instance, show_command);
}
