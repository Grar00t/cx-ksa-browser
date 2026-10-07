#include "agent/agent_core.h"

#include <chrono>
#include <cstdlib>
#include <system_error>

namespace cx::agent {

LocalLogger::LocalLogger(std::filesystem::path path)
    : path_(std::move(path)) {}

std::filesystem::path LocalLogger::DefaultPath() {
#ifdef _WIN32
  wchar_t* appdata = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&appdata, &length, L"APPDATA") == 0 &&
      appdata != nullptr && length > 1) {
    std::filesystem::path result =
        std::filesystem::path(appdata) /
        L"CX Build" / L"logs" / L"agent.log";
    std::free(appdata);
    return result;
  }
  std::free(appdata);
#endif
  return std::filesystem::temp_directory_path() /
         "CX Build" / "logs" / "agent.log";
}

bool LocalLogger::Open() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stream_.is_open()) {
    return true;
  }

  std::error_code error;
  std::filesystem::create_directories(
      path_.parent_path(), error);
  if (error) {
    return false;
  }

  stream_.open(path_, std::ios::out | std::ios::app);
  return stream_.is_open();
}

bool LocalLogger::IsOpen() const noexcept {
  return stream_.is_open();
}

const std::filesystem::path& LocalLogger::path() const noexcept {
  return path_;
}

bool LocalLogger::Log(
    std::string_view event, std::string_view detail) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stream_.is_open()) {
    return false;
  }

  const auto now = std::chrono::system_clock::now();
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count();

  stream_ << milliseconds << "\t"
          << event << "\t"
          << detail << "\n";
  stream_.flush();
  return stream_.good();
}

AgentCore::AgentCore(
    PermissionManager& permissions, LocalLogger& logger)
    : permissions_(permissions), logger_(logger) {}

bool AgentCore::Start(HWND owner) {
  if (state_ == AgentState::Running) {
    return true;
  }

  if (!permissions_.Ensure(owner, Capability::AgentRun)) {
    logger_.Log("agent_start_denied", "agent.run");
    return false;
  }

  state_ = AgentState::Running;
  logger_.Log("agent_started", "agent.run");
  return true;
}

void AgentCore::Stop() {
  if (state_ == AgentState::Stopped) {
    return;
  }
  state_ = AgentState::Stopped;
  logger_.Log("agent_stopped");
}

AgentState AgentCore::state() const noexcept {
  return state_;
}

bool AgentCore::AuthorizeAction(
    HWND owner, std::string_view action) {
  if (state_ != AgentState::Running) {
    logger_.Log("action_denied_agent_stopped", action);
    return false;
  }

  const auto capability =
      ActionAllowlist::RequiredCapability(action);
  if (!capability.has_value()) {
    logger_.Log("action_denied_not_allowlisted", action);
    return false;
  }

  if (!permissions_.Ensure(owner, *capability)) {
    logger_.Log("action_denied_permission", action);
    return false;
  }

  logger_.Log("action_authorized", action);
  return true;
}

}  // namespace cx::agent
