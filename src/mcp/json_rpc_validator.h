#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace cx::mcp {

class JsonRpcValidator {
public:
  static constexpr std::size_t kMaxDepth = 16;
  static bool ValidateRequest(
      std::string_view json,
      std::string* method);
};

}  // namespace cx::mcp
