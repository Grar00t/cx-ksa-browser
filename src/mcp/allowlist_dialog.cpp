#include "mcp/allowlist_dialog.h"

#include "mcp/allowlist_manager.h"
#include "mcp/mcp_client.h"

#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace cx::mcp {
namespace {

constexpr wchar_t kDialogClass[] = L"CXBuildMcpAllowlistDialog";
constexpr WORD kAddServer = 5101;
constexpr WORD kRemoveServer = 5102;
constexpr WORD kConnectServer = 5103;
constexpr WORD kDisconnectServer = 5104;
constexpr WORD kCloseDialog = 5105;

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }

  std::wstring output(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), output.data(), size) != size) {
    return {};
  }
  return output;
}

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0,
      nullptr, nullptr);
  if (size <= 0) {
    return {};
  }

  std::string output(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), output.data(), size,
          nullptr, nullptr) != size) {
    return {};
  }
  return output;
}

std::string MakeServerId(
    const std::filesystem::path& path,
    const AllowlistManager& allowlist) {
  std::string base = WideToUtf8(path.stem().wstring());
  for (char& ch : base) {
    const unsigned char value = static_cast<unsigned char>(ch);
    if (!(std::isalnum(value) || ch == '.' ||
          ch == '_' || ch == '-')) {
      ch = '-';
    }
  }
  base.erase(
      std::unique(base.begin(), base.end(),
                  [](char a, char b) {
                    return a == '-' && b == '-';
                  }),
      base.end());
  if (base.empty()) {
    base = "local-server";
  }
  if (base.size() > 48) {
    base.resize(48);
  }

  std::string candidate = base;
  for (unsigned suffix = 2;
       allowlist.IsAllowed(candidate); ++suffix) {
    candidate = base + "-" + std::to_string(suffix);
  }
  return candidate;
}

}  // namespace

AllowlistDialog::AllowlistDialog(
    AllowlistManager& allowlist,
    McpClient& client)
    : allowlist_(allowlist), client_(client) {}

