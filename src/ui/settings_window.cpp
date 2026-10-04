#include "ui/settings_window.h"

#include "agent/agent_core.h"
#include "browser/bookmark_service.h"
#include "browser/history_service.h"
#include "mcp/allowlist_dialog.h"
#include "mcp/allowlist_manager.h"
#include "mcp/mcp_client.h"
#include "storage/database.h"
#include "ui/najdi_theme.h"

#include <commctrl.h>
#include <shellapi.h>

#include <filesystem>
#include <iterator>
#include <string>

namespace cx::ui {
namespace {

constexpr wchar_t kWindowClass[] =
    L"CXBuildSettingsWindow";
constexpr UINT kLocalSizeReady = WM_APP + 201;

constexpr WORD kPrivacyBase = 6001;
constexpr WORD kPermissionBase = 6100;
constexpr WORD kMcpManage = 6201;
constexpr WORD kMcpDisconnect = 6202;
constexpr WORD kMcpRefresh = 6203;
constexpr WORD kDataClearHistory = 6301;
constexpr WORD kDataClearBookmarks = 6302;
constexpr WORD kDataRefresh = 6303;
constexpr WORD kDataOpenFolder = 6304;

std::wstring Utf8ToWide(std::string_view value) {
  return std::wstring(value.begin(), value.end());
}

HWND AddStatic(
    HWND parent,
    const wchar_t* text,
    int x,
    int y,
    int width,
    int height) {
  return CreateWindowExW(
      0, L"STATIC", text,
      WS_CHILD | WS_VISIBLE,
      x, y, width, height,
      parent, nullptr, nullptr, nullptr);
}

HWND AddButton(
    HWND parent,
    const wchar_t* text,
    WORD id,
    int x,
    int y,
    int width,
    int height,
    DWORD style = BS_PUSHBUTTON) {
  const DWORD type = style & BS_TYPEMASK;
  const DWORD visual_style =
      type == BS_PUSHBUTTON
          ? style | BS_OWNERDRAW
          : style;
  return CreateWindowExW(
      0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | visual_style,
      x, y, width, height,
      parent,
      reinterpret_cast<HMENU>(
          static_cast<UINT_PTR>(id)),
      nullptr, nullptr);
}

}  // namespace

SettingsWindow::SettingsWindow(
    storage::Database& database,
    agent::PermissionManager& permissions,
    agent::AgentCore& agent,
    mcp::AllowlistManager& allowlist,
    mcp::AllowlistDialog& allowlist_dialog,
    mcp::McpClient& mcp_client,
    browser::HistoryService& history,
    browser::BookmarkService& bookmarks)
    : database_(database),
      permissions_(permissions),
      agent_(agent),
      allowlist_(allowlist),
      allowlist_dialog_(allowlist_dialog),
      mcp_client_(mcp_client),
      history_(history),
      bookmarks_(bookmarks),
      dashboard_(
          permissions_,
          allowlist_,
          mcp_client_,
          storage::Database::DefaultPath().parent_path()) {}

const std::array<PrivacySettingSpec, 3>&
SettingsWindow::PrivacySettings() {
  static const std::array<PrivacySettingSpec, 3> settings{{
      {
          "privacy.save_history",
          L"Save browsing history locally",
          false,
      },
      {
          "privacy.restore_session",
          L"Restore previous tabs on startup",
          false,
      },
      {
          "privacy.clear_history_on_exit",
          L"Clear local browsing history when CX exits",
          true,
      },
  }};
  return settings;
}

void SettingsWindow::Show(HWND owner) {
  owner_ = owner;
  if (hwnd_) {
    Refresh();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    return;
  }

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.hInstance = instance;
  window_class.lpfnWndProc = &SettingsWindow::WndProc;
  window_class.lpszClassName = kWindowClass;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground =
      theme::BackgroundBrush();
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_APPWINDOW,
      kWindowClass,
      L"CX Settings & Privacy",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      900, 680,
      owner_, nullptr, instance, this);
  if (!hwnd_) {
    return;
  }

