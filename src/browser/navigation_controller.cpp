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

bool EqualsInsensitive(
    std::wstring_view value,
    std::wstring_view expected) {
  return value.size() == expected.size() &&
      StartsWithInsensitive(value, expected);
}

bool IsLocalFileUrl(std::wstring_view url) {
  constexpr std::wstring_view kFilePrefix = L"file://";
  constexpr std::wstring_view kLocalhost = L"localhost";

  if (!StartsWithInsensitive(url, kFilePrefix)) {
    return false;
  }

  const auto target = url.substr(kFilePrefix.size());
  if (target.empty()) {
    return false;
  }

  if (target.front() == L'/') {
    return true;
  }

  if (!StartsWithInsensitive(target, kLocalhost)) {
    return false;
  }

  return target.size() == kLocalhost.size() ||
      (target.size() > kLocalhost.size() &&
       target[kLocalhost.size()] == L'/');
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

  const auto active = tabs_.active_tab();
  if (!active.has_value()) {
    return false;
  }

  pending_intents_.push_back(NavigationIntent{
      active->id, true, false, active->url});
  if (!surface_->ReloadPage()) {
    pending_intents_.pop_back();
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

void NavigationController::OnNavigationStarted(
    std::uint64_t navigation_id,
    std::string_view target_url) {
  if (navigation_id == 0) {
    return;
  }

  const auto existing = inflight_navigations_.find(navigation_id);
  if (existing != inflight_navigations_.end()) {
    if (!target_url.empty()) {
      existing->second.requested_url = std::string(target_url);
    }
    return;
  }

  NavigationIntent intent;
  if (!pending_intents_.empty()) {
    intent = std::move(pending_intents_.front());
    pending_intents_.pop_front();
    if (!target_url.empty()) {
      intent.requested_url = std::string(target_url);
    }
  } else {
    const auto active = tabs_.active_tab();
    if (!active.has_value()) {
      return;
    }
    intent.tab_id = active->id;
    intent.record_visit = true;
    intent.append_stack_entry = true;
    intent.requested_url = target_url.empty()
        ? active->url
        : std::string(target_url);
  }

  inflight_navigations_.emplace(
      navigation_id, std::move(intent));
}

void NavigationController::OnNavigationCompleted(
    std::uint64_t navigation_id,
    bool success,
    std::string_view final_url,
    std::string_view title) {
  const auto pending =
      inflight_navigations_.find(navigation_id);
  if (pending == inflight_navigations_.end()) {
    return;
  }

  const NavigationIntent intent = pending->second;
  inflight_navigations_.erase(pending);
  if (!success) {
    return;
  }

  const auto tab = std::find_if(
      tabs_.tabs().begin(), tabs_.tabs().end(),
      [&intent](const auto& candidate) {
        return candidate.id == intent.tab_id;
      });
  if (tab == tabs_.tabs().end()) {
    return;
  }

  const std::string effective_url =
      final_url.empty()
          ? (intent.requested_url.empty()
                 ? tab->url
                 : intent.requested_url)
          : std::string(final_url);
  const std::string effective_title =
      title.empty()
          ? (tab->title.empty()
                 ? effective_url
                 : tab->title)
          : std::string(title);

  if (!tabs_.UpdateTab(
          intent.tab_id,
          effective_url,
          effective_title)) {
    return;
  }

  auto& stack = EnsureStack(intent.tab_id);
  if (intent.append_stack_entry) {
    if (!stack.entries.empty() &&
        stack.index + 1 < stack.entries.size()) {
      stack.entries.erase(
          stack.entries.begin() +
              static_cast<std::ptrdiff_t>(stack.index + 1),
          stack.entries.end());
    }
    if (stack.entries.empty() ||
        stack.entries[stack.index] != effective_url) {
      stack.entries.push_back(effective_url);
      stack.index = stack.entries.size() - 1;
    }
  } else if (stack.entries.empty()) {
    stack.entries.push_back(effective_url);
    stack.index = 0;
  } else {
    stack.entries[stack.index] = effective_url;
  }

  if (intent.record_visit) {
    history_.RecordVisit(effective_url, effective_title);
  }
}

void NavigationController::ForgetTab(
    std::int64_t tab_id) {
  stacks_.erase(tab_id);
  std::erase_if(
      pending_intents_,
      [tab_id](const NavigationIntent& intent) {
        return intent.tab_id == tab_id;
      });
  std::erase_if(
      inflight_navigations_,
      [tab_id](const auto& entry) {
        return entry.second.tab_id == tab_id;
      });
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

  const auto active = tabs_.active_tab();
  if (!active.has_value()) {
    return false;
  }

  pending_intents_.push_back(NavigationIntent{
      active->id, record_visit, false, std::string(url)});
  if (!surface_->NavigateTo(wide)) {
    pending_intents_.pop_back();
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
         IsLocalFileUrl(url) ||
         EqualsInsensitive(
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
