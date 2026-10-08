#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cx::mcp {

struct ServerConfig {
  std::string id;
  std::string command;
  std::vector<std::string> args;
  std::string version;
  std::string sha256;

  bool operator==(const ServerConfig&) const = default;
};

class AllowlistManager {
public:
  explicit AllowlistManager(
      std::filesystem::path path = DefaultPath());

  static std::filesystem::path DefaultPath();

  bool Load();
  bool Save() const;

  bool IsAllowed(std::string_view server_id) const;
  std::optional<ServerConfig> Find(
      std::string_view server_id) const;
  std::vector<ServerConfig> List() const;

  bool AddOrUpdate(ServerConfig server);
  bool Remove(std::string_view server_id);

  const std::filesystem::path& path() const noexcept;

  static bool ValidateServer(const ServerConfig& server);
  static bool VerifyExecutableIdentity(const ServerConfig& server);

private:
  std::filesystem::path path_;
  std::vector<ServerConfig> servers_;
};

}  // namespace cx::mcp