  theme::ApplyWindowChrome(hwnd_);
  CreateControls();
  theme::ApplyFontToChildren(hwnd_);
  Layout();
  Refresh();
  ShowPage(0);
  dashboard_.RefreshLocalSizeAsync(
      hwnd_, kLocalSizeReady);
}

void SettingsWindow::Refresh() {
  if (!hwnd_) {
    return;
  }
  RefreshDashboard();
  RefreshPrivacy();
  RefreshPermissions();
  RefreshMcp();
  RefreshData();
}

void SettingsWindow::CreateControls() {
  dashboard_label_ = CreateWindowExW(
      WS_EX_CLIENTEDGE,
      L"STATIC",
      L"",
      WS_CHILD | WS_VISIBLE | SS_LEFT,
      16, 16, 840, 92,
      hwnd_, nullptr, nullptr, nullptr);

  tabs_ = CreateWindowExW(
      0, WC_TABCONTROLW, L"",
      WS_CHILD | WS_VISIBLE |
          WS_CLIPSIBLINGS | WS_TABSTOP |
          TCS_OWNERDRAWFIXED,
      16, 120, 840, 500,
      hwnd_, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  theme::StyleTabControl(tabs_);

  const wchar_t* names[] = {
      L"Privacy & Security",
      L"Agent Permissions",
      L"MCP Allowlist",
      L"Data & Storage",
  };
  for (int i = 0; i < 4; ++i) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText =
        const_cast<wchar_t*>(names[i]);
    TabCtrl_InsertItem(tabs_, i, &item);
  }

  CreatePrivacyPage();
  CreatePermissionsPage();
  CreateMcpPage();
  CreateDataPage();
}

void SettingsWindow::CreatePrivacyPage() {
  int y = 172;
  HWND intro = AddStatic(
      hwnd_,
      L"Privacy-first defaults. Every switch below is stored immediately in local SQLite.",
      40, y, 790, 24);
  page_controls_[0].push_back(intro);
  y += 36;

  const auto& specs = PrivacySettings();
  for (std::size_t i = 0; i < specs.size(); ++i) {
    const WORD id =
        static_cast<WORD>(kPrivacyBase + i);
    HWND box = AddButton(
        hwnd_, specs[i].label, id,
        48, y, 620, 28,
        BS_AUTOCHECKBOX);
    page_controls_[0].push_back(box);
    y += 34;
  }

  HWND sync = AddStatic(
      hwnd_,
      L"WebView sync: disabled by design (not configurable).",
      48, y + 6, 700, 24);
  page_controls_[0].push_back(sync);
  HWND telemetry = AddStatic(
      hwnd_,
      L"CX application telemetry upload: none (not configurable).",
      48, y + 34, 700, 24);
  page_controls_[0].push_back(telemetry);
}

void SettingsWindow::CreatePermissionsPage() {
  int y = 172;
  HWND intro = AddStatic(
      hwnd_,
      L"All capabilities default to Deny. Checking a box is explicit local consent; unchecking revokes it immediately.",
      40, y, 800, 36);
  page_controls_[1].push_back(intro);
  y += 44;

  const agent::Capability capabilities[] = {
      agent::Capability::AgentRun,
      agent::Capability::ReadPage,
      agent::Capability::Navigate,
      agent::Capability::ManageTabs,
      agent::Capability::ClipboardWrite,
      agent::Capability::NativeMessaging,
      agent::Capability::McpConnect,
  };

  for (std::size_t i = 0;
       i < std::size(capabilities);
       ++i) {
    const auto& descriptor =
        agent::DescribeCapability(capabilities[i]);
    std::wstring label = descriptor.title;
    label += L"  [";
    label += Utf8ToWide(descriptor.id);
    label += L"]";

    HWND box = AddButton(
        hwnd_,
        label.c_str(),
        static_cast<WORD>(kPermissionBase + i),
        48, y, 720, 28,
        BS_AUTOCHECKBOX);
    page_controls_[1].push_back(box);
    permission_controls_.push_back(
        {box, capabilities[i]});
    y += 36;
  }
}

