#include "mcp/allowlist_manager.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace cx::mcp {
namespace {

constexpr std::uintmax_t kMaxConfigBytes = 1024 * 1024;

void SkipWhitespace(std::string_view input, std::size_t* position) {
  while (*position < input.size() &&
         std::isspace(static_cast<unsigned char>(input[*position]))) {
    ++(*position);
  }
}

bool Consume(
    std::string_view input, std::size_t* position, char expected) {
  SkipWhitespace(input, position);
  if (*position >= input.size() || input[*position] != expected) {
    return false;
  }
  ++(*position);
  return true;
}

bool ParseString(
    std::string_view input, std::size_t* position, std::string* output) {
  SkipWhitespace(input, position);
  if (*position >= input.size() || input[*position] != '"') {
    return false;
  }
  ++(*position);
  output->clear();

  while (*position < input.size()) {
    const char value = input[(*position)++];
    if (value == '"') {
      return true;
    }
    if (static_cast<unsigned char>(value) < 0x20) {
      return false;
    }
    if (value != '\\') {
      output->push_back(value);
      continue;
    }

    if (*position >= input.size()) {
      return false;
    }
    const char escaped = input[(*position)++];

    switch (escaped) {
      case '"': output->push_back('"'); break;
      case '\\': output->push_back('\\'); break;
      case '/': output->push_back('/'); break;
      case 'b': output->push_back('\b'); break;
      case 'f': output->push_back('\f'); break;
      case 'n': output->push_back('\n'); break;
      case 'r': output->push_back('\r'); break;
      case 't': output->push_back('\t'); break;
      default:
        return false;
    }
  }
  return false;
}

bool ParseUnsigned(
    std::string_view input, std::size_t* position, std::size_t* value) {
  SkipWhitespace(input, position);
  if (*position >= input.size() ||
      !std::isdigit(static_cast<unsigned char>(input[*position]))) {
    return false;
  }

  std::size_t result = 0;
  while (*position < input.size() &&
         std::isdigit(static_cast<unsigned char>(input[*position]))) {
    const unsigned digit =
        static_cast<unsigned>(input[*position] - '0');

    if (result > (SIZE_MAX - digit) / 10) {
      return false;
    }
    result = result * 10 + digit;
    ++(*position);
  }
  *value = result;
  return true;
}

bool ParseStringArray(
    std::string_view input,
    std::size_t* position,
    std::vector<std::string>* values) {
  if (!Consume(input, position, '[')) {
    return false;
  }
  values->clear();

  SkipWhitespace(input, position);
  if (*position < input.size() && input[*position] == ']') {
    ++(*position);
    return true;
  }

  while (true) {
    std::string value;
    if (!ParseString(input, position, &value)) {
      return false;
    }
    values->push_back(std::move(value));

    SkipWhitespace(input, position);
    if (*position < input.size() && input[*position] == ']') {
      ++(*position);
      return true;
    }
    if (!Consume(input, position, ',')) {
      return false;
    }
    SkipWhitespace(input, position);
    if (*position < input.size() &&
        input[*position] == ']') {
      return false;
    }
  }
}

bool ParseServer(
    std::string_view input,
    std::size_t* position,
    ServerConfig* server) {
  if (!Consume(input, position, '{')) {
    return false;
  }

  bool has_id = false;
  bool has_command = false;
  bool has_args = false;
  bool has_version = false;
  bool has_sha256 = false;

  while (true) {
    SkipWhitespace(input, position);
    if (*position < input.size() && input[*position] == '}') {
      ++(*position);
      return has_id && has_command && has_args &&
          has_version && has_sha256;
    }

    std::string key;

    if (!ParseString(input, position, &key) ||
        !Consume(input, position, ':')) {
      return false;
    }

    if (key == "id") {
      if (has_id || !ParseString(input, position, &server->id)) {
        return false;
      }
      has_id = true;
    } else if (key == "command") {
      if (has_command ||
          !ParseString(input, position, &server->command)) {
        return false;
      }
      has_command = true;
    } else if (key == "args") {
      if (has_args ||
          !ParseStringArray(input, position, &server->args)) {
        return false;
      }
      has_args = true;
    } else if (key == "version") {
      if (has_version ||
          !ParseString(input, position, &server->version)) {
        return false;
      }
      has_version = true;
    } else if (key == "sha256") {
      if (has_sha256 ||
          !ParseString(input, position, &server->sha256)) {
        return false;
      }
      has_sha256 = true;
    } else {
      return false;
    }

    SkipWhitespace(input, position);
    if (*position < input.size() && input[*position] == '}') {
      ++(*position);
      return has_id && has_command && has_args &&
          has_version && has_sha256;

    }
    if (!Consume(input, position, ',')) {
      return false;
    }
    SkipWhitespace(input, position);
    if (*position < input.size() &&
        input[*position] == '}') {
      return false;
    }
  }
}

bool ParseServers(
    std::string_view input,
    std::size_t* position,
    std::vector<ServerConfig>* servers) {
  if (!Consume(input, position, '[')) {
    return false;
  }
  servers->clear();

  SkipWhitespace(input, position);
  if (*position < input.size() && input[*position] == ']') {
    ++(*position);
    return true;
  }

  while (true) {
    ServerConfig server;
    if (!ParseServer(input, position, &server)) {
      return false;
    }
    servers->push_back(std::move(server));

    SkipWhitespace(input, position);
    if (*position < input.size() && input[*position] == ']') {

      ++(*position);
      return true;
    }
    if (!Consume(input, position, ',')) {
      return false;
    }
    SkipWhitespace(input, position);
    if (*position < input.size() &&
        input[*position] == ']') {
      return false;
    }
  }
}

bool ParseRoot(
    std::string_view input, std::vector<ServerConfig>* servers) {
  std::size_t position = 0;
  if (!Consume(input, &position, '{')) {
    return false;
  }

  bool has_version = false;
  bool has_servers = false;
  while (true) {
    SkipWhitespace(input, &position);
    if (position < input.size() && input[position] == '}') {
      ++position;
      break;
    }

    std::string key;
    if (!ParseString(input, &position, &key) ||
        !Consume(input, &position, ':')) {
      return false;
    }

    if (key == "version") {
      std::size_t version = 0;
      if (has_version ||
          !ParseUnsigned(input, &position, &version) ||
          version != 2) {
        return false;
      }
      has_version = true;
    } else if (key == "servers") {
      if (has_servers ||
          !ParseServers(input, &position, servers)) {
        return false;
      }
      has_servers = true;
    } else {
      return false;
    }

    SkipWhitespace(input, &position);
    if (position < input.size() && input[position] == '}') {
      ++position;
      break;
    }
    if (!Consume(input, &position, ',')) {
      return false;
    }
    SkipWhitespace(input, &position);
    if (position < input.size() &&
        input[position] == '}') {
      return false;
    }
  }

  SkipWhitespace(input, &position);
  return has_version && has_servers && position == input.size();
}

std::string EscapeJson(std::string_view value) {
  std::string output;
  output.reserve(value.size() + 8);
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (ch < 0x20) {
          return {};
        }
        output.push_back(static_cast<char>(ch));
        break;
    }
  }
  return output;
}

