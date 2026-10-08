#include "app_window.h"

#include "agent/agent_core.h"
#include "agent/permissions.h"
#include "ui/permission_dialog.h"
#include "ui/settings_window.h"
#include "ui/najdi_theme.h"
#include "browser/bookmark_service.h"
#include "browser/history_service.h"
#include "browser/tab_manager.h"
#include "localization/strings.h"
#include "mcp/allowlist_dialog.h"
#include "mcp/mcp_client.h"

#include <WebView2EnvironmentOptions.h>
#include <commctrl.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

using Microsoft::WRL::Callback;
namespace design = cx::ui::design;

namespace {
constexpr wchar_t kWindowClass[] = L"CXBuildWindowClass";
constexpr wchar_t kActivityShieldClass[] =
    L"CXBuildAgentActivityShield";
constexpr wchar_t kWindowTitle[] = L"CX Build";

constexpr WORD kAgentStart = 40001;
constexpr WORD kAgentStop = 40002;
constexpr WORD kAgentRevokeAll = 40003;
constexpr WORD kMcpAllowlist = 40101;

constexpr WORD kBack = 41001;
constexpr WORD kForward = 41002;
constexpr WORD kReload = 41003;
constexpr WORD kGo = 41004;
constexpr WORD kBookmark = 41005;
constexpr WORD kNewTab = 41006;
constexpr WORD kCloseTab = 41007;
constexpr WORD kHistory = 41008;
constexpr WORD kBookmarks = 41009;
constexpr WORD kSettings = 41010;
constexpr WORD kToggleAgentPanel = 41011;

constexpr UINT_PTR kAddressSubclassId = 0x43584144;
constexpr std::size_t kMaxVisibleTabTitle = 28;

std::wstring DisplayTabTitle(
    std::wstring_view base,
    std::wstring_view suffix) {
  std::wstring title =
      base.empty() ? std::wstring(L"New Tab") : std::wstring(base);
  const std::size_t reserved = suffix.size();
  if (title.size() + reserved > kMaxVisibleTabTitle) {
    const std::size_t room =
        kMaxVisibleTabTitle > reserved
            ? kMaxVisibleTabTitle - reserved
            : 0;
    if (room > 1) {
      title.resize(room - 1);
      title += L"\x2026";
    } else {
      title.clear();
    }
  }
  title += suffix;
  return title;
}

LRESULT CALLBACK AddressBarSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR,
    DWORD_PTR) {
  if (message == WM_SETFOCUS) {
    const LRESULT result =
        DefSubclassProc(hwnd, message, wparam, lparam);
    SendMessageW(hwnd, EM_SETSEL, 0, -1);
    return result;
  }
  if (message == WM_KEYDOWN && wparam == VK_RETURN) {
    SendMessageW(
        GetParent(hwnd),
        WM_COMMAND,
        MAKEWPARAM(kGo, BN_CLICKED),
        reinterpret_cast<LPARAM>(hwnd));
    return 0;
  }
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(
        hwnd, AddressBarSubclassProc, kAddressSubclassId);
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string output(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), output.data(), size,
          nullptr, nullptr) != size) {
    return {};
  }
  return output;
}

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring output(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), output.data(), size) != size) {
    return {};
  }
  return output;
}
}  // namespace

AppWindow::AppWindow(
    cx::agent::AgentCore& agent,
    cx::agent::PermissionManager& permissions,
    cx::ui::PermissionDialog& permission_dialog,
    cx::ui::SettingsWindow& settings_window,
    cx::mcp::AllowlistDialog& mcp_dialog,
    cx::mcp::McpClient& mcp_client,
    cx::browser::TabManager& tabs,
    cx::browser::NavigationController& navigation,
    cx::browser::HistoryService& history,
    cx::browser::BookmarkService& bookmarks)
    : agent_(agent),
      permissions_(permissions),
      permission_dialog_(permission_dialog),
      settings_window_(settings_window),
      mcp_dialog_(mcp_dialog),
      mcp_client_(mcp_client),
      tabs_(tabs),
      navigation_(navigation),
      history_(history),
      bookmarks_(bookmarks),
      history_dialog_(
          history_,
          [this](std::string url) { OpenLibraryUrl(std::move(url)); }),
      bookmarks_dialog_(
          bookmarks_,
          [this](std::string url) { OpenLibraryUrl(std::move(url)); }) {
  navigation_.AttachSurface(this);
}