void SettingsWindow::CreateMcpPage() {
  int y = 172;
  HWND intro = AddStatic(
      hwnd_,
      L"Only executables in the explicit local allowlist can be launched. MCP remains gated by the mcp.connect permission.",
      40, y, 800, 36);
  page_controls_[2].push_back(intro);
  y += 44;

  mcp_list_ = CreateWindowExW(
      WS_EX_CLIENTEDGE,
      L"LISTBOX",
      nullptr,
      WS_CHILD | WS_VISIBLE |
          WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
      48, y, 760, 240,
      hwnd_, nullptr, nullptr, nullptr);
  page_controls_[2].push_back(mcp_list_);
  y += 252;

  page_controls_[2].push_back(
      AddButton(
          hwnd_, L"Manage Allowlist...",
          kMcpManage,
          48, y, 160, 32));
  page_controls_[2].push_back(
      AddButton(
          hwnd_, L"Disconnect Active MCP",
          kMcpDisconnect,
          220, y, 180, 32));
  page_controls_[2].push_back(
      AddButton(
          hwnd_, L"Refresh",
          kMcpRefresh,
          412, y, 100, 32));
}

void SettingsWindow::CreateDataPage() {
  int y = 172;
  data_label_ = AddStatic(
      hwnd_,
      L"",
      40, y, 800, 150);
  page_controls_[3].push_back(data_label_);
  y += 164;

  page_controls_[3].push_back(
      AddButton(
          hwnd_, L"Clear Local History...",
          kDataClearHistory,
          48, y, 170, 32));
  page_controls_[3].push_back(
      AddButton(
          hwnd_, L"Clear Bookmarks...",
          kDataClearBookmarks,
          230, y, 150, 32));
  page_controls_[3].push_back(
      AddButton(
          hwnd_, L"Refresh Data Size",
          kDataRefresh,
          392, y, 150, 32));
  page_controls_[3].push_back(
      AddButton(
          hwnd_, L"Open Local Data Folder",
          kDataOpenFolder,
          554, y, 190, 32));
}

void SettingsWindow::Layout() {
  if (!hwnd_) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const int width =
      static_cast<int>(client.right - client.left);
  const int height =
      static_cast<int>(client.bottom - client.top);

  MoveWindow(
      dashboard_label_,
      16, 16,
      width > 48 ? width - 32 : 16,
      94, TRUE);
  MoveWindow(
      tabs_,
      16, 122,
      width > 48 ? width - 32 : 16,
      height > 150 ? height - 138 : 40,
      TRUE);
}

void SettingsWindow::ShowPage(int index) {
  if (index < 0 || index >= 4) {
    return;
  }
  for (int page = 0; page < 4; ++page) {
    const int show =
        page == index ? SW_SHOW : SW_HIDE;
    for (HWND control : page_controls_[page]) {
      if (control) {
        ShowWindow(control, show);
      }
    }
  }
}

void SettingsWindow::HandleCommand(WORD command) {
  if (command >= kPrivacyBase &&
      command < kPrivacyBase + PrivacySettings().size()) {
    HandlePrivacyToggle(command);
    return;
  }

  if (command >= kPermissionBase &&
      command < kPermissionBase +
          permission_controls_.size()) {
    HandlePermissionToggle(command);
    return;
  }

  switch (command) {
    case kMcpManage:
      allowlist_dialog_.Show(hwnd_);
      RefreshMcp();
      RefreshDashboard();
      break;
    case kMcpDisconnect:
      mcp_client_.Stop();
      RefreshMcp();
      RefreshDashboard();
      break;
    case kMcpRefresh:
      allowlist_.Load();
      RefreshMcp();
      RefreshDashboard();
      break;
    case kDataClearHistory:
      if (MessageBoxW(
              hwnd_,
              L"Clear all local browsing history now?",
              L"CX Data & Storage",
              MB_OKCANCEL | MB_ICONWARNING |
                  MB_DEFBUTTON2) == IDOK) {
        history_.Clear();
        dashboard_.RefreshLocalSizeAsync(
            hwnd_, kLocalSizeReady);
        RefreshData();
      }
      break;
    case kDataClearBookmarks:
      if (MessageBoxW(
              hwnd_,
              L"Clear all local bookmarks now?",
              L"CX Data & Storage",
              MB_OKCANCEL | MB_ICONWARNING |
                  MB_DEFBUTTON2) == IDOK) {
        for (const auto& bookmark : bookmarks_.List()) {
          bookmarks_.Remove(bookmark.id);
        }
        dashboard_.RefreshLocalSizeAsync(
            hwnd_, kLocalSizeReady);
        RefreshData();
      }
      break;
    case kDataRefresh:
      dashboard_.RefreshLocalSizeAsync(
          hwnd_, kLocalSizeReady);
      RefreshData();
      break;
    case kDataOpenFolder:
      ShellExecuteW(
          hwnd_, L"open",
          dashboard_.local_root().c_str(),
          nullptr, nullptr, SW_SHOWNORMAL);
      break;
    default:
      break;
  }
}

