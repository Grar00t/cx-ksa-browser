#include "agent/untrusted_page_content.h"

#include <array>

namespace cx::agent {
namespace {

std::string EscapeJson(std::string_view value) {
  std::string output;
  output.reserve(value.size() + 16);
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (ch < 0x20) {
          return {};
        }
        output.push_back(static_cast<char>(ch));
        break;
    }
  }
  return output;
}

}  // namespace

UntrustedPageContent::UntrustedPageContent(
    std::int64_t tab_id,
    std::string origin,
    std::string content)
    : tab_id_(tab_id),
      origin_(std::move(origin)),
      content_(std::move(content)) {}

std::optional<UntrustedPageContent>
UntrustedPageContent::Create(
    std::int64_t tab_id,
    std::string_view origin,
    std::string_view content) {
  if (tab_id <= 0 ||
      origin.empty() ||
      origin.size() > 2048 ||
      content.size() > kMaxBytes ||
      (!origin.starts_with("https://") &&
       !origin.starts_with("http://"))) {
    return std::nullopt;
  }
  return UntrustedPageContent(
      tab_id,
      std::string(origin),
      std::string(content));
}

std::int64_t UntrustedPageContent::tab_id() const noexcept {
  return tab_id_;
}

const std::string& UntrustedPageContent::origin() const noexcept {
  return origin_;
}

const std::string& UntrustedPageContent::content() const noexcept {
  return content_;
}

std::string UntrustedPageContent::SerializeAsData() const {
  const std::string escaped_origin = EscapeJson(origin_);
  const std::string escaped_content = EscapeJson(content_);
  if (escaped_origin.empty() || escaped_content.empty()) {
    return {};
  }
  return "{\"trust\":\"untrusted\",\"role\":\"data\","
      "\"tab_id\":" + std::to_string(tab_id_) +
      ",\"origin\":\"" + escaped_origin +
      "\",\"content\":\"" + escaped_content + "\"}";
}

bool IsSensitiveAction(std::string_view action) {
  constexpr std::array<std::string_view, 6> sensitive{{
      "form.submit",
      "purchase.confirm",
      "file.upload",
      "file.download",
      "credential.fill",
      "browser.navigate_cross_origin",
  }};
  for (const auto candidate : sensitive) {
    if (candidate == action) {
      return true;
    }
  }
  return false;
}

bool IsAgentVisibleField(
    std::string_view input_type,
    std::string_view autocomplete) {
  return input_type != "password" &&
      autocomplete != "current-password" &&
      autocomplete != "new-password" &&
      autocomplete != "one-time-code";
}

}  // namespace cx::agent