int AppWindow::Run(HINSTANCE instance, int show_command) {
  const HRESULT com = OleInitialize(nullptr);
  if (FAILED(com)) return 1;

  if (!Create(instance, show_command)) {
    OleUninitialize();
    return 2;
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    const HWND root = message.hwnd
        ? GetAncestor(message.hwnd, GA_ROOT)
        : nullptr;
    if (root && IsDialogMessageW(root, &message)) {
      continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  webview_.Reset();
  controller_.Reset();
  OleUninitialize();
  return static_cast<int>(message.wParam);
}

bool AppWindow::Create(HINSTANCE instance, int) {
  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_TAB_CLASSES | ICC_WIN95_CLASSES;
  InitCommonControlsEx(&controls);

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.hInstance = instance;
  window_class.lpfnWndProc = &AppWindow::WndProc;
  window_class.lpszClassName = kWindowClass;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground =
      cx::ui::theme::BackgroundBrush();

  if (!RegisterClassExW(&window_class) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  WNDCLASSEXW shield_class{};
  shield_class.cbSize = sizeof(shield_class);
  shield_class.hInstance = instance;
  shield_class.lpfnWndProc = &AppWindow::ActivityShieldProc;
  shield_class.lpszClassName = kActivityShieldClass;
  shield_class.hCursor = LoadCursorW(nullptr, IDC_NO);
  if (!RegisterClassExW(&shield_class) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  hwnd_ = CreateWindowExW(
      0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT,
      design::Window::AppWidth, design::Window::AppHeight,
      nullptr, nullptr, instance, this);
  if (!hwnd_) return false;

  cx::ui::theme::ApplyWindowChrome(hwnd_);
  CreateMenus();
  CreateBrowserControls();
  RefreshBrowserChrome();

  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetWindowPos(
      hwnd_, nullptr, 0, 0,
      design::Window::AppWidth, design::Window::AppHeight,
      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  UpdateWindow(hwnd_);
  InitializeWebView();
  return true;
}

void AppWindow::CreateMenus() {
  HMENU menu_bar = CreateMenu();
  HMENU browser_menu = CreatePopupMenu();
  HMENU agent_menu = CreatePopupMenu();
  HMENU mcp_menu = CreatePopupMenu();
  if (!menu_bar || !browser_menu || !agent_menu || !mcp_menu) {
    if (browser_menu) DestroyMenu(browser_menu);
    if (agent_menu) DestroyMenu(agent_menu);
    if (mcp_menu) DestroyMenu(mcp_menu);
    if (menu_bar) DestroyMenu(menu_bar);
    return;
  }

  AppendMenuW(
      browser_menu, MF_STRING, kNewTab,
      cx::localization::Text(
          cx::localization::StringId::NewTab).data());
  AppendMenuW(
      browser_menu, MF_STRING, kCloseTab,
      cx::localization::Text(
          cx::localization::StringId::CloseTab).data());
  AppendMenuW(browser_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(browser_menu, MF_STRING, kHistory, L"History...");
  AppendMenuW(browser_menu, MF_STRING, kBookmarks, L"Bookmarks...");
  AppendMenuW(browser_menu, MF_SEPARATOR, 0, nullptr);
  std::wstring settings_text(
      cx::localization::Text(
          cx::localization::StringId::SettingsAndPrivacy));
  settings_text += L"...";
  AppendMenuW(
      browser_menu, MF_STRING, kSettings,
      settings_text.c_str());
  AppendMenuW(
      menu_bar, MF_POPUP,
      reinterpret_cast<UINT_PTR>(browser_menu),
      cx::localization::Text(
          cx::localization::StringId::Browser).data());

  AppendMenuW(agent_menu, MF_STRING, kAgentStart, L"Start Agent...");
  AppendMenuW(agent_menu, MF_STRING, kAgentStop, L"Stop Agent");
  AppendMenuW(agent_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(
      agent_menu, MF_STRING, kAgentRevokeAll,
      L"Revoke All Permissions...");
  AppendMenuW(
      menu_bar, MF_POPUP,
      reinterpret_cast<UINT_PTR>(agent_menu),
      cx::localization::Text(
          cx::localization::StringId::Agent).data());

  AppendMenuW(
      mcp_menu, MF_STRING, kMcpAllowlist,
      L"Allowed Servers...");
  AppendMenuW(
      menu_bar, MF_POPUP,
      reinterpret_cast<UINT_PTR>(mcp_menu),
      cx::localization::Text(
          cx::localization::StringId::Mcp).data());

  SetMenu(hwnd_, menu_bar);
}

void AppWindow::CreateBrowserControls() {
  tab_strip_ = CreateWindowExW(
      0, WC_TABCONTROLW, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
          TCS_TABS | TCS_SINGLELINE |
          TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH,
      0, 0,
      design::Density::MinimumInputWidth,
      design::Density::TabHeight,
      hwnd_, nullptr, nullptr, nullptr);
  cx::ui::theme::StyleTabControl(tab_strip_);

  back_button_ = CreateWindowExW(
      0, L"BUTTON", L"\x2190",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::IconButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kBack), nullptr, nullptr);
  forward_button_ = CreateWindowExW(
      0, L"BUTTON", L"\x2192",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::IconButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kForward), nullptr, nullptr);
  reload_button_ = CreateWindowExW(
      0, L"BUTTON", L"\x21BB",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::IconButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kReload), nullptr, nullptr);
  address_bar_ = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
      0, 0,
      design::Density::MinimumInputWidth,
      design::Density::ControlHeight,
      hwnd_, nullptr, nullptr, nullptr);
  cx::ui::theme::StyleBorderedSurface(address_bar_);
  SetWindowSubclass(
      address_bar_,
      AddressBarSubclassProc,
      kAddressSubclassId,
      0);
  go_button_ = CreateWindowExW(
      0, L"BUTTON", L"\x21B5",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::IconButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kGo), nullptr, nullptr);
  cx::ui::theme::MarkPrimaryAction(go_button_);
  bookmark_button_ = CreateWindowExW(
      0, L"BUTTON", L"\x2605",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::IconButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kBookmark), nullptr, nullptr);
  new_tab_button_ = CreateWindowExW(
      0, L"BUTTON", L"+",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::IconButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kNewTab), nullptr, nullptr);
  close_tab_button_ = CreateWindowExW(
      0, L"BUTTON", L"\x00D7",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::IconButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kCloseTab), nullptr, nullptr);
  agent_toggle_button_ = CreateWindowExW(
      0, L"BUTTON", L"Agent",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      68, design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kToggleAgentPanel),
      nullptr, nullptr);

  status_label_ = CreateWindowExW(
      0, L"STATIC", L"Ready",
      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
      0, 0, 100, design::Density::StatusHeight,
      hwnd_, nullptr, nullptr, nullptr);

  CreateAgentWorkspaceControls();

  tooltip_ = CreateWindowExW(
      WS_EX_TOPMOST,
      TOOLTIPS_CLASSW,
      nullptr,
      WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
      CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT,
      hwnd_, nullptr, nullptr, nullptr);
  if (tooltip_) {
    SetWindowPos(
        tooltip_, HWND_TOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    AddTooltip(
        back_button_, cx::localization::Text(
            cx::localization::StringId::Back).data());
    AddTooltip(
        forward_button_, cx::localization::Text(
            cx::localization::StringId::Forward).data());
    AddTooltip(
        reload_button_, cx::localization::Text(
            cx::localization::StringId::Reload).data());
    AddTooltip(
        go_button_, cx::localization::Text(
            cx::localization::StringId::Navigate).data());
    AddTooltip(
        bookmark_button_, cx::localization::Text(
            cx::localization::StringId::SaveBookmarkLocally).data());
    AddTooltip(
        new_tab_button_, cx::localization::Text(
            cx::localization::StringId::NewTab).data());
    AddTooltip(
        close_tab_button_, cx::localization::Text(
            cx::localization::StringId::CloseTab).data());
    AddTooltip(agent_toggle_button_, L"Show or hide the agent workspace");
    AddTooltip(
        agent_microphone_button_,
        L"Voice input is unavailable in this build");
  }

  SendMessageW(
      address_bar_,
      EM_SETMARGINS,
      EC_LEFTMARGIN | EC_RIGHTMARGIN,
      MAKELPARAM(
          design::Density::InputTextInset,
          design::Density::InputTextInset));
  cx::ui::theme::ApplyFontToChildren(hwnd_);
  LayoutControls();
}

