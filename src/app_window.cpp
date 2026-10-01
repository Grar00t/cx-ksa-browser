#include "app_window.h"

#include "agent/agent_core.h"
#include "agent/consent_dialog.h"
#include "agent/permissions.h"
#include "mcp/allowlist_dialog.h"
#include "mcp/mcp_client.h"

#include <WebView2EnvironmentOptions.h>

#include <cwchar>

using Microsoft::WRL::Callback;

namespace {
constexpr wchar_t kWindowClass[] = L"CXBuildWindowClass";
constexpr wchar_t kWindowTitle[] = L"CX Build - P05";
constexpr WORD kAgentStart = 40001;
constexpr WORD kAgentStop = 40002;
constexpr WORD kAgentRevokeAll = 40003;
constexpr WORD kMcpAllowlist = 40101;
}

AppWindow::AppWindow(
    cx::agent::AgentCore& agent,
    cx::agent::PermissionManager& permissions,
    cx::agent::ConsentDialog& consent_dialog,
    cx::mcp::AllowlistDialog& mcp_dialog,
    cx::mcp::McpClient& mcp_client)
    : agent_(agent),
      permissions_(permissions),
      consent_dialog_(consent_dialog),
      mcp_dialog_(mcp_dialog),
      mcp_client_(mcp_client) {}