bool IsValidId(std::string_view id) {
  if (id.empty() || id.size() > 64) {
    return false;
  }

  return std::all_of(
      id.begin(), id.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' ||
               ch == '_' || ch == '-';
      });
}

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }

  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), result.data(), size) != size) {
    return {};
  }
  return result;
}

bool HasExeExtension(const std::filesystem::path& path) {
  std::wstring extension = path.extension().wstring();

  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return extension == L".exe";
}


std::string HashFileSha256(const std::filesystem::path& path) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_bytes = 0;
  DWORD hash_bytes = 0;
  DWORD returned = 0;
  std::vector<unsigned char> object;
  std::vector<unsigned char> digest;

  auto close = [&]() {
    if (hash) {
      BCryptDestroyHash(hash);
    }
    if (algorithm) {
      BCryptCloseAlgorithmProvider(algorithm, 0);
    }
  };

  if (BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptGetProperty(
          algorithm, BCRYPT_OBJECT_LENGTH,
          reinterpret_cast<PUCHAR>(&object_bytes),
          sizeof(object_bytes), &returned, 0) < 0 ||
      BCryptGetProperty(
          algorithm, BCRYPT_HASH_LENGTH,
          reinterpret_cast<PUCHAR>(&hash_bytes),
          sizeof(hash_bytes), &returned, 0) < 0) {
    close();
    return {};
  }

  object.resize(object_bytes);
  digest.resize(hash_bytes);
  if (BCryptCreateHash(
          algorithm, &hash, object.data(), object_bytes,
          nullptr, 0, 0) < 0) {
    close();
    return {};
  }

  std::ifstream input(path, std::ios::binary);
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), buffer.size());
    const auto count = input.gcount();
    if (count > 0 &&
        BCryptHashData(
            hash, reinterpret_cast<PUCHAR>(buffer.data()),
            static_cast<ULONG>(count), 0) < 0) {
      close();
      return {};
    }
  }
  if (!input.eof() ||
      BCryptFinishHash(
          hash, digest.data(),
          static_cast<ULONG>(digest.size()), 0) < 0) {
    close();
    return {};
  }
  close();

  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const unsigned char byte : digest) {
    output << std::setw(2) << static_cast<unsigned>(byte);
  }
  return output.str();
}