void AppWindow::CreateAgentWorkspaceControls() {
  agent_panel_ = CreateWindowExW(
      0, L"STATIC", L"",
      WS_CHILD | WS_VISIBLE,
      0, 0, design::AgentWorkspaceLayout::PanelWidth, 200,
      hwnd_, nullptr, nullptr, nullptr);
  cx::ui::theme::StyleBorderedSurface(agent_panel_);

  agent_title_ = CreateWindowExW(
      0, L"STATIC", L"CX Agent",
      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
      0, 0, 100, design::AgentWorkspaceLayout::HeaderHeight,
      hwnd_, nullptr, nullptr, nullptr);
  agent_status_ = CreateWindowExW(
      0, L"STATIC", L"Stopped",
      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
      0, 0, 100, design::Density::StatusHeight,
      hwnd_, nullptr, nullptr, nullptr);
  agent_scope_ = CreateWindowExW(
      0, L"STATIC",
      L"Scope: explicit permission grants only",
      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
      0, 0, 100, design::Density::StatusHeight,
      hwnd_, nullptr, nullptr, nullptr);
  agent_log_ = CreateWindowExW(
      0, L"LISTBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL |
          LBS_NOINTEGRALHEIGHT | LBS_NOSEL,
      0, 0, 100, design::AgentWorkspaceLayout::LogMinimumHeight,
      hwnd_, nullptr, nullptr, nullptr);
  cx::ui::theme::StyleBorderedSurface(agent_log_);
  SendMessageW(
      agent_log_, LB_ADDSTRING, 0,
      reinterpret_cast<LPARAM>(
          L"Ready. Page input is available."));

  agent_start_button_ = CreateWindowExW(
      0, L"BUTTON", L"Start agent",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0, 100, design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kAgentStart), nullptr, nullptr);
  cx::ui::theme::MarkPrimaryAction(agent_start_button_);
  agent_microphone_button_ = CreateWindowExW(
      0, L"BUTTON", L"Mic",
      WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
      0, 0, 56, design::Density::ControlHeight,
      hwnd_, nullptr, nullptr, nullptr);
  EnableWindow(agent_microphone_button_, FALSE);

  activity_shield_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_NOACTIVATE,
      kActivityShieldClass, L"",
      WS_CHILD | WS_CLIPSIBLINGS,
      0, 0, 100, 100,
      hwnd_, nullptr, GetModuleHandleW(nullptr), this);
  if (activity_shield_) {
    SetLayeredWindowAttributes(
        activity_shield_, 0,
        design::AgentWorkspaceLayout::ShieldAlpha,
        LWA_ALPHA);
  }

  stop_strip_ = CreateWindowExW(
      0, L"STATIC",
      L"Agent active - pointer input is locked",
      WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
      0, 0, 100, design::AgentWorkspaceLayout::StopStripHeight,
      hwnd_, nullptr, nullptr, nullptr);
  emergency_stop_button_ = CreateWindowExW(
      0, L"BUTTON", L"Stop agent",
      WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
      0, 0, 120, design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kAgentStop), nullptr, nullptr);
  cx::ui::theme::MarkPrimaryAction(emergency_stop_button_);
}

