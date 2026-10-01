#include "mcp/mcp_client.h"

#include "agent/agent_core.h"
#include "mcp/allowlist_manager.h"
#include "mcp/rate_limiter.h"

#include <array>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace cx::mcp {
namespace {

constexpr std::size_t kMaxMessageBytes = 4 * 1024 * 1024;

void CloseHandleIfOpen(HANDLE* handle) {
  if (*handle && *handle != INVALID_HANDLE_VALUE) {
    CloseHandle(*handle);
    *handle = nullptr;
  }
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

std::wstring QuoteWindowsArgument(std::wstring_view value) {
  if (value.empty()) {
    return L"\"\"";
  }

  const bool needs_quotes =
      value.find_first_of(L" \t\"") != std::wstring_view::npos;
  if (!needs_quotes) {
    return std::wstring(value);
  }

  std::wstring output = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t ch : value) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }
    if (ch == L'\"') {
      output.append(backslashes * 2 + 1, L'\\');
      output.push_back(L'\"');

      backslashes = 0;
      continue;
    }
    output.append(backslashes, L'\\');
    backslashes = 0;
    output.push_back(ch);
  }
  output.append(backslashes * 2, L'\\');
  output.push_back(L'\"');
  return output;
}

std::wstring BuildCommandLine(const ServerConfig& server) {
  const std::wstring command = Utf8ToWide(server.command);
  if (command.empty()) {
    return {};
  }

  std::wstring line = QuoteWindowsArgument(command);
  for (const auto& argument : server.args) {
    const std::wstring wide = Utf8ToWide(argument);
    if (argument.size() != 0 && wide.empty()) {
      return {};
    }
    line.push_back(L' ');
    line += QuoteWindowsArgument(wide);
  }
  return line;
}

std::string SafeLogToken(std::string_view value) {
  std::string safe;
  safe.reserve(std::min<std::size_t>(value.size(), 128));
  for (const char ch : value) {
    if (safe.size() >= 128) {
      break;
    }
    if (ch == '\r' || ch == '\n' || ch == '\t') {
      safe.push_back('_');
    } else {
      safe.push_back(ch);
    }
  }
  return safe;
}

std::string RequestDetail(
    std::string_view server_id, std::size_t bytes) {
  std::ostringstream output;
  output << "server=" << SafeLogToken(server_id)
         << " bytes=" << bytes;
  return output.str();
}

}  // namespace

McpClient::McpClient(
    agent::AgentCore& agent,
    AllowlistManager& allowlist,
    RateLimiter& rate_limiter,
    agent::LocalLogger& logger)
    : agent_(agent),
      allowlist_(allowlist),
      rate_limiter_(rate_limiter),
      logger_(logger) {}

McpClient::~McpClient() {
  Stop();
}

bool McpClient::Start(HWND owner, std::string_view server_id) {
  logger_.Log(
      "mcp_server_start_requested", SafeLogToken(server_id));

  const auto server = allowlist_.Find(server_id);
  if (!server.has_value()) {
    logger_.Log(
        "mcp_server_denied_not_allowlisted",
        SafeLogToken(server_id));
    return false;
  }

  if (!agent_.AuthorizeAction(owner, "mcp.connect")) {
    logger_.Log(
        "mcp_server_denied_permission", server->id);
    return false;
  }

  if (IsRunning() && active_server_ == server->id) {
    return true;
  }

  Stop();
  if (!Launch(*server)) {
    logger_.Log("mcp_server_launch_failed", server->id);
    return false;
  }

  active_server_ = server->id;
  rate_limiter_.Reset(active_server_);
  logger_.Log("mcp_server_started", active_server_);
  return true;
}

