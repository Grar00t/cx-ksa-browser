#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cx::ui {

enum class ContextNodeKind {
  Origin,
  Tab,
};

struct ContextTabInput final {
  std::int64_t id = 0;
  std::string url;
  std::string title;
  bool active = false;
};

struct ContextGraphNode final {
  std::string key;
  std::string label;
  ContextNodeKind kind = ContextNodeKind::Tab;
  std::int64_t tab_id = 0;
  bool active = false;
  std::size_t degree = 0;
  float x = 0.0F;
  float y = 0.0F;
};

struct ContextGraphEdge final {
  std::size_t first = 0;
  std::size_t second = 0;
};

struct ContextGraph final {
  std::vector<ContextGraphNode> nodes;
  std::vector<ContextGraphEdge> edges;
};

std::string SafeOriginLabel(std::string_view url);
ContextGraph BuildContextGraph(
    const std::vector<ContextTabInput>& tabs,
    std::size_t maximum_tabs = 18);

}  // namespace cx::ui
