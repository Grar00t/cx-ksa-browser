#pragma once

#include "browser/bookmarks_dialog.h"
#include "browser/history_dialog.h"
#include "browser/navigation_controller.h"
#include "ui/agent_workspace.h"
#include "ui/context_graph.h"

#include <windows.h>
#include <commctrl.h>
#include <wrl.h>
#include <WebView2.h>

#include <string>

namespace cx::agent {
class AgentCore;
class PermissionManager;
}

namespace cx::ui {
class PermissionDialog;
class SettingsWindow;
}

namespace cx::mcp {
class AllowlistDialog;
class McpClient;
}

namespace cx::browser {
class BookmarkService;
class HistoryService;
class TabManager;
}

class AppWindow final : public cx::browser::NavigationSurface {
public:
  AppWindow(
      cx::agent::AgentCore& agent,
      cx::agent::PermissionManager& permissions,
      cx::ui::PermissionDialog& permission_dialog,
      cx::ui::SettingsWindow& settings_window,
      cx::mcp::AllowlistDialog& mcp_dialog,
      cx::mcp::McpClient& mcp_client,
      cx::browser::TabManager& tabs,
      cx::browser::NavigationController& navigation,
      cx::browser::HistoryService& history,
      cx::browser::BookmarkService& bookmarks);

  int Run(HINSTANCE instance, int show_command);

  bool NavigateTo(std::wstring_view url) override;
  bool ReloadPage() override;

private:
  static LRESULT CALLBACK WndProc(
      HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK ActivityShieldProc(
      HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK ContextGraphProc(
      HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  bool Create(HINSTANCE instance, int show_command);
  void CreateBrowserControls();
  void ShowToolsMenu();
  void CreateAgentWorkspaceControls();
  void HandleCommand(WORD command);
  void HandleNotify(const NMHDR* header);

  void InitializeWebView();
  void LayoutControls();
  void RefreshBrowserChrome();
  void RefreshTabs();
  void RefreshAddressBar();
  void RefreshAddressFromWebView();
  void SetBrowserStatus(std::wstring_view text);
  void SetAgentRunning(bool running);
  void RefreshAgentWorkspace();
  void RefreshContextGraph();
  void PaintContextGraph(HDC dc);
  int HitTestContextGraph(POINT point) const;
  void ActivateContextGraphNode(POINT point);
  void UpdateContextGraphHover(POINT point);
  void AddTooltip(HWND control, const wchar_t* text);

  void NavigateAddressBar();
  void NewTab();
  void CloseActiveTab();
  void ActivateSelectedTab();
  void BookmarkCurrent();
  void OpenLibraryUrl(std::string url);
  void HandleNavigationStarting(
      ICoreWebView2NavigationStartingEventArgs* args);
  void HandleNavigationCompleted(
      ICoreWebView2NavigationCompletedEventArgs* args);
  void HandleNewWindow(
      ICoreWebView2NewWindowRequestedEventArgs* args);

  cx::agent::AgentCore& agent_;
  cx::agent::PermissionManager& permissions_;
  cx::ui::PermissionDialog& permission_dialog_;
  cx::ui::SettingsWindow& settings_window_;
  cx::mcp::AllowlistDialog& mcp_dialog_;
  cx::mcp::McpClient& mcp_client_;
  cx::browser::TabManager& tabs_;
  cx::browser::NavigationController& navigation_;
  cx::browser::HistoryService& history_;
  cx::browser::BookmarkService& bookmarks_;

  cx::browser::HistoryDialog history_dialog_;
  cx::browser::BookmarksDialog bookmarks_dialog_;

  HWND hwnd_ = nullptr;
  HWND tab_strip_ = nullptr;
  HWND back_button_ = nullptr;
  HWND forward_button_ = nullptr;
  HWND reload_button_ = nullptr;
  HWND address_bar_ = nullptr;
  HWND go_button_ = nullptr;
  HWND bookmark_button_ = nullptr;
  HWND new_tab_button_ = nullptr;
  HWND close_tab_button_ = nullptr;
  HWND more_button_ = nullptr;
  HWND agent_toggle_button_ = nullptr;
  HWND graph_toggle_button_ = nullptr;
  HWND status_label_ = nullptr;

  HWND agent_panel_ = nullptr;
  HWND agent_title_ = nullptr;
  HWND agent_status_ = nullptr;
  HWND agent_scope_ = nullptr;
  HWND agent_log_ = nullptr;
  HWND agent_start_button_ = nullptr;
  HWND agent_microphone_button_ = nullptr;
  HWND context_graph_view_ = nullptr;
  HWND activity_shield_ = nullptr;
  HWND stop_strip_ = nullptr;
  HWND emergency_stop_button_ = nullptr;
  HWND tooltip_ = nullptr;

  cx::ui::AgentWorkspacePolicy workspace_policy_;
  bool agent_panel_visible_ = false;
  bool context_graph_visible_ = false;
  cx::ui::ContextGraph context_graph_;
  int context_graph_hover_ = -1;

  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
};
