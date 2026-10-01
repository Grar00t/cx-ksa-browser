#include "browser/navigation_controller.h"

#include "browser/history_service.h"
#include "browser/tab_manager.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <utility>

namespace cx::browser {
namespace {

bool StartsWithInsensitive(
    std::wstring_view value,
    std::wstring_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (std::towlower(value[i]) !=
        std::towlower(prefix[i])) {
      return false;
    }
  }
  return true;
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

NavigationController::NavigationController(
    TabManager& tabs,
    HistoryService& history)
    : tabs_(tabs), history_(history) {}

void NavigationController::AttachSurface(
    NavigationSurface* surface) noexcept {
  surface_ = surface;
}

bool NavigationController::ActivateTab(
    std::int64_t tab_id) {
  if (!tabs_.ActivateTab(tab_id)) {
    return false;
  }

  auto& stack = EnsureStack(tab_id);
  if (stack.entries.empty()) {
    return false;
  }

  if (!surface_) {
    return true;
  }

  return SendNavigate(
      stack.entries[stack.index], false);
}

bool NavigationController::NavigateAddress(
    std::wstring_view input) {
  const auto normalized = NormalizeAddress(input);
  if (!normalized.has_value()) {
    return false;
  }
  return NavigateUrl(*normalized);
}

bool NavigationController::NavigateUrl(
    std::string_view url) {
  const auto active = tabs_.active_tab();
  if (!active.has_value() || !surface_) {
    return false;
  }

  const std::wstring wide = Utf8ToWide(url);
  if (wide.empty() || !IsAllowedUrl(wide)) {
    return false;
  }

  auto& stack = EnsureStack(active->id);
  const std::string previous_url = active->url;

  if (!tabs_.UpdateTab(
          active->id, url, active->title)) {
    return false;
  }
  if (!SendNavigate(url, true)) {
    tabs_.UpdateTab(
        active->id, previous_url, active->title);
    return false;
  }

  if (!stack.entries.empty() &&
      stack.index + 1 < stack.entries.size()) {
    stack.entries.erase(
        stack.entries.begin() +
            static_cast<std::ptrdiff_t>(stack.index + 1),
        stack.entries.end());
  }

  stack.entries.push_back(std::string(url));
  stack.index = stack.entries.size() - 1;
  return true;
}

bool NavigationController::Back() {
  auto* stack = const_cast<Stack*>(ActiveStack());
  if (!stack || stack->index == 0) {
    return false;
  }
  return NavigateStackEntry(
      *stack, stack->index - 1, true);
}

bool NavigationController::Forward() {
  auto* stack = const_cast<Stack*>(ActiveStack());
  if (!stack ||
      stack->index + 1 >= stack->entries.size()) {
    return false;
  }
  return NavigateStackEntry(
      *stack, stack->index + 1, true);
}

bool NavigationController::Reload() {
  if (!surface_ || !tabs_.active_tab().has_value()) {
    return false;
  }

  pending_tab_id_ = tabs_.active_tab_id();

  pending_record_visit_ = true;
  if (!surface_->ReloadPage()) {
    pending_tab_id_ = 0;
    pending_record_visit_ = false;
    return false;
  }
  return true;
}

bool NavigationController::CanGoBack() const {
  const auto* stack = ActiveStack();
  return stack && stack->index > 0;
}

bool NavigationController::CanGoForward() const {
  const auto* stack = ActiveStack();
  return stack &&
      stack->index + 1 < stack->entries.size();
}

void NavigationController::OnNavigationCompleted(
    bool success,
    std::string_view final_url,
    std::string_view title) {
  const auto active = tabs_.active_tab();
  if (!active.has_value()) {
    pending_tab_id_ = 0;
    pending_record_visit_ = false;
    return;
  }

  if (!success) {
    pending_tab_id_ = 0;
    pending_record_visit_ = false;
    return;
  }

  const std::string effective_url =
      final_url.empty()
          ? active->url
          : std::string(final_url);
  const std::string effective_title =
      title.empty()
          ? (active->title.empty()
                 ? effective_url
                 : active->title)
          : std::string(title);

  tabs_.UpdateTab(
      active->id,
      effective_url,
      effective_title);

  auto& stack = EnsureStack(active->id);
  if (stack.entries.empty()) {
    stack.entries.push_back(effective_url);
    stack.index = 0;
  } else {
    stack.entries[stack.index] = effective_url;
  }

  if (pending_record_visit_ &&
      pending_tab_id_ == active->id) {
    history_.RecordVisit(
        effective_url, effective_title);
  }

  pending_tab_id_ = 0;
  pending_record_visit_ = false;
}

void NavigationController::ForgetTab(
    std::int64_t tab_id) {
  stacks_.erase(tab_id);
  if (pending_tab_id_ == tab_id) {
    pending_tab_id_ = 0;
    pending_record_visit_ = false;
  }
}

NavigationController::Stack&
NavigationController::EnsureStack(
    std::int64_t tab_id) {
  auto [iterator, inserted] =
      stacks_.try_emplace(tab_id);
  if (inserted || iterator->second.entries.empty()) {
    for (const auto& tab : tabs_.tabs()) {
      if (tab.id == tab_id) {

        iterator->second.entries = {
            tab.url.empty()
                ? std::string("about:blank")
                : tab.url};
        iterator->second.index = 0;
        break;
      }
    }
  }
  return iterator->second;
}

const NavigationController::Stack*
NavigationController::ActiveStack() const {
  const auto iterator =
      stacks_.find(tabs_.active_tab_id());
  if (iterator == stacks_.end()) {
    return nullptr;
  }
  return &iterator->second;
}

bool NavigationController::NavigateStackEntry(
    Stack& stack,
    std::size_t index,
    bool record_visit) {
  const auto active = tabs_.active_tab();
  if (!active.has_value() || !surface_ ||
      index >= stack.entries.size()) {
    return false;
  }

  const std::string previous_url = active->url;
  const std::string& target = stack.entries[index];

  if (!tabs_.UpdateTab(
          active->id, target, active->title)) {
    return false;
  }
  if (!SendNavigate(target, record_visit)) {
    tabs_.UpdateTab(
        active->id, previous_url, active->title);
    return false;
  }

  stack.index = index;
  return true;
}

bool NavigationController::SendNavigate(
    std::string_view url,
    bool record_visit) {
  if (!surface_) {
    return false;
  }

  const std::wstring wide = Utf8ToWide(url);
  if (wide.empty() || !IsAllowedUrl(wide)) {
    return false;
  }

  pending_tab_id_ = tabs_.active_tab_id();
  pending_record_visit_ = record_visit;

  if (!surface_->NavigateTo(wide)) {
    pending_tab_id_ = 0;
    pending_record_visit_ = false;
    return false;
  }
  return true;
}

std::optional<std::string>
NavigationController::NormalizeAddress(
    std::wstring_view input) {
  std::size_t first = 0;
  while (first < input.size() &&
         std::iswspace(input[first])) {
    ++first;
  }

  std::size_t last = input.size();
  while (last > first &&
         std::iswspace(input[last - 1])) {
    --last;
  }
  if (first == last) {
    return std::nullopt;
  }

  std::wstring value(
      input.substr(first, last - first));
  for (const wchar_t ch : value) {
    if (ch < 0x20) {
      return std::nullopt;
    }
  }

  if (!IsAllowedUrl(value)) {
    const auto colon = value.find(L':');
    if (colon != std::wstring::npos) {
      const auto slash = value.find(L'/');
      const auto port_end =
          slash == std::wstring::npos
              ? value.size()
              : slash;
      const bool numeric_port =
          colon > 0 &&
          colon + 1 < port_end &&
          std::all_of(
              value.begin() +
                  static_cast<std::ptrdiff_t>(colon + 1),
              value.begin() +
                  static_cast<std::ptrdiff_t>(port_end),
              [](wchar_t ch) {
                return std::iswdigit(ch) != 0;
              });
      if (!numeric_port) {
        return std::nullopt;
      }
    }
    value = L"https://" + value;
  }

  if (!IsAllowedUrl(value)) {
    return std::nullopt;
  }

  const std::string utf8 = WideToUtf8(value);
  if (utf8.empty()) {
    return std::nullopt;
  }
  return utf8;
}

bool NavigationController::IsAllowedUrl(
    std::wstring_view url) {

  return StartsWithInsensitive(
             url, L"https://") ||
         StartsWithInsensitive(
             url, L"http://") ||
         StartsWithInsensitive(
             url, L"file://") ||
         StartsWithInsensitive(
             url, L"about:blank");
}

std::wstring NavigationController::Utf8ToWide(
    std::string_view value) {
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

}  // namespace cx::browser
