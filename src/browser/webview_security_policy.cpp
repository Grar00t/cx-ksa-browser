#include "browser/webview_security_policy.h"

#include <algorithm>
#include <cwctype>

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

std::wstring Lower(std::wstring_view value) {
  std::wstring output(value);
  std::transform(
      output.begin(), output.end(), output.begin(),
      [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
      });
  return output;
}

}  // namespace

bool WebViewSecurityPolicy::IsAllowedResourceUrl(
    std::wstring_view url) {
  return StartsWithInsensitive(url, L"https://") ||
      StartsWithInsensitive(url, L"http://") ||
      StartsWithInsensitive(url, L"data:") ||
      StartsWithInsensitive(url, L"blob:") ||
      url == L"about:blank";
}

std::optional<std::wstring>
WebViewSecurityPolicy::OriginFromUrl(
    std::wstring_view url) {
  const auto separator = url.find(L"://");
  if (separator == std::wstring_view::npos ||
      separator == 0) {
    return std::nullopt;
  }

  const auto scheme = Lower(url.substr(0, separator));
  if (scheme != L"http" && scheme != L"https") {
    return std::nullopt;
  }

  const std::size_t authority_start = separator + 3;
  const std::size_t authority_end =
      url.find_first_of(L"/?#", authority_start);
  std::wstring authority = Lower(url.substr(
      authority_start,
      authority_end == std::wstring_view::npos
          ? std::wstring_view::npos
          : authority_end - authority_start));
  if (authority.empty() ||
      authority.find(L'@') != std::wstring::npos ||
      authority.find_first_of(L" 	
\") !=
          std::wstring::npos) {
    return std::nullopt;
  }

  if (scheme == L"http" &&
      authority.ends_with(L":80")) {
    authority.resize(authority.size() - 3);
  } else if (scheme == L"https" &&
             authority.ends_with(L":443")) {
    authority.resize(authority.size() - 4);
  }
  if (authority.empty()) {
    return std::nullopt;
  }
  return scheme + L"://" + authority;
}

bool WebViewSecurityPolicy::IsCrossOrigin(
    std::wstring_view current_url,
    std::wstring_view target_url) {
  const auto current = OriginFromUrl(current_url);
  const auto target = OriginFromUrl(target_url);
  return current.has_value() &&
      target.has_value() &&
      *current != *target;
}

}  // namespace cx::browser
