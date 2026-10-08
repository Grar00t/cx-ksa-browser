#pragma once

#include "agent/permissions.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace cx::agent {

class LocalLogger {
public:
  explicit LocalLogger(
      std::filesystem::path path = DefaultPath());

  static std::filesystem::path DefaultPath();

  bool Open();
  bool IsOpen() const noexcept;
  static bool VerifyFile(const std::filesystem::path& path);
  const std::filesystem::path& path() const noexcept;
  bool Log(std::string_view event,
           std::string_view detail = {});

private:
  std::filesystem::path path_;
  std::ofstream stream_;
  mutable std::mutex mutex_;
  std::string previous_hash_(64, '0');
};

enum class AgentState {
  Stopped,
  Running,
};

class AgentCore {
public:
  AgentCore(PermissionManager& permissions,
            LocalLogger& logger);

  bool Start(HWND owner);
  void Stop();
  AgentState state() const noexcept;

  bool AuthorizeAction(HWND owner,
                       std::string_view action);
  bool AuthorizeScopedAction(
      HWND owner,
      std::string_view action,
      const PermissionScope& scope,
      bool sensitive_action_confirmed);

private:
  PermissionManager& permissions_;
  LocalLogger& logger_;
  AgentState state_ = AgentState::Stopped;
};

}  // namespace cx::agent
