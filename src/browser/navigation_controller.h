#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cx::browser {

class HistoryService;
class TabManager;

class NavigationSurface {
public:
  virtual ~NavigationSurface() = default;
  virtual bool NavigateTo(
      std::wstring_view url) = 0;
  virtual bool ReloadPage() = 0;
};

class NavigationController {
public:
  NavigationController(
      TabManager& tabs,
      HistoryService& history);

  void AttachSurface(
      NavigationSurface* surface) noexcept;

  bool ActivateTab(std::int64_t tab_id);
  bool NavigateAddress(std::wstring_view input);
  bool NavigateUrl(std::string_view url);
  bool Back();
  bool Forward();
  bool Reload();

  bool CanGoBack() const;
  bool CanGoForward() const;

  void OnNavigationCompleted(
      bool success,
      std::string_view final_url,
      std::string_view title);
  void ForgetTab(std::int64_t tab_id);

  static std::optional<std::string> NormalizeAddress(
      std::wstring_view input);
  static bool IsAllowedUrl(
      std::wstring_view url);

private:
  struct Stack {
    std::vector<std::string> entries;

    std::size_t index = 0;
  };

  Stack& EnsureStack(std::int64_t tab_id);
  const Stack* ActiveStack() const;
  bool NavigateStackEntry(
      Stack& stack,
      std::size_t index,
      bool record_visit);
  bool SendNavigate(
      std::string_view url,
      bool record_visit);
  static std::wstring Utf8ToWide(
      std::string_view value);

  TabManager& tabs_;
  HistoryService& history_;
  NavigationSurface* surface_ = nullptr;
  std::unordered_map<std::int64_t, Stack> stacks_;

  std::int64_t pending_tab_id_ = 0;
  bool pending_record_visit_ = false;
};

}  // namespace cx::browser
