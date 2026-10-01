#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

namespace cx::agent {
class AgentCore;
class ConsentDialog;
class PermissionManager;
}

namespace cx::mcp {
class AllowlistDialog;
class McpClient;
}

class AppWindow {
public:
  AppWindow(cx::agent::AgentCore& agent,
            cx::agent::PermissionManager& permissions,
            cx::agent::ConsentDialog& consent_dialog,
            cx::mcp::AllowlistDialog& mcp_dialog,
            cx::mcp::McpClient& mcp_client);

  int Run(HINSTANCE instance, int show_command);

private:
  static LRESULT CALLBACK WndProc(
      HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  bool Create(HINSTANCE instance, int show_command);
  void CreateMenus();
  void HandleCommand(WORD command);
  void InitializeWebView();
  void ResizeWebView();

  cx::agent::AgentCore& agent_;
  cx::agent::PermissionManager& permissions_;
  cx::agent::ConsentDialog& consent_dialog_;
  cx::mcp::AllowlistDialog& mcp_dialog_;
  cx::mcp::McpClient& mcp_client_;

  HWND hwnd_ = nullptr;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
};
