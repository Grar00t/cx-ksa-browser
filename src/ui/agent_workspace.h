#pragma once

namespace cx::ui {

enum class WorkspacePointerTarget {
  BrowserChrome,
  BrowserContent,
  AgentPanel,
  EmergencyStop,
};

class AgentWorkspacePolicy final {
public:
  void SetAgentRunning(bool running) noexcept;
  bool agent_running() const noexcept;
  bool shield_visible() const noexcept;
  bool emergency_stop_visible() const noexcept;
  bool BlocksPointer(WorkspacePointerTarget target) const noexcept;

private:
  bool agent_running_ = false;
};

}  // namespace cx::ui
