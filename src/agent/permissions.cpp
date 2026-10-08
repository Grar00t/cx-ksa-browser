#include "agent/permissions.h"

#include "storage/database.h"

#include <array>
#include <stdexcept>

namespace cx::agent {
namespace {

constexpr std::array<CapabilityDescriptor, 7> kCapabilities{{
    {Capability::AgentRun, "agent.run", L"Start the agent",
     L"Allows the local agent lifecycle to start."},
    {Capability::ReadPage, "browser.read_page", L"Read page content",
     L"Allows the agent to inspect the current page content."},
    {Capability::Navigate, "browser.navigate", L"Navigate the browser",
     L"Allows the agent to request browser navigation."},
    {Capability::ManageTabs, "tabs.manage", L"Manage tabs",
     L"Allows the agent to open, close, or reorder tabs."},
    {Capability::ClipboardWrite, "clipboard.write", L"Write clipboard",
     L"Allows the agent to place text on the Windows clipboard."},
    {Capability::NativeMessaging, "native_messaging.connect",
     L"Use native messaging",
     L"Allows the agent to connect to an explicitly allowlisted native host."},
    {Capability::McpConnect, "mcp.connect", L"Connect to MCP",
     L"Allows the agent to connect to an explicitly configured MCP endpoint."},
}};

bool IsValidScope(const PermissionScope& scope) {
  if (scope.tab_id <= 0 ||
      scope.origin.empty() ||
      scope.origin.size() > 2048) {
    return false;
  }
  return scope.origin.starts_with("https://") ||
      scope.origin.starts_with("http://");
}

constexpr std::array<ActionRule, 12> kActionRules{{
    {"browser.read_page", Capability::ReadPage},
    {"browser.navigate", Capability::Navigate},
    {"tabs.manage", Capability::ManageTabs},
    {"clipboard.write", Capability::ClipboardWrite},
    {"native_messaging.connect", Capability::NativeMessaging},
    {"mcp.connect", Capability::McpConnect},
    {"form.submit", Capability::Navigate},
    {"purchase.confirm", Capability::Navigate},
    {"file.upload", Capability::NativeMessaging},
    {"file.download", Capability::NativeMessaging},
    {"credential.fill", Capability::ClipboardWrite},
    {"browser.navigate_cross_origin", Capability::Navigate},
}};

}  // namespace

const CapabilityDescriptor& DescribeCapability(Capability capability) {
  for (const auto& descriptor : kCapabilities) {
    if (descriptor.capability == capability) {
      return descriptor;
    }
  }
  throw std::invalid_argument("Unknown capability");
}

std::optional<Capability> CapabilityFromId(std::string_view id) {
  for (const auto& descriptor : kCapabilities) {
    if (descriptor.id == id) {
      return descriptor.capability;
    }
  }
  return std::nullopt;
}

PermissionManager::PermissionManager(
    storage::Database& database, ConsentPrompt& prompt)
    : database_(database), prompt_(prompt) {}

bool PermissionManager::IsGranted(Capability capability) const {
  const auto value =
      database_.GetPermission(DescribeCapability(capability).id);
  return value.has_value() && *value;
}

bool PermissionManager::Ensure(HWND owner, Capability capability) {
  if (IsGranted(capability)) {
    return true;
  }

  const auto& descriptor = DescribeCapability(capability);
  const bool granted = prompt_.Request(owner, descriptor);

  if (!database_.SetPermission(descriptor.id, granted)) {
    return false;
  }
  return granted;
}

bool PermissionManager::SetGranted(
    Capability capability, bool granted) {
  return database_.SetPermission(
      DescribeCapability(capability).id, granted);
}

bool PermissionManager::Revoke(Capability capability) {
  return SetGranted(capability, false);
}

bool PermissionManager::RevokeAll() {
  return database_.RevokeAllPermissions();
}

bool PermissionManager::IsGrantedScoped(
    Capability capability,
    const PermissionScope& scope) const {
  if (!IsValidScope(scope)) {
    return false;
  }
  const auto value = database_.GetScopedPermission(
      scope.tab_id,
      scope.origin,
      DescribeCapability(capability).id);
  return value.has_value() && *value;
}

bool PermissionManager::EnsureScoped(
    HWND owner,
    Capability capability,
    const PermissionScope& scope) {
  if (!IsValidScope(scope)) {
    return false;
  }
  if (IsGrantedScoped(capability, scope)) {
    return true;
  }

  const auto& descriptor = DescribeCapability(capability);
  const bool granted =
      prompt_.RequestScoped(owner, descriptor, scope);
  if (!database_.SetScopedPermission(
          scope.tab_id,
          scope.origin,
          descriptor.id,
          granted)) {
    return false;
  }
  return granted;
}

bool PermissionManager::RevokeTab(std::int64_t tab_id) {
  return database_.RevokeTabPermissions(tab_id);
}

std::vector<Capability> PermissionManager::Granted() const {
  std::vector<Capability> granted;
  for (const auto& descriptor : kCapabilities) {
    if (IsGranted(descriptor.capability)) {
      granted.push_back(descriptor.capability);
    }
  }
  return granted;
}

bool ActionAllowlist::IsListed(std::string_view action) {
  return RequiredCapability(action).has_value();
}

std::optional<Capability> ActionAllowlist::RequiredCapability(
    std::string_view action) {
  for (const auto& rule : kActionRules) {
    if (rule.action == action) {
      return rule.capability;
    }
  }
  return std::nullopt;
}

}  // namespace cx::agent
