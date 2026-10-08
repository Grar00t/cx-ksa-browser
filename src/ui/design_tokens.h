#pragma once

#include <windows.h>

namespace cx::ui::design {

struct Color final {
  static constexpr COLORREF Background = RGB(11, 16, 32);
  static constexpr COLORREF Surface = RGB(18, 26, 46);
  static constexpr COLORREF SurfaceRaised = RGB(26, 37, 64);
  static constexpr COLORREF Input = RGB(10, 15, 28);
  static constexpr COLORREF Border = RGB(38, 52, 79);
  static constexpr COLORREF Text = RGB(232, 241, 255);
  static constexpr COLORREF MutedText = RGB(159, 176, 204);
  static constexpr COLORREF AccentTurquoise = RGB(31, 209, 198);
  static constexpr COLORREF AccentTurquoiseDim = RGB(19, 143, 137);
  static constexpr COLORREF AgentActive = RGB(125, 227, 255);
  static constexpr COLORREF Danger = RGB(255, 107, 122);
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
  inline static constexpr wchar_t ArabicFallbackFamily[] = L"Tahoma";
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
  static constexpr int Focus = 1;
};

struct Focus final {
  static constexpr COLORREF Primary = Color::AccentTurquoise;
  static constexpr COLORREF Secondary = Color::AccentTurquoiseDim;
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

struct ContextGraphLayout final {
  static constexpr int PanelWidth = 360;
  static constexpr int HeaderHeight = 62;
  static constexpr int FooterHeight = 28;
  static constexpr int NodeRadius = 5;
  static constexpr int ActiveNodeRadius = 8;
  static constexpr int LabelWidth = 220;
  static constexpr int PlotInset = Spacing::Xxl;
};

struct AgentWorkspaceLayout final {
  static constexpr int PanelWidth = 296;
  static constexpr int MinimumBrowserWidth = 480;
  static constexpr int HeaderHeight = 38;
  static constexpr int StopStripHeight = 46;
  static constexpr int ActivityRail = 2;
  static constexpr BYTE ShieldAlpha = 48;
  static constexpr int PanelInset = Spacing::Lg;
  static constexpr int LogMinimumHeight = 120;
};

struct ShellRect final {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  constexpr int Width() const {
    return right > left ? right - left : 0;
  }
  constexpr int Height() const {
    return bottom > top ? bottom - top : 0;
  }
};

struct BrowserShellLayout final {
  ShellRect browser;
  ShellRect agent;
  ShellRect graph;
  ShellRect status;
  ShellRect stop;
};

constexpr int LayoutMax(int left, int right) {
  return left > right ? left : right;
}

constexpr int LayoutMin(int left, int right) {
  return left < right ? left : right;
}

constexpr BrowserShellLayout ComputeBrowserShellLayout(
    int width, int height,
    bool agent_visible,
    bool graph_visible,
    bool emergency_stop_visible) {
  const int safe_width = LayoutMax(0, width);
  const int safe_height = LayoutMax(0, height);
  const int toolbar_y =
      Density::TabHeight + Spacing::Xxs;
  const int content_top =
      toolbar_y + Density::ControlHeight + Spacing::Xxs;
  const int status_top =
      LayoutMax(content_top, safe_height - Density::StatusHeight);
  const int stop_top = emergency_stop_visible
      ? LayoutMax(
            content_top,
            status_top - AgentWorkspaceLayout::StopStripHeight)
      : status_top;

  const int side_gap_count =
      (agent_visible ? 1 : 0) + (graph_visible ? 1 : 0);
  const int side_budget = LayoutMax(
      0,
      safe_width - AgentWorkspaceLayout::MinimumBrowserWidth -
          side_gap_count * Spacing::Sm);
  const int agent_width = agent_visible
      ? LayoutMin(
            AgentWorkspaceLayout::PanelWidth,
            graph_visible ? side_budget / 2 : side_budget)
      : 0;
  const int graph_width = graph_visible
      ? LayoutMin(
            ContextGraphLayout::PanelWidth,
            side_budget - agent_width)
      : 0;
  const int browser_left =
      agent_width > 0 ? agent_width + Spacing::Sm : 0;
  const int browser_right = LayoutMax(
      browser_left,
      safe_width - graph_width -
          (graph_width > 0 ? Spacing::Sm : 0));

  return BrowserShellLayout{
      ShellRect{browser_left, content_top,
                browser_right, stop_top},
      ShellRect{0, content_top,
                agent_width, stop_top},
      ShellRect{
          graph_width > 0 ? browser_right + Spacing::Sm : safe_width,
          content_top, safe_width, stop_top},
      ShellRect{0, status_top, safe_width, safe_height},
      ShellRect{0, stop_top, safe_width, status_top}};
}

struct Window final {
  static constexpr int AppWidth = 1280;
  static constexpr int AppHeight = 800;
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
