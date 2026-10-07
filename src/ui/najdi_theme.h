#pragma once

#include "ui/design_tokens.h"

#include <windows.h>

namespace cx::ui::theme {

using Palette = design::Color;

HBRUSH BackgroundBrush() noexcept;
HBRUSH SurfaceBrush() noexcept;
HBRUSH InputBrush() noexcept;
HFONT UiFont() noexcept;
HFONT UiFontSemibold() noexcept;

void ApplyWindowChrome(HWND window) noexcept;
void ApplyFont(HWND window) noexcept;
void ApplyFontToChildren(HWND parent) noexcept;
void ApplyLayoutDirection(HWND window, bool right_to_left) noexcept;
void MarkPrimaryAction(HWND button) noexcept;
void StyleBorderedSurface(HWND window) noexcept;
void StyleTabControl(HWND tabs) noexcept;

LRESULT HandleControlColor(
    UINT message,
    WPARAM wparam,
    LPARAM lparam) noexcept;

bool DrawOwnerItem(
    const DRAWITEMSTRUCT* item) noexcept;

}  // namespace cx::ui::theme