void SettingsWindow::HandleNotify(
    const NMHDR* header) {
  if (!header || header->hwndFrom != tabs_) {
    return;
  }
  if (header->code == TCN_SELCHANGE) {
    const int index = TabCtrl_GetCurSel(tabs_);
    ShowPage(index < 0 ? 0 : index);
    InvalidateRect(tabs_, nullptr, TRUE);
  }
}

void SettingsWindow::HandlePrivacyToggle(
    WORD command) {
  const std::size_t index =
      static_cast<std::size_t>(command - kPrivacyBase);
  if (index >= PrivacySettings().size()) {
    return;
  }

  HWND control = nullptr;
  const auto& controls = page_controls_[0];
  const std::size_t control_index = index + 1;
  if (control_index < controls.size()) {
    control = controls[control_index];
  }
  if (!control) {
    return;
  }

  const bool enabled =
      SendMessageW(control, BM_GETCHECK, 0, 0) ==
      BST_CHECKED;
  const auto& spec = PrivacySettings()[index];

  if (!WriteBool(spec.key, enabled)) {
    SendMessageW(
        control, BM_SETCHECK,
        enabled ? BST_UNCHECKED : BST_CHECKED, 0);
    MessageBoxW(
        hwnd_,
        L"Could not persist this privacy setting.",
        L"CX Settings", MB_OK | MB_ICONERROR);
    return;
  }

  RefreshDashboard();
  RefreshData();
}

void SettingsWindow::HandlePermissionToggle(
    WORD command) {
  const std::size_t index =
      static_cast<std::size_t>(
          command - kPermissionBase);
  if (index >= permission_controls_.size()) {
    return;
  }

  HWND control = permission_controls_[index].first;
  const auto capability =
      permission_controls_[index].second;
  const bool enabled =
      SendMessageW(control, BM_GETCHECK, 0, 0) ==
      BST_CHECKED;

  if (!permissions_.SetGranted(capability, enabled)) {
    SendMessageW(
        control, BM_SETCHECK,
        enabled ? BST_UNCHECKED : BST_CHECKED, 0);
    MessageBoxW(
        hwnd_,
        L"Could not persist this permission.",
        L"CX Settings", MB_OK | MB_ICONERROR);
    return;
  }

  if (!enabled) {
    if (capability == agent::Capability::McpConnect) {
      mcp_client_.Stop();
    }
    if (capability == agent::Capability::AgentRun) {
      mcp_client_.Stop();
      agent_.Stop();
    }
  }

  RefreshPermissions();
  RefreshMcp();
  RefreshDashboard();
}

bool SettingsWindow::ReadBool(
    std::string_view key,
    bool default_value) const {
  const auto stored = database_.GetSetting(key);
  if (!stored.has_value()) {
    return default_value;
  }
  return *stored == "1";
}

bool SettingsWindow::WriteBool(
    std::string_view key,
    bool value) {
  return database_.SetSetting(
      key, value ? "1" : "0");
}

void SettingsWindow::RefreshDashboard() {
  if (!dashboard_label_) {
    return;
  }
  const auto text = dashboard_.Summary();
  SetWindowTextW(
      dashboard_label_, text.c_str());
}

