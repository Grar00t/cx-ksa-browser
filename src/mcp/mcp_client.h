#pragma once

#include <windows.h>

#include <chrono>
#include <string>
#include <string_view>

namespace cx::agent {
class AgentCore;
class LocalLogger;
}

namespace cx::mcp {
class AllowlistManager;
class RateLimiter;
struct ServerConfig;

class McpClient {
public:
  McpClient(
      agent::AgentCore& agent,
      AllowlistManager& allowlist,
      RateLimiter& rate_limiter,
      agent::LocalLogger& logger);
  ~McpClient();

  McpClient(const McpClient&) = delete;
  McpClient& operator=(const McpClient&) = delete;

  bool Start(HWND owner, std::string_view server_id);
  void Stop();

  bool IsRunning() const;
  std::string active_server() const;

  bool SendRequest(
      std::string_view server_id,
      std::string_view json_line,
      std::string* response,
      std::chrono::milliseconds timeout =
          std::chrono::seconds(5));

private:
  bool Launch(const ServerConfig& server);
  bool WriteAll(std::string_view bytes);
  bool ReadLine(
      std::string* line,
      std::chrono::milliseconds timeout);
  void CloseHandles();

  agent::AgentCore& agent_;
  AllowlistManager& allowlist_;
  RateLimiter& rate_limiter_;
  agent::LocalLogger& logger_;

  HANDLE process_ = nullptr;
  HANDLE stdin_write_ = nullptr;
  HANDLE stdout_read_ = nullptr;
  std::string active_server_;
  std::string read_buffer_;
};

}  // namespace cx::mcp
