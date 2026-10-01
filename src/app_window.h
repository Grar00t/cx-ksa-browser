#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

namespace cx::agent {
class AgentCore;
class ConsentDialog;
class PermissionManager;
}

class AppWindow {
public:
  AppWindow(cx::agent::AgentCore& agent,
            cx::agent::PermissionManager& permissions,
            cx::agent::ConsentDialog& consent_dialog);

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

  HWND hwnd_ = nullptr;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
};
