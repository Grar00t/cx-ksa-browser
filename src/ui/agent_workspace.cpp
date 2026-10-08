#include "ui/agent_workspace.h"

namespace cx::ui {

void AgentWorkspacePolicy::SetAgentRunning(bool running) noexcept {
  agent_running_ = running;
}

bool AgentWorkspacePolicy::agent_running() const noexcept {
  return agent_running_;
}

bool AgentWorkspacePolicy::shield_visible() const noexcept {
  return agent_running_;
}

bool AgentWorkspacePolicy::emergency_stop_visible() const noexcept {
  return agent_running_;
}

bool AgentWorkspacePolicy::BlocksPointer(
    WorkspacePointerTarget target) const noexcept {
  return agent_running_ &&
      target != WorkspacePointerTarget::EmergencyStop;
}

}  // namespace cx::ui
