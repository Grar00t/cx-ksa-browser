#pragma once

#include "agent/permissions.h"

#include <cstddef>

namespace cx::ui {

class PermissionDialog final : public agent::ConsentPrompt {
public:
  bool Request(
      HWND owner,
      const agent::CapabilityDescriptor& capability) override;

  bool ConfirmRevokeAll(
      HWND owner,
      std::size_t granted_count) const;
};

}  // namespace cx::ui
