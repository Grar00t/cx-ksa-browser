#include "app_window.h"
#include "storage/database.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  cx::storage::Database database;
  if (!database.Open()) {
    MessageBoxW(nullptr, L"Failed to open local CX database.",
                L"CX Build", MB_ICONERROR);
    return 3;
  }

  AppWindow app;
  return app.Run(instance, show_command);
}
