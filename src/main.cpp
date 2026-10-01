#include "agent/agent_core.h"
#include "agent/consent_dialog.h"
#include "agent/permissions.h"
#include "app_window.h"
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

  cx::agent::LocalLogger logger;
  if (!logger.Open()) {
    MessageBoxW(
        nullptr, L"Failed to open local CX agent log.",
        L"CX Build", MB_ICONERROR);
    return 4;
  }

  cx::agent::ConsentDialog consent_dialog;
  cx::agent::PermissionManager permissions(
      database, consent_dialog);
  cx::agent::AgentCore agent(permissions, logger);

  AppWindow app(agent, permissions, consent_dialog);
  return app.Run(instance, show_command);
}