void AppWindow::HandleCommand(WORD command) {
  switch (command) {
    case kBack:
      navigation_.Back();
      RefreshBrowserChrome();
      return;
    case kForward:
      navigation_.Forward();
      RefreshBrowserChrome();
      return;
    case kReload:
      if (navigation_.Reload()) {
        SetBrowserStatus(L"Loading...");
      } else {
        SetBrowserStatus(L"Reload unavailable");
      }
      return;
    case kGo:
      NavigateAddressBar();
      return;
    case kBookmark:
      BookmarkCurrent();
      return;
    case kNewTab:
      NewTab();
      return;
    case kCloseTab:
      CloseActiveTab();
      return;
    case kHistory:
      history_dialog_.Show(hwnd_);
      return;
    case kBookmarks:
      bookmarks_dialog_.Show(hwnd_);
      return;
    case kSettings:
      settings_window_.Show(hwnd_);
      return;
    case kToggleAgentPanel:
      agent_panel_visible_ = !agent_panel_visible_;
      LayoutControls();
      return;
    case kMcpAllowlist:
      mcp_dialog_.Show(hwnd_);
      return;
    default:
      break;
  }

  if (command == kAgentStart) {
    if (agent_.Start(hwnd_)) {
      SetAgentRunning(true);
    } else {
      SetBrowserStatus(L"Agent start denied or unavailable");
      SendMessageW(
          agent_log_, LB_ADDSTRING, 0,
          reinterpret_cast<LPARAM>(
              L"Start denied. Review explicit permissions."));
    }
    return;
  }

  if (command == kAgentStop) {
    mcp_client_.Stop();
    agent_.Stop();
    SetAgentRunning(false);
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

    if (!permission_dialog_.ConfirmRevokeAll(hwnd_, granted.size())) {
      return;
    }

    mcp_client_.Stop();
    agent_.Stop();
    SetAgentRunning(false);
    if (permissions_.RevokeAll()) {
      MessageBoxW(
          hwnd_, L"All agent permissions were revoked.",
          L"CX Agent Permissions", MB_OK | MB_ICONINFORMATION);
    } else {
      MessageBoxW(
          hwnd_, L"Permission revocation could not be saved.",
          L"CX Agent Permissions", MB_OK | MB_ICONERROR);
    }
  }
}

void AppWindow::HandleNotify(const NMHDR* header) {
  if (!header) return;
  if (header->hwndFrom == tab_strip_ &&
      header->code == TCN_SELCHANGE) {
    ActivateSelectedTab();
    InvalidateRect(tab_strip_, nullptr, TRUE);
  }
}

