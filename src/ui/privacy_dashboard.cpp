#include "ui/privacy_dashboard.h"

#include "agent/permissions.h"
#include "mcp/allowlist_manager.h"
#include "mcp/mcp_client.h"

#include <sstream>
#include <thread>
#include <utility>

namespace cx::ui {

PrivacyDashboard::PrivacyDashboard(
    agent::PermissionManager& permissions,
    mcp::AllowlistManager& allowlist,
    mcp::McpClient& mcp_client,
    std::filesystem::path local_root)
    : permissions_(permissions),
      allowlist_(allowlist),
      mcp_client_(mcp_client),
      local_root_(std::move(local_root)) {}

std::wstring PrivacyDashboard::Summary() const {
  std::wostringstream output;

  const auto granted = permissions_.Granted();
  output << L"Enabled permissions: " << granted.size();
  if (!granted.empty()) {
    output << L" (";
    for (std::size_t i = 0; i < granted.size(); ++i) {
      if (i != 0) {
        output << L", ";
      }
      const auto id = agent::DescribeCapability(granted[i]).id;
      output << std::wstring(id.begin(), id.end());
    }
    output << L")";
  }

  output << L"\r\nMCP active: ";
  if (mcp_client_.IsRunning()) {
    const auto id = mcp_client_.active_server();
    output << std::wstring(id.begin(), id.end());
  } else {
    output << L"none";
  }

  output << L"\r\nMCP allowlist entries: "
         << allowlist_.List().size();

  output << L"\r\nLocal data: ";
  if (local_size_.has_value()) {
    output << FormatBytes(*local_size_);
  } else {
    output << L"calculating...";
  }

  return output.str();
}

void PrivacyDashboard::RefreshLocalSizeAsync(
    HWND target,
    UINT completion_message) const {
  const auto root = local_root_;
  std::thread(
      [root, target, completion_message]() {
        auto* result = new std::uintmax_t(
            ComputeDirectorySize(root));
        if (!PostMessageW(
                target,
                completion_message,
                0,
                reinterpret_cast<LPARAM>(result))) {
          delete result;
        }
      })
      .detach();
}

bool PrivacyDashboard::AcceptLocalSizeResult(LPARAM lparam) {
  auto* result =
      reinterpret_cast<std::uintmax_t*>(lparam);
  if (!result) {
    return false;
  }
  local_size_ = *result;
  delete result;
  return true;
}

std::optional<std::uintmax_t>
PrivacyDashboard::local_size() const noexcept {
  return local_size_;
}

const std::filesystem::path&
PrivacyDashboard::local_root() const noexcept {
  return local_root_;
}

std::uintmax_t PrivacyDashboard::ComputeDirectorySize(
    const std::filesystem::path& root) {
  std::error_code error;
  if (!std::filesystem::exists(root, error)) {
    return 0;
  }

  std::uintmax_t total = 0;
  std::filesystem::recursive_directory_iterator iterator(
      root,
      std::filesystem::directory_options::skip_permission_denied,
      error);
  const std::filesystem::recursive_directory_iterator end;

  while (!error && iterator != end) {
    if (iterator->is_regular_file(error) && !error) {
      const auto size = iterator->file_size(error);
      if (!error) {
        total += size;
      }
    }
    error.clear();
    iterator.increment(error);
  }
  return total;
}

std::wstring PrivacyDashboard::FormatBytes(
    std::uintmax_t bytes) {
  const wchar_t* units[] = {
      L"B", L"KiB", L"MiB", L"GiB"};
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    ++unit;
  }

  std::wostringstream output;
  output.setf(std::ios::fixed);
  output.precision(unit == 0 ? 0 : 2);
  output << value << L" " << units[unit];
  return output.str();
}

}  // namespace cx::ui
