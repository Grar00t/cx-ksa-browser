#include "ui/context_graph.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace cx::ui {
namespace {

std::string LowerAscii(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return value;
}

std::string CleanTitle(
    std::string_view title,
    std::string_view fallback) {
  std::string output;
  output.reserve((std::min)(title.size(), std::size_t{32}));
  for (const unsigned char character : title) {
    if (output.size() == 32) break;
    if (character >= 0x20 && character != 0x7F) {
      output.push_back(static_cast<char>(character));
    }
  }
  return output.empty() ? std::string(fallback) : output;
}

}  // namespace

std::string SafeOriginLabel(std::string_view url) {
  if (url == "about:blank") {
    return "Local new tab";
  }

  const auto separator = url.find("://");
  if (separator == std::string_view::npos) {
    return "Local or blocked";
  }

  const std::string scheme =
      LowerAscii(std::string(url.substr(0, separator)));
  if (scheme != "http" && scheme != "https") {
    return "Local or blocked";
  }

  const std::size_t authority_start = separator + 3;
  const std::size_t authority_end =
      url.find_first_of("/?#", authority_start);
  std::string authority(url.substr(
      authority_start,
      authority_end == std::string_view::npos
          ? std::string_view::npos
          : authority_end - authority_start));
  const auto credentials = authority.rfind('@');
  if (credentials != std::string::npos) {
    authority.erase(0, credentials + 1);
  }
  authority = LowerAscii(authority);
  if (authority.empty() || authority.size() > 255) {
    return "Invalid origin";
  }

  return scheme + "://" + authority;
}

ContextGraph BuildContextGraph(
    const std::vector<ContextTabInput>& tabs,
    std::size_t maximum_tabs) {
  ContextGraph graph;
  std::unordered_map<std::string, std::size_t> origin_indices;
  std::vector<std::size_t> origin_order;
  std::vector<std::size_t> tab_order;

  const std::size_t count = (std::min)(tabs.size(), maximum_tabs);
  std::vector<std::size_t> selected;
  selected.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    selected.push_back(index);
  }
  if (!selected.empty() && count < tabs.size()) {
    const auto active = std::find_if(
        tabs.begin() + static_cast<std::ptrdiff_t>(count),
        tabs.end(),
        [](const ContextTabInput& tab) { return tab.active; });
    if (active != tabs.end()) {
      selected.back() = static_cast<std::size_t>(
          std::distance(tabs.begin(), active));
    }
  }

  for (const std::size_t index : selected) {
    const auto& tab = tabs[index];
    const std::string origin = SafeOriginLabel(tab.url);

    std::size_t origin_index = 0;
    const auto existing = origin_indices.find(origin);
    if (existing == origin_indices.end()) {
      origin_index = graph.nodes.size();
      origin_indices.emplace(origin, origin_index);
      graph.nodes.push_back(ContextGraphNode{
          "origin:" + origin,
          origin,
          ContextNodeKind::Origin,
          0,
          false,
          0,
          0.0F,
          0.0F});
      origin_order.push_back(origin_index);
    } else {
      origin_index = existing->second;
    }

    const std::size_t tab_index = graph.nodes.size();
    graph.nodes.push_back(ContextGraphNode{
        "tab:" + std::to_string(tab.id),
        CleanTitle(tab.title, origin),
        ContextNodeKind::Tab,
        tab.id,
        tab.active,
        0,
        0.0F,
        0.0F});
    tab_order.push_back(tab_index);
    graph.edges.push_back(ContextGraphEdge{
        origin_index, tab_index});
    ++graph.nodes[origin_index].degree;
    ++graph.nodes[tab_index].degree;
  }

  for (std::size_t index = 0; index < origin_order.size(); ++index) {
    auto& node = graph.nodes[origin_order[index]];
    node.x = 0.27F;
    node.y = static_cast<float>(index + 1) /
        static_cast<float>(origin_order.size() + 1);
  }
  for (std::size_t index = 0; index < tab_order.size(); ++index) {
    auto& node = graph.nodes[tab_order[index]];
    node.x = 0.73F;
    node.y = static_cast<float>(index + 1) /
        static_cast<float>(tab_order.size() + 1);
  }

  return graph;
}

}  // namespace cx::ui