bool IsSha256(std::string_view value) {
  return value.size() == 64 &&
      std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) || (ch >= 'a' && ch <= 'f');
      });
}

}  // namespace

AllowlistManager::AllowlistManager(std::filesystem::path path)
    : path_(std::move(path)) {}

std::filesystem::path AllowlistManager::DefaultPath() {
  wchar_t* appdata = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&appdata, &length, L"APPDATA") == 0 &&
      appdata != nullptr && length > 1) {
    std::filesystem::path result =
        std::filesystem::path(appdata) /
        L"CX Build" / L"config" / L"mcp_allowlist.json";
    std::free(appdata);
    return result;
  }
  std::free(appdata);
  return std::filesystem::temp_directory_path() /
      "CX Build" / "config" / "mcp_allowlist.json";
}

bool AllowlistManager::Load() {
  servers_.clear();

  std::error_code error;
  if (!std::filesystem::exists(path_, error)) {
    if (error) {
      return false;
    }
    return Save();
  }

  const auto size = std::filesystem::file_size(path_, error);
  if (error || size > kMaxConfigBytes) {
    return false;
  }

  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    return false;
  }

  const std::string json{
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()};
  std::string_view json_view(json);
  if (json_view.size() >= 3 &&
      static_cast<unsigned char>(json_view[0]) == 0xEF &&
      static_cast<unsigned char>(json_view[1]) == 0xBB &&
      static_cast<unsigned char>(json_view[2]) == 0xBF) {
    json_view.remove_prefix(3);
  }

  std::vector<ServerConfig> parsed;
  if (!ParseRoot(json_view, &parsed)) {
    return false;
  }

  for (const auto& server : parsed) {
    if (!ValidateServer(server)) {
      return false;
    }
  }

  std::sort(
      parsed.begin(), parsed.end(),
      [](const ServerConfig& left, const ServerConfig& right) {
        return left.id < right.id;
      });
  const auto duplicate = std::adjacent_find(
      parsed.begin(), parsed.end(),
      [](const ServerConfig& left, const ServerConfig& right) {
        return left.id == right.id;
      });
  if (duplicate != parsed.end()) {
    return false;
  }

  servers_ = std::move(parsed);
  return true;
}

