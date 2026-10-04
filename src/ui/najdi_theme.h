#pragma once

#include <windows.h>

namespace cx::ui::theme {

struct Palette final {
  static constexpr COLORREF Background = RGB(30, 28, 25);
  static constexpr COLORREF Surface = RGB(40, 37, 31);
  static constexpr COLORREF SurfaceRaised = RGB(49, 45, 37);
  static constexpr COLORREF Input = RGB(24, 23, 20);
  static constexpr COLORREF Border = RGB(73, 66, 55);
  static constexpr COLORREF Text = RGB(242, 238, 228);
  static constexpr COLORREF MutedText = RGB(183, 173, 154);
  static constexpr COLORREF Sand = RGB(199, 169, 107);
  static constexpr COLORREF Olive = RGB(127, 133, 87);
};

HBRUSH BackgroundBrush() noexcept;
HBRUSH SurfaceBrush() noexcept;
HBRUSH InputBrush() noexcept;
HFONT UiFont() noexcept;
HFONT UiFontSemibold() noexcept;

void ApplyWindowChrome(HWND window) noexcept;
void ApplyFont(HWND window) noexcept;
void ApplyFontToChildren(HWND parent) noexcept;
void StyleTabControl(HWND tabs) noexcept;

LRESULT HandleControlColor(
    UINT message,
    WPARAM wparam,
    LPARAM lparam) noexcept;

bool DrawOwnerItem(
    const DRAWITEMSTRUCT* item) noexcept;

}  // namespace cx::ui::theme
