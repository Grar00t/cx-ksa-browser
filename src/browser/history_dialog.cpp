#include "browser/history_dialog.h"

#include "browser/history_service.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace cx::browser {
namespace {

constexpr wchar_t kClassName[] =
    L"CXBuildHistoryDialog";
constexpr WORD kSearch = 5201;
constexpr WORD kOpen = 5202;
constexpr WORD kClear = 5203;
constexpr WORD kClose = 5204;

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()),
      nullptr, 0);
  if (size <= 0) {
    return {};
  }

  std::wstring output(
      static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS,
          value.data(), static_cast<int>(value.size()),
          output.data(), size) != size) {
    return {};
  }
  return output;
}

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()),
      nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }

  std::string output(
      static_cast<std::size_t>(size), '\0');

  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS,
          value.data(), static_cast<int>(value.size()),
          output.data(), size, nullptr, nullptr) != size) {
    return {};
  }
  return output;
}

}  // namespace

HistoryDialog::HistoryDialog(
    HistoryService& history,
    std::function<void(std::string)> open_url)
    : history_(history),
      open_url_(std::move(open_url)) {}

void HistoryDialog::Show(HWND owner) {
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
  window_class.lpfnWndProc = &HistoryDialog::WndProc;
  window_class.lpszClassName = kClassName;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground =
      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW,
      kClassName,
      L"CX Local History",
      WS_OVERLAPPED | WS_CAPTION |
          WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT,
      760, 520,
      owner_, nullptr, instance, this);
  if (!hwnd_) {
    return;
  }

  CreateControls();
  Refresh();
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
}

void HistoryDialog::CreateControls() {

  CreateWindowExW(
      0, L"STATIC", L"Search local history:",
      WS_CHILD | WS_VISIBLE,
      14, 16, 130, 22,
      hwnd_, nullptr, nullptr, nullptr);

  query_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          ES_AUTOHSCROLL,
      145, 12, 430, 28,
      hwnd_, nullptr, nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Search",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      585, 12, 80, 28,
      hwnd_, reinterpret_cast<HMENU>(kSearch),
      nullptr, nullptr);

  list_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
      WS_CHILD | WS_VISIBLE | WS_VSCROLL |
          LBS_NOTIFY,
      14, 52, 716, 360,
      hwnd_, nullptr, nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Open",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      14, 426, 90, 30,
      hwnd_, reinterpret_cast<HMENU>(kOpen),
      nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Clear History...",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      114, 426, 120, 30,
      hwnd_, reinterpret_cast<HMENU>(kClear),
      nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Close",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      640, 426, 90, 30,
      hwnd_, reinterpret_cast<HMENU>(kClose),
      nullptr, nullptr);
}

void HistoryDialog::Refresh() {
  entries_ = history_.Search(QueryText(), 500);
  SendMessageW(list_, LB_RESETCONTENT, 0, 0);

  for (const auto& entry : entries_) {

    std::wstring row =
        Utf8ToWide(
            entry.title.empty()
                ? entry.url
                : entry.title);
    if (!entry.title.empty()) {
      row += L"  —  ";
      row += Utf8ToWide(entry.url);
    }
    SendMessageW(
        list_, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(row.c_str()));
  }
}

void HistoryDialog::HandleCommand(WORD command) {
  if (command == kSearch) {
    Refresh();
    return;
  }

  if (command == kOpen) {
    const int index = SelectedIndex();
    if (index >= 0 &&
        static_cast<std::size_t>(index) < entries_.size()) {
      open_url_(entries_[static_cast<std::size_t>(index)].url);
    }
    return;
  }

  if (command == kClear) {
    if (MessageBoxW(
            hwnd_,
            L"Clear all local browsing history?",
            L"CX Local History",
            MB_OKCANCEL | MB_ICONWARNING |
                MB_DEFBUTTON2) == IDOK) {
      history_.Clear();
      Refresh();
    }
    return;
  }

  if (command == kClose && hwnd_) {
    DestroyWindow(hwnd_);
  }
}

std::string HistoryDialog::QueryText() const {
  if (!query_) {
    return {};
  }

  const int length = GetWindowTextLengthW(query_);
  if (length <= 0) {
    return {};
  }
  std::wstring value(
      static_cast<std::size_t>(length) + 1, L'\0');

  const int copied = GetWindowTextW(
      query_, value.data(), length + 1);
  if (copied <= 0) {
    return {};
  }
  value.resize(static_cast<std::size_t>(copied));
  return WideToUtf8(value);
}

int HistoryDialog::SelectedIndex() const {
  if (!list_) {
    return -1;
  }
  const LRESULT selected =
      SendMessageW(list_, LB_GETCURSEL, 0, 0);
  return selected == LB_ERR
      ? -1
      : static_cast<int>(selected);
}

LRESULT CALLBACK HistoryDialog::WndProc(
    HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<HistoryDialog*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (message == WM_NCCREATE) {

    const auto* create =
        reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<HistoryDialog*>(
        create->lpCreateParams);
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
    self->query_ = nullptr;
    self->list_ = nullptr;
    return 0;
  }

  return DefWindowProcW(
      hwnd, message, wparam, lparam);
}

}  // namespace cx::browser