void AppWindow::LayoutControls() {
  if (!hwnd_) return;

  RECT client{};
  GetClientRect(hwnd_, &client);
  const int width = (std::max)(0, client.right - client.left);
  const int height = (std::max)(0, client.bottom - client.top);
  const int tab_height = design::Density::TabHeight;
  const int toolbar_y = tab_height + design::Spacing::Xxs;
  const int control_height = design::Density::ControlHeight;
  const int gap = design::Spacing::Sm;
  const int button_width = design::Density::IconButtonWidth;
  const int status_y =
      toolbar_y + control_height + design::Spacing::Xxs;
  const int content_y =
      status_y + design::Density::StatusHeight +
      design::Spacing::Xxs;
  const int stop_height = workspace_policy_.emergency_stop_visible()
      ? design::AgentWorkspaceLayout::StopStripHeight
      : 0;
  const int content_bottom = (std::max)(content_y, height - stop_height);

  MoveWindow(tab_strip_, 0, 0, width, tab_height, TRUE);

  int x = design::Spacing::Sm;
  for (HWND button : {back_button_, forward_button_, reload_button_}) {
    MoveWindow(button, x, toolbar_y, button_width, control_height, TRUE);
    x += button_width + gap;
  }

  const int agent_button_width = 68;
  const int right_fixed =
      (4 * button_width) + agent_button_width +
      (4 * gap) + design::Spacing::Lg;
  const int requested_address_width = width - x - right_fixed;
  const int address_width = (std::max)(
      requested_address_width,
      design::Density::MinimumInputWidth);
  MoveWindow(
      address_bar_, x, toolbar_y,
      address_width, control_height, TRUE);
  x += address_width + gap;

  for (HWND button : {
           go_button_, bookmark_button_,
           new_tab_button_, close_tab_button_}) {
    MoveWindow(button, x, toolbar_y, button_width, control_height, TRUE);
    x += button_width + gap;
  }
  MoveWindow(
      agent_toggle_button_, x, toolbar_y,
      agent_button_width, control_height, TRUE);

  MoveWindow(
      status_label_, design::Spacing::Md, status_y,
      (std::max)(0, width - 2 * design::Spacing::Md),
      design::Density::StatusHeight, TRUE);

  const int panel_width = agent_panel_visible_
      ? (std::min)(
            design::AgentWorkspaceLayout::PanelWidth,
            (std::max)(0, width -
                design::AgentWorkspaceLayout::MinimumBrowserWidth))
      : 0;
  const int panel_height = (std::max)(0, content_bottom - content_y);
  const int browser_x = panel_width > 0
      ? panel_width + design::Spacing::Sm
      : 0;
  const int browser_width = (std::max)(0, width - browser_x);

  for (HWND control : {
           agent_panel_, agent_title_, agent_status_, agent_scope_,
           agent_log_, agent_start_button_, agent_microphone_button_}) {
    ShowWindow(
        control,
        panel_width > 0 ? SW_SHOWNOACTIVATE : SW_HIDE);
  }
  if (panel_width > 0) {
    const int inset = design::AgentWorkspaceLayout::PanelInset;
    const int inner_width = (std::max)(0, panel_width - 2 * inset);
    MoveWindow(agent_panel_, 0, content_y, panel_width, panel_height, TRUE);
    int panel_y = content_y + inset;
    MoveWindow(
        agent_title_, inset, panel_y, inner_width,
        design::AgentWorkspaceLayout::HeaderHeight, TRUE);
    panel_y += design::AgentWorkspaceLayout::HeaderHeight;
    MoveWindow(
        agent_status_, inset, panel_y, inner_width,
        design::Density::StatusHeight, TRUE);
    panel_y += design::Density::StatusHeight;
    MoveWindow(
        agent_scope_, inset, panel_y, inner_width,
        design::Density::StatusHeight, TRUE);
    panel_y += design::Density::StatusHeight + design::Spacing::Md;
    const int action_y =
        content_bottom - inset - design::Density::ControlHeight;
    const int log_height = (std::max)(
        design::AgentWorkspaceLayout::LogMinimumHeight,
        action_y - panel_y - design::Spacing::Md);
    MoveWindow(
        agent_log_, inset, panel_y, inner_width, log_height, TRUE);
    const int mic_width = 56;
    MoveWindow(
        agent_start_button_, inset, action_y,
        (std::max)(0, inner_width - mic_width - gap),
        design::Density::ControlHeight, TRUE);
    MoveWindow(
        agent_microphone_button_,
        panel_width - inset - mic_width, action_y,
        mic_width, design::Density::ControlHeight, TRUE);
  }

  if (controller_) {
    RECT bounds{browser_x, content_y, width, content_bottom};
    controller_->put_Bounds(bounds);
  }

  if (workspace_policy_.shield_visible()) {
    MoveWindow(activity_shield_, 0, 0, width, content_bottom, TRUE);
    ShowWindow(activity_shield_, SW_SHOWNOACTIVATE);
    SetWindowPos(
        activity_shield_, HWND_TOP, 0, 0, width, content_bottom,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else {
    ShowWindow(activity_shield_, SW_HIDE);
  }

  if (workspace_policy_.emergency_stop_visible()) {
    const int strip_y = content_bottom;
    MoveWindow(stop_strip_, 0, strip_y, width, stop_height, TRUE);
    const int stop_width = 132;
    const int stop_x =
        (std::max)(design::Spacing::Md, width - stop_width -
            design::Spacing::Md);
    MoveWindow(
        emergency_stop_button_, stop_x,
        strip_y + design::Spacing::Md,
        stop_width, design::Density::ControlHeight, TRUE);
    ShowWindow(stop_strip_, SW_SHOWNOACTIVATE);
    ShowWindow(emergency_stop_button_, SW_SHOWNOACTIVATE);
    SetWindowPos(
        stop_strip_, HWND_TOP, 0, strip_y, width, stop_height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(
        emergency_stop_button_, HWND_TOP, stop_x,
        strip_y + design::Spacing::Md,
        stop_width, design::Density::ControlHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else {
    ShowWindow(stop_strip_, SW_HIDE);
    ShowWindow(emergency_stop_button_, SW_HIDE);
  }
}

void AppWindow::SetAgentRunning(bool running) {
  workspace_policy_.SetAgentRunning(running);
  RefreshAgentWorkspace();
  LayoutControls();
  if (running && emergency_stop_button_) {
    SetFocus(emergency_stop_button_);
  }
}

void AppWindow::RefreshAgentWorkspace() {
  const bool running = workspace_policy_.agent_running();
  SetWindowTextW(agent_status_, running
      ? L"Running - pointer input locked"
      : L"Stopped - page input available");
  SetWindowTextW(
      agent_start_button_,
      running ? L"Agent running" : L"Start agent");
  EnableWindow(agent_start_button_, running ? FALSE : TRUE);
  SetBrowserStatus(running
      ? L"Agent active - use the bottom control to stop"
      : L"Agent stopped");
  SendMessageW(
      agent_log_, LB_ADDSTRING, 0,
      reinterpret_cast<LPARAM>(running
          ? L"Agent started. All pointer targets are shielded."
          : L"Agent stopped. Pointer input restored."));
  const LRESULT count = SendMessageW(agent_log_, LB_GETCOUNT, 0, 0);
  if (count > 0) {
    SendMessageW(agent_log_, LB_SETTOPINDEX, count - 1, 0);
  }
}

void AppWindow::RefreshBrowserChrome() {
  RefreshTabs();
  RefreshAddressBar();

  EnableWindow(
      back_button_, navigation_.CanGoBack() ? TRUE : FALSE);
  EnableWindow(
      forward_button_, navigation_.CanGoForward() ? TRUE : FALSE);
}

void AppWindow::RefreshTabs() {
  if (!tab_strip_) return;

  std::unordered_map<std::wstring, int> totals;
  for (const auto& tab : tabs_.tabs()) {
    const bool default_title =
        tab.title.empty() || tab.title == "New Tab";
    const std::wstring base = default_title
        ? std::wstring(cx::localization::Text(
              cx::localization::StringId::NewTab))
        : Utf8ToWide(tab.title);
    ++totals[base.empty()
        ? std::wstring(cx::localization::Text(
              cx::localization::StringId::NewTab))
        : base];
  }

  std::unordered_map<std::wstring, int> seen;
  TabCtrl_DeleteAllItems(tab_strip_);
  int selected = -1;
  int index = 0;
  for (const auto& tab : tabs_.tabs()) {
    const bool default_title =
        tab.title.empty() || tab.title == "New Tab";
    std::wstring base = default_title
        ? std::wstring(cx::localization::Text(
              cx::localization::StringId::NewTab))
        : Utf8ToWide(tab.title);
    if (base.empty()) {
      base = cx::localization::Text(
          cx::localization::StringId::NewTab);
    }

    std::wstring suffix;
    const auto total = totals.find(base);
    if (total != totals.end() && total->second > 1) {
      const int ordinal = ++seen[base];
      suffix = L" · " + std::to_wstring(ordinal);
    }
    std::wstring title = DisplayTabTitle(base, suffix);

    TCITEMW item{};
    item.mask = TCIF_TEXT | TCIF_PARAM;
    item.pszText = title.data();
    item.lParam = static_cast<LPARAM>(tab.id);
    TabCtrl_InsertItem(tab_strip_, index, &item);

    if (tab.id == tabs_.active_tab_id()) {
      selected = index;
    }
    ++index;
  }

  if (selected >= 0) {
    TabCtrl_SetCurSel(tab_strip_, selected);
  }
  InvalidateRect(tab_strip_, nullptr, TRUE);
}
void AppWindow::RefreshAddressBar() {
  if (!address_bar_) return;
  const auto active = tabs_.active_tab();
  if (!active.has_value()) {
    SetWindowTextW(address_bar_, L"");
    return;
  }
  const std::wstring url = Utf8ToWide(active->url);
  SetWindowTextW(address_bar_, url.c_str());
}

void AppWindow::RefreshAddressFromWebView() {
  if (!webview_ || !address_bar_) {
    return;
  }
  LPWSTR source = nullptr;
  if (FAILED(webview_->get_Source(&source)) || !source) {
    return;
  }
  const std::wstring canonical(source);
  CoTaskMemFree(source);
  if (cx::browser::NavigationController::IsAllowedUrl(canonical)) {
    SetWindowTextW(address_bar_, canonical.c_str());
  }
}

void AppWindow::SetBrowserStatus(std::wstring_view text) {
  if (!status_label_) {
    return;
  }
  const std::wstring owned(text);
  SetWindowTextW(status_label_, owned.c_str());
}

void AppWindow::AddTooltip(
    HWND control,
    const wchar_t* text) {
  if (!tooltip_ || !control || !text) {
    return;
  }
  TOOLINFOW info{};
  info.cbSize = sizeof(info);
  info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  info.hwnd = hwnd_;
  info.uId = reinterpret_cast<UINT_PTR>(control);
  info.lpszText = const_cast<wchar_t*>(text);
  SendMessageW(
      tooltip_,
      TTM_ADDTOOLW,
      0,
      reinterpret_cast<LPARAM>(&info));
}

void AppWindow::NavigateAddressBar() {
  if (!address_bar_) return;

  const int length = GetWindowTextLengthW(address_bar_);
  if (length <= 0) return;

  std::wstring value(
      static_cast<std::size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(
      address_bar_, value.data(), length + 1);
  if (copied <= 0) return;
  value.resize(static_cast<std::size_t>(copied));

  if (!navigation_.NavigateAddress(value)) {
    MessageBeep(MB_ICONWARNING);
    SetBrowserStatus(L"Blocked: unsupported or unsafe address");
    RefreshAddressBar();
    return;
  }
  SetBrowserStatus(L"Loading...");
  RefreshBrowserChrome();
}

void AppWindow::NewTab() {
  const auto id = tabs_.CreateTab();
  if (!id.has_value()) {
    MessageBoxW(
        hwnd_, L"Could not create a new local tab.",
        L"CX Browser", MB_OK | MB_ICONERROR);
    return;
  }

  RefreshBrowserChrome();
  SetBrowserStatus(L"New tab ready");
  if (webview_) {
    navigation_.ActivateTab(*id);
  }
}

void AppWindow::CloseActiveTab() {
  const auto closing = tabs_.active_tab_id();
  if (closing <= 0) return;

  if (!tabs_.CloseTab(closing)) {
    MessageBoxW(
        hwnd_, L"Could not close this tab.",
        L"CX Browser", MB_OK | MB_ICONERROR);
    return;
  }

  navigation_.ForgetTab(closing);
  RefreshBrowserChrome();
  SetBrowserStatus(L"Tab closed");
  if (webview_) {
    navigation_.ActivateTab(tabs_.active_tab_id());
  }
}

void AppWindow::ActivateSelectedTab() {
  const int selected = TabCtrl_GetCurSel(tab_strip_);
  if (selected < 0) return;

  TCITEMW item{};
  item.mask = TCIF_PARAM;
  if (!TabCtrl_GetItem(tab_strip_, selected, &item)) {
    return;
  }

  const auto id = static_cast<std::int64_t>(item.lParam);
  if (navigation_.ActivateTab(id)) {
    RefreshBrowserChrome();
  }
}

void AppWindow::BookmarkCurrent() {
  const auto active = tabs_.active_tab();
  if (!active.has_value() ||
      active->url.empty() ||
      active->url == "about:blank") {
    MessageBeep(MB_ICONWARNING);
    SetBrowserStatus(L"Nothing to bookmark on this tab");
    return;
  }

  if (bookmarks_.Add(
          active->url,
          active->title.empty() ? active->url : active->title) <= 0) {
    MessageBoxW(
        hwnd_, L"Could not save the local bookmark.",
        L"CX Browser", MB_OK | MB_ICONERROR);
    return;
  }

  bookmarks_dialog_.RefreshIfOpen();
  SetBrowserStatus(L"Bookmark saved locally");
}

void AppWindow::OpenLibraryUrl(std::string url) {
  if (navigation_.NavigateUrl(url)) {
    SetBrowserStatus(L"Loading...");
  }
  RefreshBrowserChrome();
}

void AppWindow::InitializeWebView() {
  auto options =
      Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
  options->put_AdditionalBrowserArguments(
      L"--disable-background-networking "
      L"--disable-component-update "
      L"--disable-sync "
      L"--disable-breakpad "
      L"--no-first-run "
      L"--metrics-recording-only");

  const HRESULT hr =
      CreateCoreWebView2EnvironmentWithOptions(
          nullptr, nullptr, options.Get(),
          Callback<
              ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
              [this](
                  HRESULT result,
                  ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result) || !environment) {
                  MessageBoxW(
                      hwnd_,
                      L"WebView2 environment creation failed.",
                      L"CX Build", MB_ICONERROR);
                  return result;
                }

                return environment->CreateCoreWebView2Controller(
                    hwnd_,
                    Callback<
                        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](
                            HRESULT controller_result,
                            ICoreWebView2Controller* controller) -> HRESULT {
                          if (FAILED(controller_result) || !controller) {
                            MessageBoxW(
                                hwnd_,
                                L"WebView2 controller creation failed.",
                                L"CX Build", MB_ICONERROR);
                            return controller_result;
                          }

                          controller_ = controller;
                          controller_->get_CoreWebView2(&webview_);

                          Microsoft::WRL::ComPtr<
                              ICoreWebView2Controller2> controller2;
                          if (SUCCEEDED(controller_.As(&controller2))) {
                            const COLORREF background =
                                design::Color::Background;
                            const COREWEBVIEW2_COLOR color{
                                static_cast<BYTE>(255),
                                static_cast<BYTE>(background & 0xFFu),
                                static_cast<BYTE>((background >> 8) & 0xFFu),
                                static_cast<BYTE>((background >> 16) & 0xFFu)};
                            controller2->put_DefaultBackgroundColor(color);
                          }
                          LayoutControls();

                          EventRegistrationToken starting_token{};
                          webview_->add_NavigationStarting(
                              Callback<
                                  ICoreWebView2NavigationStartingEventHandler>(
                                  [this](ICoreWebView2*,
                                     ICoreWebView2NavigationStartingEventArgs* args)
                                      -> HRESULT {
                                    HandleNavigationStarting(args);
                                    return S_OK;
                                  }).Get(),
                              &starting_token);


                          EventRegistrationToken source_token{};
                          webview_->add_SourceChanged(
                              Callback<
                                  ICoreWebView2SourceChangedEventHandler>(
                                  [this](
                                      ICoreWebView2*,
                                      ICoreWebView2SourceChangedEventArgs*)
                                      -> HRESULT {
                                    RefreshAddressFromWebView();
                                    return S_OK;
                                  }).Get(),
                              &source_token);

                          EventRegistrationToken completed_token{};
                          webview_->add_NavigationCompleted(
                              Callback<
                                  ICoreWebView2NavigationCompletedEventHandler>(
                                  [this](
                                      ICoreWebView2*,
                                      ICoreWebView2NavigationCompletedEventArgs* args)
                                      -> HRESULT {
                                    HandleNavigationCompleted(args);
                                    return S_OK;
                                  }).Get(),
                              &completed_token);

                          EventRegistrationToken new_window_token{};
                          webview_->add_NewWindowRequested(
                              Callback<
                                  ICoreWebView2NewWindowRequestedEventHandler>(
                                  [this](
                                      ICoreWebView2*,
                                      ICoreWebView2NewWindowRequestedEventArgs* args)
                                      -> HRESULT {
                                    HandleNewWindow(args);
                                    return S_OK;
                                  }).Get(),
                              &new_window_token);

                          navigation_.ActivateTab(
                              tabs_.active_tab_id());
                          RefreshBrowserChrome();
                          return S_OK;
                        }).Get());
              }).Get());

  if (FAILED(hr)) {
    MessageBoxW(
        hwnd_,
        L"WebView2 initialization call failed.",
        L"CX Build", MB_ICONERROR);
  }
}

bool AppWindow::NavigateTo(std::wstring_view url) {
  if (!webview_ ||
      !cx::browser::NavigationController::IsAllowedUrl(url)) {
    return false;
  }
  const std::wstring owned(url);
  return SUCCEEDED(webview_->Navigate(owned.c_str()));
}

bool AppWindow::ReloadPage() {
  return webview_ && SUCCEEDED(webview_->Reload());
}

void AppWindow::HandleNavigationStarting(
    ICoreWebView2NavigationStartingEventArgs* args) {
  if (!args) return;

  UINT64 navigation_id = 0;
  if (FAILED(args->get_NavigationId(&navigation_id)) ||
      navigation_id == 0) {
    SetBrowserStatus(L"Navigation tracking unavailable");
    return;
  }

  LPWSTR uri = nullptr;
  std::string target_utf8;
  if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
    const std::wstring target(uri);
    const bool allowed =
        cx::browser::NavigationController::IsAllowedUrl(target);
    target_utf8 = WideToUtf8(target);
    CoTaskMemFree(uri);

    if (!allowed) {
      args->put_Cancel(TRUE);
      SetBrowserStatus(L"Blocked unsafe navigation");
      return;
    }
  }

  navigation_.OnNavigationStarted(
      static_cast<std::uint64_t>(navigation_id),
      target_utf8);
  SetBrowserStatus(L"Loading...");
}

