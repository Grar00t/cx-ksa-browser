#include "browser/bookmarks_dialog.h"

#include "browser/bookmark_service.h"

#include <string>
#include <utility>

namespace cx::browser {
namespace {

constexpr wchar_t kClassName[] =
    L"CXBuildBookmarksDialog";
constexpr WORD kOpen = 5301;
constexpr WORD kRemove = 5302;
constexpr WORD kClose = 5303;

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

}  // namespace

BookmarksDialog::BookmarksDialog(
    BookmarkService& bookmarks,
    std::function<void(std::string)> open_url)
    : bookmarks_(bookmarks),
      open_url_(std::move(open_url)) {}

void BookmarksDialog::Show(HWND owner) {
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
  window_class.lpfnWndProc = &BookmarksDialog::WndProc;
  window_class.lpszClassName = kClassName;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground =
      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW,
      kClassName,
      L"CX Local Bookmarks",
      WS_OVERLAPPED | WS_CAPTION |
          WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT,
      720, 450,
      owner_, nullptr, instance, this);
  if (!hwnd_) {
    return;
  }

  CreateControls();
  Refresh();
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
}

void BookmarksDialog::RefreshIfOpen() {

  if (hwnd_) {
    Refresh();
  }
}

void BookmarksDialog::CreateControls() {
  list_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
      WS_CHILD | WS_VISIBLE | WS_VSCROLL |
          LBS_NOTIFY,
      14, 14, 676, 330,
      hwnd_, nullptr, nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Open",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      14, 360, 90, 30,
      hwnd_, reinterpret_cast<HMENU>(kOpen),
      nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Remove",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      114, 360, 90, 30,
      hwnd_, reinterpret_cast<HMENU>(kRemove),
      nullptr, nullptr);

  CreateWindowExW(
      0, L"BUTTON", L"Close",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      600, 360, 90, 30,
      hwnd_, reinterpret_cast<HMENU>(kClose),
      nullptr, nullptr);
}

void BookmarksDialog::Refresh() {
  entries_ = bookmarks_.List();
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

void BookmarksDialog::HandleCommand(WORD command) {
  const int index = SelectedIndex();

  if (command == kOpen) {
    if (index >= 0 &&
        static_cast<std::size_t>(index) < entries_.size()) {
      open_url_(entries_[static_cast<std::size_t>(index)].url);
    }
    return;
  }

  if (command == kRemove) {
    if (index < 0 ||
        static_cast<std::size_t>(index) >= entries_.size()) {
      return;
    }

    const auto& entry =
        entries_[static_cast<std::size_t>(index)];
    const std::wstring prompt =
        L"Remove bookmark for\n" +
        Utf8ToWide(entry.url) + L"?";
    if (MessageBoxW(
            hwnd_, prompt.c_str(),
            L"CX Local Bookmarks",
            MB_OKCANCEL | MB_ICONWARNING |
                MB_DEFBUTTON2) == IDOK) {
      bookmarks_.Remove(entry.id);
      Refresh();
    }
    return;
  }

  if (command == kClose && hwnd_) {
    DestroyWindow(hwnd_);
  }
}

int BookmarksDialog::SelectedIndex() const {

  if (!list_) {
    return -1;
  }
  const LRESULT selected =
      SendMessageW(list_, LB_GETCURSEL, 0, 0);
  return selected == LB_ERR
      ? -1
      : static_cast<int>(selected);
}

LRESULT CALLBACK BookmarksDialog::WndProc(
    HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<BookmarksDialog*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (message == WM_NCCREATE) {
    const auto* create =
        reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<BookmarksDialog*>(
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
    self->list_ = nullptr;
    return 0;
  }

  return DefWindowProcW(
      hwnd, message, wparam, lparam);
}

}  // namespace cx::browser
