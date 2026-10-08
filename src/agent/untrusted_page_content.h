#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cx::agent {

class UntrustedPageContent {
public:
  static constexpr std::size_t kMaxBytes = 1024 * 1024;

  static std::optional<UntrustedPageContent> Create(
      std::int64_t tab_id,
      std::string_view origin,
      std::string_view content);

  std::int64_t tab_id() const noexcept;
  const std::string& origin() const noexcept;
  const std::string& content() const noexcept;
  std::string SerializeAsData() const;

private:
  UntrustedPageContent(
      std::int64_t tab_id,
      std::string origin,
      std::string content);

  std::int64_t tab_id_;
  std::string origin_;
  std::string content_;
};

bool IsSensitiveAction(std::string_view action);
bool IsAgentVisibleField(
    std::string_view input_type,
    std::string_view autocomplete);

}  // namespace cx::agent
