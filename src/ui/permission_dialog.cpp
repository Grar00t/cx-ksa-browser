#include "ui/permission_dialog.h"

#include "localization/strings.h"

#include <string>

namespace cx::ui {

bool PermissionDialog::Request(
    HWND owner,
    const agent::CapabilityDescriptor& capability) {
  std::wstring message =
      L"CX is requesting an explicit permission.\n\n";
  message += capability.title;
  message += L"\n";
  message += L"Capability ID: ";
  message += std::wstring(
      capability.id.begin(), capability.id.end());
  message += L"\n\n";
  message += capability.description;
  message += L"\n\n";
  message += localization::Text(
      localization::StringId::Consent);
  message += L": ";
  message += localization::Text(
      localization::StringId::Denied);
  message +=
      L" by default. Choose Yes only if you want this capability enabled.";

  return MessageBoxW(
             owner,
             message.c_str(),
             L"CX Permission",
             MB_YESNO | MB_ICONWARNING |
                 MB_DEFBUTTON2 | MB_APPLMODAL) == IDYES;
}

bool PermissionDialog::ConfirmRevokeAll(
    HWND owner,
    std::size_t granted_count) const {
  std::wstring message =
      L"Revoke all currently granted agent permissions?\n\nGranted: ";
  message += std::to_wstring(granted_count);
  message +=
      L"\n\nThe agent and MCP connection will be stopped first.";

  return MessageBoxW(
             owner,
             message.c_str(),
             L"Revoke CX Permissions",
             MB_OKCANCEL | MB_ICONWARNING |
                 MB_DEFBUTTON2 | MB_APPLMODAL) == IDOK;
}

}  // namespace cx::ui