void AppWindow::HandleNavigationCompleted(
    ICoreWebView2NavigationCompletedEventArgs* args) {
  if (!args || !webview_) return;

  UINT64 navigation_id = 0;
  if (FAILED(args->get_NavigationId(&navigation_id))) {
    navigation_id = 0;
  }

  BOOL success = FALSE;
  if (FAILED(args->get_IsSuccess(&success))) {
    success = FALSE;
  }

  LPWSTR source = nullptr;
  LPWSTR title = nullptr;
  std::string source_utf8;
  std::string title_utf8;

  if (SUCCEEDED(webview_->get_Source(&source)) && source) {
    source_utf8 = WideToUtf8(source);
    CoTaskMemFree(source);
  }
  if (SUCCEEDED(webview_->get_DocumentTitle(&title)) && title) {
    title_utf8 = WideToUtf8(title);
    CoTaskMemFree(title);
  }

  navigation_.OnNavigationCompleted(
      static_cast<std::uint64_t>(navigation_id),
      success != FALSE,
      source_utf8,
      title_utf8);

  if (success != FALSE && source_utf8 == "about:blank") {
    const COLORREF background = design::Color::Background;
    std::wstring script =
        L"document.documentElement.style.background='rgb(";
    script += std::to_wstring(
        static_cast<unsigned int>(background & 0xFFu));
    script += L",";
    script += std::to_wstring(
        static_cast<unsigned int>((background >> 8) & 0xFFu));
    script += L",";
    script += std::to_wstring(
        static_cast<unsigned int>((background >> 16) & 0xFFu));
    script +=
        L")';document.body.style.background='inherit';"
        L"document.body.style.margin='0';"
        L"document.documentElement.style.colorScheme='dark';";
    webview_->ExecuteScript(script.c_str(), nullptr);
  }

  RefreshBrowserChrome();
  if (success == FALSE) {
    SetBrowserStatus(L"Error: page could not be loaded");
  } else if (source_utf8 == "about:blank") {
    SetBrowserStatus(L"Local new tab");
  } else {
    SetBrowserStatus(L"Ready");
  }
}

