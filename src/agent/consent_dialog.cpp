#include "agent/consent_dialog.h"

#include <string>

namespace cx::agent {

bool ConsentDialog::Request(
    HWND owner, const CapabilityDescriptor& capability) {
  std::wstring message =
      L"CX Agent is requesting this capability:\n\n";
  message += capability.title;
  message += L"\n\n";
  message += capability.description;
  message +=
      L"\n\nThis capability is denied by default. "
      L"Choose Yes to allow it or No to keep it denied.";

  const int result = MessageBoxW(
      owner, message.c_str(), L"CX Agent Permission",
      MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_APPLMODAL);
  return result == IDYES;
}

bool ConsentDialog::ConfirmRevokeAll(
    HWND owner, std::size_t granted_count) const {
  std::wstring message =
      L"Revoke all granted CX Agent permissions?\n\nGranted: ";
  message += std::to_wstring(granted_count);
  message +=
      L"\n\nThe agent will stop and every capability will "
      L"return to denied until explicitly approved again.";

  const int result = MessageBoxW(
      owner, message.c_str(), L"Revoke CX Agent Permissions",
      MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2 | MB_APPLMODAL);
  return result == IDOK;
}

}  // namespace cx::agent
