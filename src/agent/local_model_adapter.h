#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace cx::agent {

struct LocalModelPolicy {
  bool enabled = false;
  bool allow_non_loopback = false;
  bool non_loopback_warning_acknowledged = false;
};

class LocalModelAdapter {
public:
  LocalModelAdapter(
      std::string endpoint,
      std::string model,
      LocalModelPolicy policy = {});

  static bool IsLoopbackEndpoint(
      std::string_view endpoint) noexcept;
  bool Complete(
      std::string_view prompt,
      std::string* response,
      std::chrono::milliseconds timeout =
          std::chrono::seconds(30)) const;
  bool enabled() const noexcept;
  bool uses_explicit_non_loopback_override() const noexcept;

private:
  std::string endpoint_;
  std::string model_;
  LocalModelPolicy policy_;
};

}  // namespace cx::agent