void AppWindow::HandleNewWindow(
    ICoreWebView2NewWindowRequestedEventArgs* args) {
  if (!args) return;

  args->put_Handled(TRUE);

  LPWSTR uri = nullptr;
  if (FAILED(args->get_Uri(&uri)) || !uri) {
    return;
  }

  const std::wstring target(uri);
  CoTaskMemFree(uri);
  if (!cx::browser::NavigationController::IsAllowedUrl(target)) {
    return;
  }

  const auto id = tabs_.CreateTab();
  if (!id.has_value()) {
    return;
  }

  RefreshBrowserChrome();
  navigation_.NavigateAddress(target);
  RefreshBrowserChrome();
}

LRESULT CALLBACK AppWindow::ActivityShieldProc(
    HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto* create =
        reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(
        hwnd, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }

  switch (message) {
    case WM_ERASEBKGND:
      return TRUE;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(hwnd, &paint);
      RECT client{};
      GetClientRect(hwnd, &client);
      HBRUSH fill = CreateSolidBrush(design::Color::SurfaceRaised);
      FillRect(dc, &client, fill);
      DeleteObject(fill);
      HBRUSH rail = CreateSolidBrush(design::Color::AgentActive);
      FrameRect(dc, &client, rail);
      DeleteObject(rail);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, design::Color::Text);
      SelectObject(dc, cx::ui::theme::UiFontSemibold());
      DrawTextW(
          dc, L"Agent active - pointer input locked", -1,
          &client,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      EndPaint(hwnd, &paint);
      return 0;
    }
    case WM_SETCURSOR:
      SetCursor(LoadCursorW(nullptr, IDC_NO));
      return TRUE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
      return 0;
    default:
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }
}