int AppWindow::Run(HINSTANCE instance, int show_command) {
  const HRESULT com = OleInitialize(nullptr);
  if (FAILED(com)) {
    return 1;
  }

  if (!Create(instance, show_command)) {
    OleUninitialize();
    return 2;
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  webview_.Reset();
  controller_.Reset();
  OleUninitialize();
  return static_cast<int>(message.wParam);
}

bool AppWindow::Create(HINSTANCE instance, int) {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.hInstance = instance;
  window_class.lpfnWndProc = &AppWindow::WndProc;
  window_class.lpszClassName = kWindowClass;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

  if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  hwnd_ = CreateWindowExW(
      0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
      nullptr, nullptr, instance, this);
  if (!hwnd_) {
    return false;
  }
  CreateMenus();
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetWindowPos(hwnd_, nullptr, 0, 0, 1024, 768,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  UpdateWindow(hwnd_);
  InitializeWebView();
  return true;
}

void AppWindow::CreateMenus() {
  HMENU menu_bar = CreateMenu();
  HMENU agent_menu = CreatePopupMenu();
  HMENU mcp_menu = CreatePopupMenu();
  if (!menu_bar || !agent_menu || !mcp_menu) {
    if (agent_menu) {
      DestroyMenu(agent_menu);
    }
    if (mcp_menu) {
      DestroyMenu(mcp_menu);
    }
    if (menu_bar) {
      DestroyMenu(menu_bar);
    }
    return;
  }

  AppendMenuW(agent_menu, MF_STRING, kAgentStart, L"Start Agent...");
  AppendMenuW(agent_menu, MF_STRING, kAgentStop, L"Stop Agent");
  AppendMenuW(agent_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(
      agent_menu, MF_STRING, kAgentRevokeAll,
      L"Revoke All Permissions...");
  AppendMenuW(
      menu_bar, MF_POPUP,
      reinterpret_cast<UINT_PTR>(agent_menu), L"Agent");

  AppendMenuW(
      mcp_menu, MF_STRING, kMcpAllowlist,
      L"Allowed Servers...");
  AppendMenuW(
      menu_bar, MF_POPUP,
      reinterpret_cast<UINT_PTR>(mcp_menu), L"MCP");
  SetMenu(hwnd_, menu_bar);
}

void AppWindow::HandleCommand(WORD command) {
  if (command == kAgentStart) {
    if (agent_.Start(hwnd_)) {
      MessageBoxW(
          hwnd_, L"CX Agent is running.",
          L"CX Agent", MB_OK | MB_ICONINFORMATION);
    } else {
      MessageBoxW(
          hwnd_, L"CX Agent remains stopped.",
          L"CX Agent", MB_OK | MB_ICONINFORMATION);
    }
    return;
  }

  if (command == kAgentStop) {
    mcp_client_.Stop();
    agent_.Stop();
    MessageBoxW(
        hwnd_, L"CX Agent is stopped.",
        L"CX Agent", MB_OK | MB_ICONINFORMATION);
    return;
  }

  if (command == kAgentRevokeAll) {
    const auto granted = permissions_.Granted();
    if (granted.empty()) {
      MessageBoxW(
          hwnd_, L"No agent permissions are currently granted.",
          L"CX Agent Permissions", MB_OK | MB_ICONINFORMATION);
      return;
    }

    if (!consent_dialog_.ConfirmRevokeAll(hwnd_, granted.size())) {
      return;
    }

    mcp_client_.Stop();
    agent_.Stop();
    if (permissions_.RevokeAll()) {
      MessageBoxW(
          hwnd_, L"All agent permissions were revoked.",
          L"CX Agent Permissions", MB_OK | MB_ICONINFORMATION);
    } else {
      MessageBoxW(
          hwnd_, L"Permission revocation could not be saved.",
          L"CX Agent Permissions", MB_OK | MB_ICONERROR);
    }
    return;
  }

  if (command == kMcpAllowlist) {
    mcp_dialog_.Show(hwnd_);
  }
}

void AppWindow::InitializeWebView() {
  auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
  options->put_AdditionalBrowserArguments(
      L"--disable-background-networking --disable-component-update "
      L"--disable-sync --no-first-run --metrics-recording-only "
      L"--proxy-server=127.0.0.1:9 --proxy-bypass-list=<-loopback> "
      L"--host-resolver-rules=MAP * 0.0.0.0");

  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, nullptr, options.Get(),
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(result) || !environment) {
              MessageBoxW(hwnd_, L"WebView2 environment creation failed.", L"CX Build", MB_ICONERROR);
              return result;
            }
            return environment->CreateCoreWebView2Controller(
                hwnd_,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this](HRESULT controller_result,
                           ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(controller_result) || !controller) {
                        MessageBoxW(hwnd_, L"WebView2 controller creation failed.", L"CX Build", MB_ICONERROR);
                        return controller_result;
                      }

                      controller_ = controller;
                      controller_->get_CoreWebView2(&webview_);
                      ResizeWebView();

                      EventRegistrationToken navigation_token{};
                      webview_->add_NavigationStarting(
                          Callback<ICoreWebView2NavigationStartingEventHandler>(
                              [](ICoreWebView2*,
                                 ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                LPWSTR uri = nullptr;
                                if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                  const bool network =
                                      wcsncmp(uri, L"http://", 7) == 0 ||
                                      wcsncmp(uri, L"https://", 8) == 0;
                                  CoTaskMemFree(uri);
                                  if (network) {
                                    args->put_Cancel(TRUE);
                                  }
                                }
                                return S_OK;
                              }).Get(),
                          &navigation_token);

                      EventRegistrationToken new_window_token{};
                      webview_->add_NewWindowRequested(
                          Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                              [](ICoreWebView2*,
                                 ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                args->put_Handled(TRUE);
                                return S_OK;
                              }).Get(),
                          &new_window_token);

                      return webview_->Navigate(L"about:blank");
                    }).Get());
          }).Get());

  if (FAILED(hr)) {
    MessageBoxW(hwnd_, L"WebView2 initialization call failed.", L"CX Build", MB_ICONERROR);
  }
}

void AppWindow::ResizeWebView() {
  if (!controller_ || !hwnd_) {
    return;
  }
  RECT bounds{};
  GetClientRect(hwnd_, &bounds);
  controller_->put_Bounds(bounds);
}

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  static AppWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<AppWindow*>(create->lpCreateParams);
    if (self) self->hwnd_ = hwnd;
  }
  if (self && message == WM_SIZE) {
    self->ResizeWebView();
    return 0;
  }
  if (self && message == WM_COMMAND) {
    self->HandleCommand(LOWORD(wparam));
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
