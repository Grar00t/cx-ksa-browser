#include "browser/tab_manager.h"
#include "storage/database.h"

#include <windows.h>

#include <filesystem>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;

  cx::storage::Database database{
      std::filesystem::path(argv[1])};
  if (!database.Open()) return 3;

  cx::browser::TabManager tabs(database);
  if (!tabs.Restore()) return 4;

  const auto second = tabs.CreateTab(
      "https://crash.test/second", "Crash Second");
  const auto third = tabs.CreateTab(
      "https://crash.test/third", "Crash Third");
  if (!second.has_value() || !third.has_value()) return 5;

  if (!tabs.ActivateTab(*second)) return 6;

  ExitProcess(77);
}
