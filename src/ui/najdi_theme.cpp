#include "ui/najdi_theme.h"

#include <commctrl.h>

#include <array>
#include <iterator>

namespace cx::ui::theme {
namespace {

constexpr UINT_PTR kTabSubclassId = 0x43584E4A;
constexpr UINT_PTR kSurfaceSubclassId = 0x43585346;
constexpr wchar_t kPrimaryActionProperty[] =
    L"CXBuild.PrimaryAction";

HBRUSH Brush(COLORREF color) noexcept {
  static const std::array<COLORREF, 3> colors{
      Palette::Background,
      Palette::Surface,
      Palette::Input};
  static const std::array<HBRUSH, 3> brushes{
      CreateSolidBrush(colors[0]),
      CreateSolidBrush(colors[1]),
      CreateSolidBrush(colors[2])};

  for (std::size_t i = 0; i < colors.size(); ++i) {
    if (colors[i] == color) {
      return brushes[i];
    }
  }
  return brushes[0];
}

HFONT Font(int weight) noexcept {
  static HFONT normal = CreateFontW(
      design::Typography::BodyHeight, 0, 0, 0,
      design::Typography::WeightNormal,
      FALSE, FALSE, FALSE,
      DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE,
      design::Typography::Family);
  static HFONT semibold = CreateFontW(
      design::Typography::BodyHeight, 0, 0, 0,
      design::Typography::WeightSemibold,
      FALSE, FALSE, FALSE,
      DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE,
      design::Typography::Family);
  return weight >= FW_SEMIBOLD ? semibold : normal;
}

void PaintTabControl(HWND tabs, HDC dc) noexcept {
  RECT client{};
  GetClientRect(tabs, &client);
  FillRect(dc, &client, BackgroundBrush());

  const int selected = TabCtrl_GetCurSel(tabs);
  const int count = TabCtrl_GetItemCount(tabs);
  for (int index = 0; index < count; ++index) {
    RECT item{};
    if (!TabCtrl_GetItemRect(tabs, index, &item)) {
      continue;
    }

    const bool active = index == selected;
    HBRUSH fill = CreateSolidBrush(
        active ? Palette::SurfaceRaised : Palette::Surface);
    HPEN border = CreatePen(
        PS_SOLID,
        design::Border::Standard,
        active ? design::Focus::Primary : Palette::Border);
    const auto previous_pen = SelectObject(dc, border);
    const auto previous_brush = SelectObject(dc, fill);
    RoundRect(
        dc,
        item.left,
        item.top,
        item.right,
        item.bottom,
        design::Radius::Control * 2,
        design::Radius::Control * 2);
    SelectObject(dc, previous_brush);
    SelectObject(dc, previous_pen);
    DeleteObject(border);
    DeleteObject(fill);

    wchar_t text[256]{};
    TCITEMW tab{};
    tab.mask = TCIF_TEXT;
    tab.pszText = text;
    tab.cchTextMax = static_cast<int>(std::size(text));
    TabCtrl_GetItem(tabs, index, &tab);

    RECT text_rect = item;
    InflateRect(
        &text_rect,
        -design::Spacing::Md,
        0);
    const auto previous_font = SelectObject(
        dc, active ? UiFontSemibold() : UiFont());
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(
        dc,
        active ? Palette::Text : Palette::MutedText);
    DrawTextW(
        dc,
        text,
        -1,
        &text_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE |
            DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous_font);

    if (active) {
      RECT accent = item;
      accent.top =
          accent.bottom - design::Focus::TabUnderline;
      HBRUSH accent_brush =
          CreateSolidBrush(Palette::Sand);
      FillRect(dc, &accent, accent_brush);
      DeleteObject(accent_brush);
    }
  }
}

LRESULT CALLBACK TabSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR,
    DWORD_PTR) {
  if (message == WM_ERASEBKGND) {
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(
        reinterpret_cast<HDC>(wparam),
        &client,
        BackgroundBrush());
    return TRUE;
  }

  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    PaintTabControl(hwnd, dc);
    EndPaint(hwnd, &paint);
    return 0;
  }

  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(
        hwnd, TabSubclassProc, kTabSubclassId);
  }

  return DefSubclassProc(
      hwnd, message, wparam, lparam);
}

void PaintSurfaceBorder(HWND hwnd) noexcept {
  HDC dc = GetDC(hwnd);
  if (!dc) {
    return;
  }
  RECT rect{};
  GetClientRect(hwnd, &rect);
  const bool focused = GetFocus() == hwnd;
  HPEN pen = CreatePen(
      PS_SOLID,
      design::Border::Standard,
      focused ? design::Focus::Primary : Palette::Border);
  const auto previous_pen = SelectObject(dc, pen);
  const auto previous_brush =
      SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
  SelectObject(dc, previous_brush);
  SelectObject(dc, previous_pen);
  DeleteObject(pen);
  ReleaseDC(hwnd, dc);
}

LRESULT CALLBACK SurfaceSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR,
    DWORD_PTR) {
  const LRESULT result =
      DefSubclassProc(hwnd, message, wparam, lparam);
  if (message == WM_PAINT ||
      message == WM_SETFOCUS ||
      message == WM_KILLFOCUS) {
    PaintSurfaceBorder(hwnd);
  }
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(
        hwnd, SurfaceSubclassProc, kSurfaceSubclassId);
  }
  return result;
}

}  // namespace

HBRUSH BackgroundBrush() noexcept {
  return Brush(Palette::Background);
}