bool AllowlistManager::Save() const {
  std::error_code error;
  std::filesystem::create_directories(path_.parent_path(), error);
  if (error) {
    return false;
  }

  const auto temporary = path_.wstring() + L".tmp";
  std::ofstream output(
      std::filesystem::path(temporary),
      std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  output << "{\n  \"version\": 2,\n  \"servers\": [";
  for (std::size_t i = 0; i < servers_.size(); ++i) {
    const auto& server = servers_[i];
    output << (i == 0 ? "\n" : ",\n")
           << "    {\"id\":\""
           << EscapeJson(server.id)
           << "\",\"command\":\""
           << EscapeJson(server.command)
           << "\",\"version\":\""
           << EscapeJson(server.version)
           << "\",\"sha256\":\""
           << EscapeJson(server.sha256)
           << "\",\"args\":[";
    for (std::size_t j = 0; j < server.args.size(); ++j) {
      if (j != 0) {
        output << ",";
      }
      output << "\"" << EscapeJson(server.args[j]) << "\"";
    }
    output << "]}";
  }
  if (!servers_.empty()) {
    output << "\n";
  }
  output << "  ]\n}\n";
  output.flush();
  if (!output.good()) {
    output.close();
    std::filesystem::remove(temporary, error);
    return false;
  }
  output.close();

  if (!MoveFileExW(
          temporary.c_str(), path_.c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}

bool AllowlistManager::IsAllowed(std::string_view server_id) const {
  return Find(server_id).has_value();
}

std::optional<ServerConfig> AllowlistManager::Find(
    std::string_view server_id) const {
  const auto iterator = std::find_if(
      servers_.begin(), servers_.end(),
      [server_id](const ServerConfig& server) {
        return server.id == server_id;
      });
  if (iterator == servers_.end()) {
    return std::nullopt;
  }
  return *iterator;
}

std::vector<ServerConfig> AllowlistManager::List() const {
  return servers_;
}

bool AllowlistManager::AddOrUpdate(ServerConfig server) {
  const std::wstring command = Utf8ToWide(server.command);
  const std::string digest = command.empty()
      ? std::string{}
      : HashFileSha256(std::filesystem::path(command));
  if (digest.empty()) {
    return false;
  }
  server.sha256 = digest;
  server.version = "sha256:" + digest.substr(0, 16);
  if (!ValidateServer(server)) {
    return false;
  }

  const auto previous = servers_;
  const auto iterator = std::find_if(
      servers_.begin(), servers_.end(),
      [&server](const ServerConfig& existing) {
        return existing.id == server.id;
      });

  if (iterator == servers_.end()) {
    servers_.push_back(std::move(server));
  } else {
    *iterator = std::move(server);
  }

  std::sort(
      servers_.begin(), servers_.end(),
      [](const ServerConfig& left, const ServerConfig& right) {
        return left.id < right.id;
      });

  if (!Save()) {
    servers_ = previous;
    return false;
  }
  return true;
}

bool AllowlistManager::Remove(std::string_view server_id) {
  const auto previous = servers_;
  std::erase_if(
      servers_, [server_id](const ServerConfig& server) {
        return server.id == server_id;
      });
  if (servers_.size() == previous.size()) {
    return false;
  }

  if (!Save()) {
    servers_ = previous;
    return false;
  }
  return true;
}

const std::filesystem::path& AllowlistManager::path() const noexcept {
  return path_;
}

bool AllowlistManager::ValidateServer(const ServerConfig& server) {
  if (!IsValidId(server.id) ||
      server.command.empty() ||
      server.command.size() > 32767 ||
      server.args.size() > 64 ||
      server.version != "sha256:" +
          server.sha256.substr(0, 16) ||
      !IsSha256(server.sha256)) {
    return false;
  }

  const std::wstring command = Utf8ToWide(server.command);
  if (command.empty()) {
    return false;
  }

  const std::filesystem::path command_path(command);
  if (!command_path.is_absolute() || !HasExeExtension(command_path)) {
    return false;
  }

  // MCP executables are local-only. Reject UNC/device namespaces and any
  // drive Windows classifies as remote, removable, optical, unknown, or
  // nonexistent. Only fixed disks and RAM disks are accepted.
  const std::wstring native_command = command_path.native();
  if (native_command.rfind(L"\\\\", 0) == 0) {
    return false;
  }
  const std::wstring root = command_path.root_path().native();
  if (root.empty()) {
    return false;
  }
  const UINT drive_type = GetDriveTypeW(root.c_str());
  if (drive_type != DRIVE_FIXED && drive_type != DRIVE_RAMDISK) {
    return false;
  }

  for (const auto& argument : server.args) {
    if (argument.size() > 8192 ||
        argument.find('\0') != std::string::npos) {
      return false;
    }
  }
  return true;
}

bool AllowlistManager::VerifyExecutableIdentity(
    const ServerConfig& server) {
  if (!ValidateServer(server)) {
    return false;
  }
  const std::wstring command = Utf8ToWide(server.command);
  return !command.empty() &&
      HashFileSha256(std::filesystem::path(command)) ==
          server.sha256;
}

}  // namespace cx::mcp
