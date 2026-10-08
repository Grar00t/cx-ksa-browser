#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cx::browser {

class WebViewSecurityPolicy {
public:
  static bool IsAllowedResourceUrl(std::wstring_view url);
  static std::optional<std::wstring> OriginFromUrl(
      std::wstring_view url);
  static bool IsCrossOrigin(
      std::wstring_view current_url,
      std::wstring_view target_url);
};

}  // namespace cx::browser
