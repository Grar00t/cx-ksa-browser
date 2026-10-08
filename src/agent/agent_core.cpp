#include "agent/agent_core.h"

#include "agent/untrusted_page_content.h"

#include <bcrypt.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <system_error>
#include <vector>

namespace cx::agent {
namespace {

std::string SanitizeToken(std::string_view value) {
  std::string output;
  output.reserve(std::min<std::size_t>(value.size(), 128));
  for (const unsigned char ch : value) {
    if (output.size() >= 128) {
      break;
    }
    output.push_back(
        ch == '\r' || ch == '\n' || ch == '\t'
            ? '_'
            : static_cast<char>(ch));
  }
  return output;
}

std::string LowerAscii(std::string_view value) {
  std::string output(value);
  for (char& ch : output) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return output;
}

std::string RedactSecrets(std::string_view detail) {
  const std::string lower = LowerAscii(detail);
  constexpr std::array<std::string_view, 5> secret_names{{
      "password", "token", "secret", "authorization", "cookie"}};
  for (const auto name : secret_names) {
    if (lower.find(name) != std::string::npos) {
      return "[REDACTED]";
    }
  }
  return SanitizeToken(detail);
}

std::string ComputeEntryHash(std::string_view value) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_bytes = 0;
  DWORD hash_bytes = 0;
  DWORD copied = 0;

  if (BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM,
          nullptr, 0) < 0 ||
      BCryptGetProperty(
          algorithm, BCRYPT_OBJECT_LENGTH,
          reinterpret_cast<PUCHAR>(&object_bytes),
          sizeof(object_bytes), &copied, 0) < 0 ||
      BCryptGetProperty(
          algorithm, BCRYPT_HASH_LENGTH,
          reinterpret_cast<PUCHAR>(&hash_bytes),
          sizeof(hash_bytes), &copied, 0) < 0 ||
      hash_bytes != 32) {
    if (algorithm) {
      BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return {};
  }

  std::vector<unsigned char> object(object_bytes);
  std::array<unsigned char, 32> digest{};
  if (BCryptCreateHash(
          algorithm, &hash, object.data(), object_bytes,
          nullptr, 0, 0) < 0 ||
      BCryptHashData(
          hash,
          reinterpret_cast<PUCHAR>(
              const_cast<char*>(value.data())),
          static_cast<ULONG>(value.size()), 0) < 0 ||
      BCryptFinishHash(
          hash, digest.data(),
          static_cast<ULONG>(digest.size()), 0) < 0) {
    if (hash) {
      BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }

  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);

  constexpr char hex[] = "0123456789abcdef";
  std::string output;
  output.reserve(64);
  for (const unsigned char byte : digest) {
    output.push_back(hex[byte >> 4]);
    output.push_back(hex[byte & 0x0F]);
  }
  return output;
}

bool ParseAuditLine(
    std::string_view line,
    std::string* payload,
    std::string* previous,
    std::string* hash) {
  std::array<std::size_t, 4> tabs{};
  std::size_t position = 0;
  for (std::size_t i = 0; i < tabs.size(); ++i) {
    tabs[i] = line.find('\t', position);
    if (tabs[i] == std::string_view::npos) {
      return false;
    }
    position = tabs[i] + 1;
  }
  if (line.find('\t', position) != std::string_view::npos) {
    return false;
  }

  *payload = std::string(line.substr(0, tabs[3]));
  *previous = std::string(
      line.substr(tabs[2] + 1, tabs[3] - tabs[2] - 1));
  *hash = std::string(line.substr(tabs[3] + 1));
  return previous->size() == 64 && hash->size() == 64;
}

}  // namespace


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

  if (std::filesystem::exists(path_, error) &&
      !error &&
      std::filesystem::file_size(path_, error) > 0) {
    if (error || !VerifyFile(path_)) {
      return false;
    }
    std::ifstream input(path_);
    std::string line;
    std::string last;
    while (std::getline(input, line)) {
      last = line;
    }
    const auto separator = last.rfind('\t');
    if (separator == std::string::npos ||
        last.size() - separator - 1 != 64) {
      return false;
    }
    previous_hash_ = last.substr(separator + 1);
  }

  stream_.open(path_, std::ios::out | std::ios::app);
  return stream_.is_open();
}

bool LocalLogger::IsOpen() const noexcept {
  return stream_.is_open();
}

bool LocalLogger::VerifyFile(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return false;
  }

  std::string expected_previous(64, '0');
  std::string line;
  while (std::getline(input, line)) {
    std::string payload;
    std::string previous;
    std::string hash;
    if (!ParseAuditLine(
            line, &payload, &previous, &hash) ||
        previous != expected_previous) {
      return false;
    }
    const std::string computed =
        ComputeEntryHash(payload + "\t" + previous);
    if (computed.empty() || computed != hash) {
      return false;
    }
    expected_previous = hash;
  }
  return !input.bad();
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

  const std::string safe_event = SanitizeToken(event);
  const std::string safe_detail = RedactSecrets(detail);
  const std::string payload =
      std::to_string(milliseconds) + "\t" +
      safe_event + "\t" + safe_detail;
  const std::string hash =
      ComputeEntryHash(payload + "\t" + previous_hash_);
  if (hash.empty()) {
    return false;
  }

  stream_ << payload << "\t"
          << previous_hash_ << "\t"
          << hash << "\n";
  stream_.flush();
  if (!stream_.good()) {
    return false;
  }
  previous_hash_ = hash;
  return true;
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

bool AgentCore::AuthorizeScopedAction(
    HWND owner,
    std::string_view action,
    const PermissionScope& scope,
    bool sensitive_action_confirmed) {
  if (state_ != AgentState::Running) {
    logger_.Log("scoped_action_denied_agent_stopped", action);
    return false;
  }
  if (IsSensitiveAction(action) &&
      !sensitive_action_confirmed) {
    logger_.Log("scoped_action_denied_confirmation", action);
    return false;
  }

  const auto capability =
      ActionAllowlist::RequiredCapability(action);
  if (!capability.has_value() ||
      !permissions_.EnsureScoped(
          owner, *capability, scope)) {
    logger_.Log("scoped_action_denied_permission", action);
    return false;
  }
  logger_.Log("scoped_action_authorized", action);
  return true;
}

}  // namespace cx::agent
