#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cx::storage {
class Database;
}

namespace cx::agent {

enum class Capability {
  AgentRun,
  ReadPage,
  Navigate,
  ManageTabs,
  ClipboardWrite,
  NativeMessaging,
  McpConnect,
};

struct CapabilityDescriptor {
  Capability capability;
  std::string_view id;
  const wchar_t* title;
  const wchar_t* description;
};

const CapabilityDescriptor& DescribeCapability(Capability capability);
std::optional<Capability> CapabilityFromId(std::string_view id);

struct PermissionScope {
  std::int64_t tab_id = 0;
  std::string origin;

  bool operator==(const PermissionScope&) const = default;
};

class ConsentPrompt {
public:
  virtual ~ConsentPrompt() = default;
  virtual bool Request(HWND owner,
                       const CapabilityDescriptor& capability) = 0;
  virtual bool RequestScoped(
      HWND owner,
      const CapabilityDescriptor& capability,
      const PermissionScope&) {
    return Request(owner, capability);
  }
};

class PermissionManager {
public:
  PermissionManager(storage::Database& database, ConsentPrompt& prompt);

  bool IsGranted(Capability capability) const;
  bool Ensure(HWND owner, Capability capability);
  bool SetGranted(Capability capability, bool granted);
  bool Revoke(Capability capability);
  bool RevokeAll();

  bool IsGrantedScoped(
      Capability capability,
      const PermissionScope& scope) const;
  bool EnsureScoped(
      HWND owner,
      Capability capability,
      const PermissionScope& scope);
  bool RevokeTab(std::int64_t tab_id);
  std::vector<Capability> Granted() const;

private:
  storage::Database& database_;
  ConsentPrompt& prompt_;
};

struct ActionRule {
  std::string_view action;
  Capability capability;
};

class ActionAllowlist {
public:
  static bool IsListed(std::string_view action);
  static std::optional<Capability> RequiredCapability(
      std::string_view action);
};

}  // namespace cx::agent
