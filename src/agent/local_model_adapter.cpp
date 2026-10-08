#include "agent/local_model_adapter.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

namespace cx::agent {
namespace {

constexpr std::size_t kMaxResponseBytes = 4 * 1024 * 1024;

struct ParsedEndpoint {
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = 0;
  bool secure = false;
};

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty() ||
      value.size() > static_cast<std::size_t>(INT_MAX)) {
    return {};
  }
  const int count = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), result.data(), count) != count) {
    return {};
  }
  return result;
}

bool ParseEndpoint(
    std::string_view value,
    ParsedEndpoint* parsed) {
  const std::wstring wide = Utf8ToWide(value);
  if (wide.empty() || !parsed) {
    return false;
  }

  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  parts.dwUserNameLength = static_cast<DWORD>(-1);
  parts.dwPasswordLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(
          wide.c_str(), static_cast<DWORD>(wide.size()),
          ICU_REJECT_USERPWD, &parts) ||
      parts.dwHostNameLength == 0 ||
      (parts.nScheme != INTERNET_SCHEME_HTTP &&
       parts.nScheme != INTERNET_SCHEME_HTTPS)) {
    return false;
  }

  parsed->host.assign(parts.lpszHostName, parts.dwHostNameLength);
  std::transform(
      parsed->host.begin(), parsed->host.end(), parsed->host.begin(),
      [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z'
            ? static_cast<wchar_t>(ch - L'A' + L'a')
            : ch;
      });
  parsed->path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.dwExtraInfoLength > 0) {
    parsed->path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  }
  if (parsed->path.empty()) {
    parsed->path = L"/";
  }
  parsed->port = parts.nPort;
  parsed->secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  return true;
}

bool IsLoopbackHost(std::wstring_view host) {
  return host == L"127.0.0.1" || host == L"::1";
}

std::string EscapeJson(std::string_view value) {
  std::string output;
  output.reserve(value.size() + 16);
  constexpr char hex[] = "0123456789abcdef";
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
          output += "\\u00";
          output.push_back(hex[ch >> 4]);
          output.push_back(hex[ch & 0x0f]);
        } else {
          output.push_back(static_cast<char>(ch));
        }
    }
  }
  return output;
}

}  // namespace

LocalModelAdapter::LocalModelAdapter(
    std::string endpoint,
    std::string model,
    LocalModelPolicy policy)
    : endpoint_(std::move(endpoint)),
      model_(std::move(model)),
      policy_(policy) {}

bool LocalModelAdapter::IsLoopbackEndpoint(
    std::string_view endpoint) noexcept {
  ParsedEndpoint parsed;
  return ParseEndpoint(endpoint, &parsed) &&
      IsLoopbackHost(parsed.host);
}

bool LocalModelAdapter::enabled() const noexcept {
  if (!policy_.enabled) {
    return false;
  }
  if (IsLoopbackEndpoint(endpoint_)) {
    return true;
  }
  return policy_.allow_non_loopback &&
      policy_.non_loopback_warning_acknowledged;
}

bool LocalModelAdapter::uses_explicit_non_loopback_override() const noexcept {
  return enabled() && !IsLoopbackEndpoint(endpoint_);
}

bool LocalModelAdapter::Complete(
    std::string_view prompt,
    std::string* response,
    std::chrono::milliseconds timeout) const {
  if (!response || !enabled() || model_.empty() ||
      prompt.size() > 1024 * 1024 ||
      timeout <= std::chrono::milliseconds::zero()) {
    return false;
  }
  response->clear();

  ParsedEndpoint parsed;
  if (!ParseEndpoint(endpoint_, &parsed)) {
    return false;
  }

  HINTERNET session = WinHttpOpen(
      L"CXBuild-LocalModel/1.0",
      WINHTTP_ACCESS_TYPE_NO_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    return false;
  }
  const int timeout_ms = static_cast<int>(
      std::clamp<long long>(timeout.count(), 1, INT_MAX));
  WinHttpSetTimeouts(
      session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  HINTERNET connection = WinHttpConnect(
      session, parsed.host.c_str(), parsed.port, 0);
  HINTERNET request = connection
      ? WinHttpOpenRequest(
            connection, L"POST", parsed.path.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            parsed.secure ? WINHTTP_FLAG_SECURE : 0)
      : nullptr;
  if (!request) {
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return false;
  }

  DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
  WinHttpSetOption(
      request, WINHTTP_OPTION_DISABLE_FEATURE,
      &disabled, sizeof(disabled));

  const std::string body =
      "{\"model\":\"" + EscapeJson(model_) +
      "\",\"messages\":[{\"role\":\"user\",\"content\":\"" +
      EscapeJson(prompt) + "\"}],\"stream\":false}";
  const wchar_t headers[] = L"Content-Type: application/json\r\n";
  bool ok = WinHttpSendRequest(
      request, headers, static_cast<DWORD>(-1),
      const_cast<char*>(body.data()),
      static_cast<DWORD>(body.size()),
      static_cast<DWORD>(body.size()), 0) &&
      WinHttpReceiveResponse(request, nullptr);

  DWORD status = 0;
  DWORD status_bytes = sizeof(status);
  ok = ok && WinHttpQueryHeaders(
      request,
      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX,
      &status, &status_bytes, WINHTTP_NO_HEADER_INDEX) &&
      status >= 200 && status < 300;

  while (ok) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available)) {
      ok = false;
      break;
    }
    if (available == 0) {
      break;
    }
    if (response->size() + available > kMaxResponseBytes) {
      ok = false;
      break;
    }
    const std::size_t offset = response->size();
    response->resize(offset + available);
    DWORD received = 0;
    if (!WinHttpReadData(
            request, response->data() + offset,
            available, &received)) {
      ok = false;
      break;
    }
    response->resize(offset + received);
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  if (!ok) {
    response->clear();
  }
  return ok;
}

}  // namespace cx::agent