void AllowlistDialog::Show(HWND owner) {
  if (hwnd_) {
    SetForegroundWindow(hwnd_);
    return;
  }

  owner_ = owner;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.hInstance = instance;
  window_class.lpfnWndProc = &AllowlistDialog::WndProc;
  window_class.lpszClassName = kDialogClass;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground =
      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kDialogClass,
      L"CX MCP Allowlist",
      WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT, 820, 430,
      owner_, nullptr, instance, this);

  if (!hwnd_) {
    return;
  }

  EnableWindow(owner_, FALSE);
  CreateControls();
  Refresh();
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);

  MSG message{};
  BOOL get_message_result = TRUE;
  while (hwnd_ &&
         (get_message_result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    if (IsDialogMessageW(hwnd_, &message)) {
      continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  if (get_message_result == 0) {
    PostQuitMessage(static_cast<int>(message.wParam));
  }

  EnableWindow(owner_, TRUE);
  SetForegroundWindow(owner_);
}

void AllowlistDialog::CreateControls() {
  const auto path_text =
      L"Config: " + allowlist_.path().wstring();
  CreateWindowExW(
      0, L"STATIC", path_text.c_str(),
      WS_CHILD | WS_VISIBLE,
      18, 16, 770, 22,
      hwnd_, nullptr, nullptr, nullptr);

  list_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,

      WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
      18, 46, 770, 250,
      hwnd_, nullptr, nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Add Local Server...",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      18, 312, 150, 30,
      hwnd_, reinterpret_cast<HMENU>(kAddServer),
      nullptr, nullptr);
  CreateWindowExW(
      0, L"BUTTON", L"Remove",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      178, 312, 100, 30,
      hwnd_, reinterpret_cast<HMENU>(kRemoveServer),
      nullptr, nullptr);
  CreateWindowExW(
      0, L"BUTTON", L"Connect",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      288, 312, 100, 30,
      hwnd_, reinterpret_cast<HMENU>(kConnectServer),
      nullptr, nullptr);
  CreateWindowExW(
      0, L"BUTTON", L"Disconnect",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      398, 312, 100, 30,
      hwnd_, reinterpret_cast<HMENU>(kDisconnectServer),
      nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Close",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      688, 312, 100, 30,
      hwnd_, reinterpret_cast<HMENU>(kCloseDialog),
      nullptr, nullptr);

  status_ = CreateWindowExW(
      0, L"STATIC",
      L"Only listed local executables can be started. Limit: 10 req/s.",
      WS_CHILD | WS_VISIBLE,
      18, 354, 770, 24,
      hwnd_, nullptr, nullptr, nullptr);
}

void AllowlistDialog::Refresh() {
  SendMessageW(list_, LB_RESETCONTENT, 0, 0);
  for (const auto& server : allowlist_.List()) {
    std::wstring row = Utf8ToWide(server.id);
    row += L" | ";
    row += Utf8ToWide(server.command);
    row += L" | 10 req/s";
    SendMessageW(
        list_, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(row.c_str()));
  }

  const auto active = client_.active_server();
  if (!active.empty() && client_.IsRunning()) {
    const std::wstring text =
        L"Connected via stdio: " + Utf8ToWide(active);
    SetWindowTextW(status_, text.c_str());
  } else {
    SetWindowTextW(
        status_,
        L"Only listed local executables can be started. Limit: 10 req/s.");
  }
}

void AllowlistDialog::HandleCommand(WORD command) {
  switch (command) {
    case kAddServer:
      AddServer();
      break;
    case kRemoveServer:
      RemoveSelected();
      break;
    case kConnectServer:
      ConnectSelected();
      break;
    case kDisconnectServer:
      client_.Stop();
      Refresh();
      break;
    case kCloseDialog:
      DestroyWindow(hwnd_);
      break;
    default:
      break;
  }
}

void AllowlistDialog::AddServer() {
  std::array<wchar_t, 32768> file{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = hwnd_;
  dialog.lpstrFile = file.data();
  dialog.nMaxFile = static_cast<DWORD>(file.size());
  dialog.lpstrFilter =
      L"Executable files (*.exe)\0*.exe\0All files (*.*)\0*.*\0";

  dialog.nFilterIndex = 1;
  dialog.Flags =
      OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
      OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;

  if (!GetOpenFileNameW(&dialog)) {
    return;
  }

  const std::filesystem::path path(file.data());
  ServerConfig server;
  server.id = MakeServerId(path, allowlist_);
  server.command = WideToUtf8(path.wstring());

  if (!AllowlistManager::ValidateServer(server) ||
      !allowlist_.AddOrUpdate(server)) {
    SetWindowTextW(
        status_, L"Could not save this server to the allowlist.");
    return;
  }

  Refresh();
  const std::wstring text =
      L"Added: " + Utf8ToWide(server.id);
  SetWindowTextW(status_, text.c_str());
}

void AllowlistDialog::RemoveSelected() {
  const auto id = SelectedServerId();
  if (!id.has_value()) {
    SetWindowTextW(status_, L"Select a server to remove.");
    return;
  }

  const std::wstring prompt =
      L"Remove '" + Utf8ToWide(*id) +
      L"' from the MCP allowlist?";
  if (MessageBoxW(
          hwnd_, prompt.c_str(), L"CX MCP Allowlist",
          MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) {
    return;
  }

  if (client_.active_server() == *id) {
    client_.Stop();
  }

  if (!allowlist_.Remove(*id)) {
    SetWindowTextW(
        status_, L"Could not remove the selected server.");
    return;
  }

  Refresh();
  SetWindowTextW(status_, L"Server removed from allowlist.");
}

void AllowlistDialog::ConnectSelected() {
  const auto id = SelectedServerId();
  if (!id.has_value()) {
    SetWindowTextW(status_, L"Select an allowed server to connect.");
    return;
  }

  if (!client_.Start(hwnd_, *id)) {
    SetWindowTextW(
        status_,
        L"Connection denied or failed. Start the agent and approve MCP access.");

    return;
  }

  Refresh();
}

std::optional<std::string> AllowlistDialog::SelectedServerId() const {
  const LRESULT selection =
      SendMessageW(list_, LB_GETCURSEL, 0, 0);
  if (selection == LB_ERR) {
    return std::nullopt;
  }

  const LRESULT length =
      SendMessageW(list_, LB_GETTEXTLEN, selection, 0);
  if (length == LB_ERR || length <= 0) {
    return std::nullopt;
  }

  std::wstring row(
      static_cast<std::size_t>(length) + 1, L'\0');
  if (SendMessageW(
          list_, LB_GETTEXT, selection,
          reinterpret_cast<LPARAM>(row.data())) == LB_ERR) {
    return std::nullopt;
  }
  row.resize(static_cast<std::size_t>(length));

  const auto separator = row.find(L" | ");
  const std::wstring id =
      separator == std::wstring::npos
          ? row
          : row.substr(0, separator);
  const std::string utf8 = WideToUtf8(id);

  if (utf8.empty()) {
    return std::nullopt;
  }
  return utf8;
}

LRESULT CALLBACK AllowlistDialog::WndProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<AllowlistDialog*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (message == WM_NCCREATE) {
    const auto* create =
        reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<AllowlistDialog*>(create->lpCreateParams);
    SetWindowLongPtrW(
        hwnd, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(self));
  }

  if (self && message == WM_COMMAND) {
    self->HandleCommand(LOWORD(wparam));
    return 0;
  }

  if (message == WM_CLOSE) {
    DestroyWindow(hwnd);
    return 0;
  }

  if (self && message == WM_DESTROY) {
    self->hwnd_ = nullptr;
    self->list_ = nullptr;
    self->status_ = nullptr;
    return 0;
  }

  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace cx::mcp