LRESULT CALLBACK AppWindow::WndProc(
    HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<AppWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (message == WM_NCCREATE) {
    const auto* create =
        reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<AppWindow*>(
        create->lpCreateParams);
    if (self) {
      self->hwnd_ = hwnd;
      SetWindowLongPtrW(
          hwnd, GWLP_USERDATA,
          reinterpret_cast<LONG_PTR>(self));
    }
  }

  if (self &&
      (message == WM_CTLCOLORSTATIC ||
       message == WM_CTLCOLOREDIT ||
       message == WM_CTLCOLORBTN ||
       message == WM_CTLCOLORLISTBOX)) {
    return cx::ui::theme::HandleControlColor(
        message, wparam, lparam);
  }

  if (self && message == WM_DRAWITEM) {
    if (cx::ui::theme::DrawOwnerItem(
            reinterpret_cast<const DRAWITEMSTRUCT*>(lparam))) {
      return TRUE;
    }
  }

  if (self && message == WM_SIZE) {
    self->LayoutControls();
    return 0;
  }

  if (self && message == WM_COMMAND) {
    self->HandleCommand(LOWORD(wparam));
    return 0;
  }

  if (self && message == WM_NOTIFY) {
    self->HandleNotify(
        reinterpret_cast<const NMHDR*>(lparam));
    return 0;
  }

  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcW(
      hwnd, message, wparam, lparam);
}