bool McpClient::Launch(const ServerConfig& server) {
  const std::wstring command = Utf8ToWide(server.command);
  std::wstring command_line = BuildCommandLine(server);
  if (command.empty() || command_line.empty()) {
    return false;
  }

  std::error_code error;
  const std::filesystem::path command_path(command);
  if (!std::filesystem::is_regular_file(command_path, error) || error) {
    return false;
  }

  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;

  HANDLE child_stdin_read = nullptr;
  HANDLE child_stdout_write = nullptr;
  HANDLE child_stderr_write = INVALID_HANDLE_VALUE;

  HANDLE parent_stdin_write = nullptr;
  HANDLE parent_stdout_read = nullptr;

  if (!CreatePipe(
          &child_stdin_read, &parent_stdin_write,
          &security, 0) ||
      !CreatePipe(
          &parent_stdout_read, &child_stdout_write,
          &security, 0)) {
    CloseHandleIfOpen(&child_stdin_read);
    CloseHandleIfOpen(&parent_stdin_write);
    CloseHandleIfOpen(&parent_stdout_read);
    CloseHandleIfOpen(&child_stdout_write);
    return false;
  }

  if (!SetHandleInformation(
          parent_stdin_write, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(
          parent_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandleIfOpen(&child_stdin_read);
    CloseHandleIfOpen(&parent_stdin_write);
    CloseHandleIfOpen(&parent_stdout_read);
    CloseHandleIfOpen(&child_stdout_write);
    return false;
  }

  child_stderr_write = CreateFileW(
      L"NUL", GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (child_stderr_write == INVALID_HANDLE_VALUE) {
    CloseHandleIfOpen(&child_stdin_read);
    CloseHandleIfOpen(&parent_stdin_write);
    CloseHandleIfOpen(&parent_stdout_read);
    CloseHandleIfOpen(&child_stdout_write);
    return false;
  }

  SIZE_T attribute_bytes = 0;
  InitializeProcThreadAttributeList(
      nullptr, 1, 0, &attribute_bytes);
  std::vector<unsigned char> attribute_storage(attribute_bytes);

  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = child_stdin_read;
  startup.StartupInfo.hStdOutput = child_stdout_write;
  startup.StartupInfo.hStdError = child_stderr_write;
  startup.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
          attribute_storage.data());

  if (!InitializeProcThreadAttributeList(
          startup.lpAttributeList, 1, 0, &attribute_bytes)) {
    CloseHandleIfOpen(&child_stdin_read);
    CloseHandleIfOpen(&parent_stdin_write);
    CloseHandleIfOpen(&parent_stdout_read);
    CloseHandleIfOpen(&child_stdout_write);
    if (child_stderr_write != child_stdout_write) {
      CloseHandleIfOpen(&child_stderr_write);
    }
    return false;
  }

  std::array<HANDLE, 3> inherited{
      child_stdin_read, child_stdout_write, child_stderr_write};
  const bool attributes_ok = UpdateProcThreadAttribute(
      startup.lpAttributeList, 0,
      PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
      inherited.data(), sizeof(HANDLE) * inherited.size(),
      nullptr, nullptr) != FALSE;

  PROCESS_INFORMATION process_info{};
  const auto working_directory = command_path.parent_path().wstring();
  const BOOL created = attributes_ok
      ? CreateProcessW(
            command.c_str(), command_line.data(),
            nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
            nullptr,
            working_directory.empty()
                ? nullptr
                : working_directory.c_str(),
            &startup.StartupInfo,
            &process_info)
      : FALSE;

  DeleteProcThreadAttributeList(startup.lpAttributeList);
  CloseHandleIfOpen(&child_stdin_read);
  CloseHandleIfOpen(&child_stdout_write);
  if (child_stderr_write != child_stdout_write) {
    CloseHandleIfOpen(&child_stderr_write);
  }

  if (!created) {
    CloseHandleIfOpen(&parent_stdin_write);
    CloseHandleIfOpen(&parent_stdout_read);
    return false;
  }

  CloseHandle(process_info.hThread);
  process_ = process_info.hProcess;
  stdin_write_ = parent_stdin_write;
  stdout_read_ = parent_stdout_read;
  read_buffer_.clear();
  return true;
}

void McpClient::Stop() {
  const std::string stopped_server = active_server_;

  CloseHandleIfOpen(&stdin_write_);

  if (process_) {
    const DWORD wait = WaitForSingleObject(process_, 300);
    if (wait == WAIT_TIMEOUT) {
      TerminateProcess(process_, 0);
      WaitForSingleObject(process_, 1000);
    }
  }

  CloseHandles();
  if (!stopped_server.empty()) {
    rate_limiter_.Reset(stopped_server);
    logger_.Log("mcp_server_stopped", stopped_server);
  }

  active_server_.clear();
  read_buffer_.clear();
}

void McpClient::CloseHandles() {
  CloseHandleIfOpen(&stdin_write_);
  CloseHandleIfOpen(&stdout_read_);
  CloseHandleIfOpen(&process_);
}

bool McpClient::IsRunning() const {
  return process_ &&
      WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

std::string McpClient::active_server() const {
  return active_server_;
}

bool McpClient::SendRequest(
    std::string_view server_id,
    std::string_view json_line,
    std::string* response,
    std::chrono::milliseconds timeout) {
  logger_.Log(
      "mcp_request", RequestDetail(server_id, json_line.size()));

  if (json_line.empty() ||
      json_line.size() > kMaxMessageBytes ||
      json_line.find('\n') != std::string_view::npos ||
      json_line.find('\r') != std::string_view::npos) {

    logger_.Log(
        "mcp_request_rejected_invalid_frame",
        SafeLogToken(server_id));
    return false;
  }

  if (!IsRunning() || active_server_ != server_id) {
    logger_.Log(
        "mcp_request_rejected_not_running",
        SafeLogToken(server_id));
    return false;
  }

  if (!allowlist_.IsAllowed(server_id)) {
    logger_.Log(
        "mcp_request_rejected_not_allowlisted",
        SafeLogToken(server_id));
    Stop();
    return false;
  }

  if (!rate_limiter_.Allow(server_id)) {
    logger_.Log(
        "mcp_request_rate_limited",
        SafeLogToken(server_id));
    return false;
  }

  std::string frame(json_line);
  frame.push_back('\n');
  if (!WriteAll(frame)) {
    logger_.Log(
        "mcp_request_write_failed",
        SafeLogToken(server_id));
    return false;
  }

  logger_.Log("mcp_request_sent", SafeLogToken(server_id));

  if (!response) {
    return true;
  }

  response->clear();
  if (!ReadLine(response, timeout)) {
    logger_.Log(
        "mcp_response_read_failed",
        SafeLogToken(server_id));
    return false;
  }

  logger_.Log(
      "mcp_response_received",
      RequestDetail(server_id, response->size()));
  return true;
}

bool McpClient::WriteAll(std::string_view bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const DWORD remaining = static_cast<DWORD>(
        std::min<std::size_t>(
            bytes.size() - offset, MAXDWORD));
    DWORD written = 0;
    if (!WriteFile(
            stdin_write_, bytes.data() + offset,
            remaining, &written, nullptr) ||
        written == 0) {
      return false;
    }

    offset += written;
  }
  return true;
}

bool McpClient::ReadLine(
    std::string* line,
    std::chrono::milliseconds timeout) {
  const auto deadline =
      std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() <= deadline) {
    const auto newline = read_buffer_.find('\n');
    if (newline != std::string::npos) {
      *line = read_buffer_.substr(0, newline);
      read_buffer_.erase(0, newline + 1);
      if (!line->empty() && line->back() == '\r') {
        line->pop_back();
      }
      return true;
    }

    if (read_buffer_.size() > kMaxMessageBytes) {
      return false;
    }

    DWORD available = 0;
    if (!PeekNamedPipe(
            stdout_read_, nullptr, 0,
            nullptr, &available, nullptr)) {
      return false;
    }

    if (available > 0) {
      std::array<char, 4096> buffer{};
      const DWORD requested = std::min<DWORD>(
          available, static_cast<DWORD>(buffer.size()));
      DWORD received = 0;
      if (!ReadFile(
              stdout_read_, buffer.data(), requested,
              &received, nullptr)) {
        return false;
      }
      read_buffer_.append(buffer.data(), received);
      continue;
    }

    if (process_ &&
        WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) {
      return false;
    }

    Sleep(2);
  }

  return false;
}

}  // namespace cx::mcp
