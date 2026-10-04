#pragma once

#include <windows.h>

namespace cx::ui::design {

struct Color final {
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

struct Spacing final {
  static constexpr int Xxs = 2;
  static constexpr int Xs = 4;
  static constexpr int Sm = 6;
  static constexpr int Md = 8;
  static constexpr int Lg = 12;
  static constexpr int Xl = 16;
  static constexpr int Xxl = 24;
};

struct Typography final {
  inline static constexpr wchar_t Family[] = L"Segoe UI";
  static constexpr int BodyHeight = -15;
  static constexpr int CompactHeight = -14;
  static constexpr int WeightNormal = FW_NORMAL;
  static constexpr int WeightSemibold = FW_SEMIBOLD;
};

struct Radius final {
  static constexpr int Control = 3;
  static constexpr int Surface = 4;
  static constexpr int Maximum = 4;
};

struct Border final {
  static constexpr int Standard = 1;
  static constexpr int Focus = 2;
};

struct Focus final {
  static constexpr COLORREF Primary = Color::Sand;
  static constexpr COLORREF Secondary = Color::Olive;
  static constexpr int TabUnderline = 2;
};

struct Density final {
  static constexpr int ControlHeight = 30;
  static constexpr int TabHeight = 34;
  static constexpr int TabItemWidth = 168;
  static constexpr int ToolbarHeight = 34;
  static constexpr int StatusHeight = 22;
  static constexpr int IconButtonWidth = 34;
  static constexpr int NavigationButtonWidth = 36;
  static constexpr int ReloadButtonWidth = 60;
  static constexpr int GoButtonWidth = 44;
  static constexpr int BookmarkButtonWidth = 78;
  static constexpr int MinimumInputWidth = 120;
  static constexpr int InputTextInset = 10;
  static constexpr int SettingsButtonHeight = 32;
  static constexpr int SettingsCheckboxHeight = 28;
};

struct Window final {
  static constexpr int AppWidth = 1024;
  static constexpr int AppHeight = 768;
  static constexpr int SettingsWidth = 900;
  static constexpr int SettingsHeight = 680;
};

struct SettingsLayout final {
  static constexpr int OuterInset = Spacing::Xl;
  static constexpr int DashboardHeight = 92;
  static constexpr int TabsTop =
      OuterInset + DashboardHeight + Spacing::Lg;
  static constexpr int PageTop =
      TabsTop + Density::TabHeight +
      Spacing::Xl + Spacing::Xxs;
  static constexpr int PageLabelStart =
      Spacing::Xxl + Spacing::Xl;
  static constexpr int PageContentStart =
      PageLabelStart + Spacing::Md;
  static constexpr int IntroHeight = 36;
  static constexpr int ListHeight = 240;
  static constexpr int DataSummaryHeight = 150;
};

static_assert(Radius::Control <= Radius::Maximum);
static_assert(Radius::Surface <= Radius::Maximum);
static_assert(Border::Standard == 1);
static_assert(Density::ControlHeight <= 32);

}  // namespace cx::ui::design