void SettingsWindow::RefreshPrivacy() {
  const auto& specs = PrivacySettings();
  const auto& controls = page_controls_[0];
  for (std::size_t i = 0; i < specs.size(); ++i) {
    const std::size_t control_index = i + 1;
    if (control_index >= controls.size()) {
      break;
    }
    SendMessageW(
        controls[control_index],
        BM_SETCHECK,
        ReadBool(
            specs[i].key,
            specs[i].default_value)
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);
  }
}

void SettingsWindow::RefreshPermissions() {
  for (const auto& [control, capability] :
       permission_controls_) {
    SendMessageW(
        control,
        BM_SETCHECK,
        permissions_.IsGranted(capability)
            ? BST_CHECKED
            : BST_UNCHECKED,
        0);
  }
}

void SettingsWindow::RefreshMcp() {
  if (!mcp_list_) {
    return;
  }

  SendMessageW(
      mcp_list_, LB_RESETCONTENT, 0, 0);
  const auto active = mcp_client_.active_server();
  const bool running = mcp_client_.IsRunning();

  for (const auto& server : allowlist_.List()) {
    std::wstring row =
        Utf8ToWide(server.id);
    row += L" | ";
    row += Utf8ToWide(server.command);
    if (running && active == server.id) {
      row += L" | ACTIVE";
    }
    SendMessageW(
        mcp_list_, LB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            row.c_str()));
  }

  if (allowlist_.List().empty()) {
    SendMessageW(
        mcp_list_, LB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            L"(allowlist empty)"));
  }
}

void SettingsWindow::RefreshData() {
  if (!data_label_) {
    return;
  }

  std::wstring text =
      L"Local data root:\r\n";
  text += dashboard_.local_root().wstring();
  text += L"\r\n\r\nDatabase:\r\n";
  text += database_.path().wstring();
  text += L"\r\n\r\nHistory rows currently visible locally: ";
  text += std::to_wstring(
      history_.Recent(1000).size());
  text += L"\r\nBookmarks: ";
  text += std::to_wstring(
      bookmarks_.List().size());

  SetWindowTextW(
      data_label_, text.c_str());
}

LRESULT CALLBACK SettingsWindow::WndProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
  auto* self =
      reinterpret_cast<SettingsWindow*>(
          GetWindowLongPtrW(
              hwnd, GWLP_USERDATA));

  if (message == WM_NCCREATE) {
    const auto* create =
        reinterpret_cast<CREATESTRUCTW*>(
            lparam);
    self = static_cast<SettingsWindow*>(
        create->lpCreateParams);
    SetWindowLongPtrW(
        hwnd, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(self));
  }

  if (self &&
      message == kLocalSizeReady) {
    if (self->dashboard_.AcceptLocalSizeResult(lparam)) {
      self->RefreshDashboard();
    }
    return 0;
  }

  if (self &&
      (message == WM_CTLCOLORSTATIC ||
       message == WM_CTLCOLOREDIT ||
       message == WM_CTLCOLORBTN ||
       message == WM_CTLCOLORLISTBOX)) {
    return theme::HandleControlColor(
        message, wparam, lparam);
  }

  if (self && message == WM_DRAWITEM) {
    if (theme::DrawOwnerItem(
            reinterpret_cast<const DRAWITEMSTRUCT*>(lparam))) {
      return TRUE;
    }
  }

  if (self && message == WM_SIZE) {
    self->Layout();
    return 0;
  }

  if (self && message == WM_NOTIFY) {
    self->HandleNotify(
        reinterpret_cast<const NMHDR*>(
            lparam));
    return 0;
  }

  if (self && message == WM_COMMAND) {
    self->HandleCommand(
        LOWORD(wparam));
    return 0;
  }

  if (self && message == WM_CLOSE) {
    ShowWindow(hwnd, SW_HIDE);
    return 0;
  }

  if (self && message == WM_DESTROY) {
    self->hwnd_ = nullptr;
    return 0;
  }

  return DefWindowProcW(
      hwnd, message, wparam, lparam);
}

}  // namespace cx::ui
