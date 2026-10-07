#pragma once

#include <windows.h>

#include <optional>
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

class ConsentPrompt {
public:
  virtual ~ConsentPrompt() = default;
  virtual bool Request(HWND owner,
                       const CapabilityDescriptor& capability) = 0;
};

class PermissionManager {
public:
  PermissionManager(storage::Database& database, ConsentPrompt& prompt);

  bool IsGranted(Capability capability) const;
  bool Ensure(HWND owner, Capability capability);
  bool SetGranted(Capability capability, bool granted);
  bool Revoke(Capability capability);
  bool RevokeAll();
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