HBRUSH SurfaceBrush() noexcept {
  return Brush(Palette::Surface);
}

HBRUSH InputBrush() noexcept {
  return Brush(Palette::Input);
}

HFONT UiFont() noexcept {
  return Font(FW_NORMAL);
}

HFONT UiFontSemibold() noexcept {
  return Font(FW_SEMIBOLD);
}

void ApplyWindowChrome(HWND window) noexcept {
  if (!window) {
    return;
  }

  using DwmSetWindowAttributeFn =
      HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
  HMODULE module = LoadLibraryW(L"dwmapi.dll");
  if (!module) {
    return;
  }

  const auto set_attribute =
      reinterpret_cast<DwmSetWindowAttributeFn>(
          GetProcAddress(module, "DwmSetWindowAttribute"));
  if (set_attribute) {
    const BOOL enabled = TRUE;
    if (FAILED(set_attribute(
            window, 20, &enabled, sizeof(enabled)))) {
      set_attribute(
          window, 19, &enabled, sizeof(enabled));
    }
  }
  FreeLibrary(module);
}

void ApplyFont(HWND window) noexcept {
  if (window) {
    SendMessageW(
        window,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(UiFont()),
        TRUE);
  }
}

void ApplyFontToChildren(HWND parent) noexcept {
  if (!parent) {
    return;
  }
  ApplyFont(parent);
  EnumChildWindows(
      parent,
      [](HWND child, LPARAM) -> BOOL {
        ApplyFont(child);
        return TRUE;
      },
      0);
}

void ApplyLayoutDirection(
    HWND window,
    bool right_to_left) noexcept {
  if (!window) {
    return;
  }
  LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
  const LONG_PTR rtl_flags =
      WS_EX_LAYOUTRTL | WS_EX_RTLREADING;
  style = right_to_left
      ? style | rtl_flags
      : style & ~rtl_flags;
  SetWindowLongPtrW(window, GWL_EXSTYLE, style);
  SetWindowPos(
      window, nullptr, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
          SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void MarkPrimaryAction(HWND button) noexcept {
  if (button) {
    SetPropW(
        button,
        kPrimaryActionProperty,
        reinterpret_cast<HANDLE>(1));
  }
}

void StyleBorderedSurface(HWND window) noexcept {
  if (!window) {
    return;
  }
  SetWindowSubclass(
      window,
      SurfaceSubclassProc,
      kSurfaceSubclassId,
      0);
  InvalidateRect(window, nullptr, TRUE);
}

void StyleTabControl(HWND tabs) noexcept {
  if (!tabs) {
    return;
  }
  SetWindowSubclass(
      tabs,
      TabSubclassProc,
      kTabSubclassId,
      0);
  InvalidateRect(tabs, nullptr, TRUE);
}

LRESULT HandleControlColor(
    UINT message,
    WPARAM wparam,
    LPARAM) noexcept {
  HDC dc = reinterpret_cast<HDC>(wparam);
  SetTextColor(dc, Palette::Text);

  if (message == WM_CTLCOLOREDIT ||
      message == WM_CTLCOLORLISTBOX) {
    SetBkColor(dc, Palette::Input);
    return reinterpret_cast<LRESULT>(InputBrush());
  }

  SetBkColor(dc, Palette::Background);
  return reinterpret_cast<LRESULT>(BackgroundBrush());
}

bool DrawOwnerItem(
    const DRAWITEMSTRUCT* item) noexcept {
  if (!item || !item->hDC || !item->hwndItem ||
      item->CtlType != ODT_BUTTON) {
    return false;
  }

  const bool pressed =
      (item->itemState & ODS_SELECTED) != 0;
  const bool disabled =
      (item->itemState & ODS_DISABLED) != 0;
  const bool focused =
      (item->itemState & ODS_FOCUS) != 0;
  const bool primary =
      GetPropW(item->hwndItem, kPrimaryActionProperty) != nullptr;

  HBRUSH fill = CreateSolidBrush(
      pressed ? Palette::SurfaceRaised : Palette::Surface);
  HPEN pen = CreatePen(
      PS_SOLID,
      focused
          ? design::Border::Focus
          : design::Border::Standard,
      focused
          ? design::Focus::Primary
          : primary
              ? design::Focus::Secondary
              : Palette::Border);
  const auto old_pen = SelectObject(item->hDC, pen);
  const auto old_brush = SelectObject(item->hDC, fill);
  RoundRect(
      item->hDC,
      item->rcItem.left,
      item->rcItem.top,
      item->rcItem.right,
      item->rcItem.bottom,
      design::Radius::Control * 2,
      design::Radius::Control * 2);
  SelectObject(item->hDC, old_brush);
  SelectObject(item->hDC, old_pen);
  DeleteObject(pen);
  DeleteObject(fill);

  wchar_t text[256]{};
  GetWindowTextW(
      item->hwndItem,
      text,
      static_cast<int>(std::size(text)));
  RECT text_rect = item->rcItem;
  const auto previous_font =
      SelectObject(item->hDC, UiFontSemibold());
  SetBkMode(item->hDC, TRANSPARENT);
  SetTextColor(
      item->hDC,
      disabled ? Palette::MutedText : Palette::Text);
  DrawTextW(
      item->hDC,
      text,
      -1,
      &text_rect,
      DT_CENTER | DT_VCENTER | DT_SINGLELINE |
          DT_END_ELLIPSIS | DT_NOPREFIX);
  SelectObject(item->hDC, previous_font);
  return true;
}

}  // namespace cx::ui::theme
