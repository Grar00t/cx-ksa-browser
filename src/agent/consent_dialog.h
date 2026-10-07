#pragma once

#include "agent/permissions.h"

#include <cstddef>

namespace cx::agent {

class ConsentDialog final : public ConsentPrompt {
public:
  bool Request(HWND owner,
               const CapabilityDescriptor& capability) override;

  bool ConfirmRevokeAll(HWND owner,
                        std::size_t granted_count) const;
};

}  // namespace cx::agent
