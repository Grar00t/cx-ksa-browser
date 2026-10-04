#include "app_window.h"

#include "agent/agent_core.h"
#include "agent/permissions.h"
#include "ui/permission_dialog.h"
#include "ui/settings_window.h"
#include "ui/najdi_theme.h"
#include "browser/bookmark_service.h"
#include "browser/history_service.h"
#include "browser/tab_manager.h"
#include "mcp/allowlist_dialog.h"
#include "mcp/mcp_client.h"

#include <WebView2EnvironmentOptions.h>
#include <commctrl.h>

#include <algorithm>
#include <string>
#include <utility>

using Microsoft::WRL::Callback;
namespace design = cx::ui::design;

namespace {
constexpr wchar_t kWindowClass[] = L"CXBuildWindowClass";
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
  controls.dwICC = ICC_TAB_CLASSES;
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

  AppendMenuW(browser_menu, MF_STRING, kNewTab, L"New Tab");
  AppendMenuW(browser_menu, MF_STRING, kCloseTab, L"Close Tab");
  AppendMenuW(browser_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(browser_menu, MF_STRING, kHistory, L"History...");
  AppendMenuW(browser_menu, MF_STRING, kBookmarks, L"Bookmarks...");
  AppendMenuW(browser_menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(
      browser_menu, MF_STRING, kSettings,
      L"Settings & Privacy...");
  AppendMenuW(
      menu_bar, MF_POPUP,
      reinterpret_cast<UINT_PTR>(browser_menu), L"Browser");

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

void AppWindow::CreateBrowserControls() {
  tab_strip_ = CreateWindowExW(
      0, WC_TABCONTROLW, L"",
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
          TCS_TABS | TCS_SINGLELINE | TCS_OWNERDRAWFIXED,
      0, 0,
      design::Density::MinimumInputWidth,
      design::Density::TabHeight,
      hwnd_, nullptr, nullptr, nullptr);
  cx::ui::theme::StyleTabControl(tab_strip_);

  back_button_ = CreateWindowExW(
      0, L"BUTTON", L"<",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::NavigationButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kBack), nullptr, nullptr);
  forward_button_ = CreateWindowExW(
      0, L"BUTTON", L">",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::NavigationButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kForward), nullptr, nullptr);
  reload_button_ = CreateWindowExW(
      0, L"BUTTON", L"Reload",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::ReloadButtonWidth,
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
  go_button_ = CreateWindowExW(
      0, L"BUTTON", L"Go",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::GoButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kGo), nullptr, nullptr);
  cx::ui::theme::MarkPrimaryAction(go_button_);
  bookmark_button_ = CreateWindowExW(
      0, L"BUTTON", L"Bookmark",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::BookmarkButtonWidth,
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
      0, L"BUTTON", L"x",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0,
      design::Density::IconButtonWidth,
      design::Density::ControlHeight,
      hwnd_, reinterpret_cast<HMENU>(kCloseTab), nullptr, nullptr);

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
      navigation_.Reload();
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
    case kMcpAllowlist:
      mcp_dialog_.Show(hwnd_);
      return;
    default:
      break;
  }

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

    if (!permission_dialog_.ConfirmRevokeAll(hwnd_, granted.size())) {
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
  const int width = static_cast<int>(
      client.right > client.left
          ? client.right - client.left
          : 0);
  const int height = static_cast<int>(
      client.bottom > client.top
          ? client.bottom - client.top
          : 0);

  const int tab_height = design::Density::TabHeight;
  const int toolbar_y =
      tab_height + design::Spacing::Xxs;
  const int toolbar_height =
      design::Density::ToolbarHeight;
  const int content_y =
      toolbar_y + toolbar_height + design::Spacing::Xs;
  const int control_height =
      design::Density::ControlHeight;
  const int gap = design::Spacing::Sm;

  MoveWindow(tab_strip_, 0, 0, width, tab_height, TRUE);

  int x = design::Spacing::Sm;
  MoveWindow(
      back_button_, x, toolbar_y,
      design::Density::NavigationButtonWidth,
      control_height, TRUE);
  x += design::Density::NavigationButtonWidth + gap;
  MoveWindow(
      forward_button_, x, toolbar_y,
      design::Density::NavigationButtonWidth,
      control_height, TRUE);
  x += design::Density::NavigationButtonWidth + gap;
  MoveWindow(
      reload_button_, x, toolbar_y,
      design::Density::ReloadButtonWidth,
      control_height, TRUE);
  x += design::Density::ReloadButtonWidth + gap;

  const int right_fixed =
      design::Density::GoButtonWidth + gap +
      design::Density::BookmarkButtonWidth + gap +
      design::Density::IconButtonWidth + gap +
      design::Density::IconButtonWidth +
      design::Spacing::Lg;
  const int requested_address_width =
      width - x - right_fixed;
  const int address_width =
      requested_address_width >
              design::Density::MinimumInputWidth
          ? requested_address_width
          : design::Density::MinimumInputWidth;
  MoveWindow(
      address_bar_, x, toolbar_y,
      address_width, control_height, TRUE);
  x += address_width + gap;
  MoveWindow(
      go_button_, x, toolbar_y,
      design::Density::GoButtonWidth,
      control_height, TRUE);
  x += design::Density::GoButtonWidth + gap;
  MoveWindow(
      bookmark_button_, x, toolbar_y,
      design::Density::BookmarkButtonWidth,
      control_height, TRUE);
  x += design::Density::BookmarkButtonWidth + gap;
  MoveWindow(
      new_tab_button_, x, toolbar_y,
      design::Density::IconButtonWidth,
      control_height, TRUE);
  x += design::Density::IconButtonWidth + gap;
  MoveWindow(
      close_tab_button_, x, toolbar_y,
      design::Density::IconButtonWidth,
      control_height, TRUE);

  if (controller_) {
    RECT bounds{0, content_y, width, height};
    controller_->put_Bounds(bounds);
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

  TabCtrl_DeleteAllItems(tab_strip_);
  int selected = -1;
  int index = 0;
  for (const auto& tab : tabs_.tabs()) {
    std::wstring title = Utf8ToWide(
        tab.title.empty() ? std::string_view("New Tab")
                          : std::string_view(tab.title));
    if (title.size() > 36) {
      title.resize(33);
      title += L"...";
    }

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
    RefreshAddressBar();
    return;
  }
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
}

void AppWindow::OpenLibraryUrl(std::string url) {
  navigation_.NavigateUrl(url);
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
                          LayoutControls();

                          EventRegistrationToken starting_token{};
                          webview_->add_NavigationStarting(
                              Callback<
                                  ICoreWebView2NavigationStartingEventHandler>(
                                  [](ICoreWebView2*,
                                     ICoreWebView2NavigationStartingEventArgs* args)
                                      -> HRESULT {
                                    LPWSTR uri = nullptr;
                                    if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                      const bool allowed =
                                          cx::browser::NavigationController::
                                              IsAllowedUrl(uri);
                                      CoTaskMemFree(uri);
                                      if (!allowed) {
                                        args->put_Cancel(TRUE);
                                      }
                                    }
                                    return S_OK;
                                  }).Get(),
                              &starting_token);

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

void AppWindow::HandleNavigationCompleted(
    ICoreWebView2NavigationCompletedEventArgs* args) {
  if (!args || !webview_) return;

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
      success != FALSE, source_utf8, title_utf8);
  RefreshBrowserChrome();
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
  navigation_.ActivateTab(*id);
  navigation_.NavigateAddress(target);
  RefreshBrowserChrome();
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
