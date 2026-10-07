#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace cx::mcp {
class AllowlistManager;
class McpClient;

class AllowlistDialog {
public:
  AllowlistDialog(
      AllowlistManager& allowlist,
      McpClient& client);

  void Show(HWND owner);

private:
  static LRESULT CALLBACK WndProc(
      HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  void CreateControls();
  void Refresh();
  void HandleCommand(WORD command);
  void AddServer();
  void RemoveSelected();
  void ConnectSelected();
  std::optional<std::string> SelectedServerId() const;

  AllowlistManager& allowlist_;
  McpClient& client_;

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND list_ = nullptr;
  HWND status_ = nullptr;
};

}  // namespace cx::mcp
