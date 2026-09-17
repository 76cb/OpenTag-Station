#pragma once

#include <vector>

#include "core/result.hpp"
#include "domain/spool_identity.hpp"

namespace opentag::services {

class ISpoolIdentityMappingStore {
 public:
  virtual ~ISpoolIdentityMappingStore() = default;
  [[nodiscard]] virtual core::Result<std::vector<domain::ConfirmedSpoolMapping>>
  load_spool_identity_mappings() = 0;
  [[nodiscard]] virtual core::Result<void> confirm_spool_identity_mapping(
      const domain::ConfirmedSpoolMapping& mapping) = 0;
  [[nodiscard]] virtual core::Result<void> clear_spool_identity_mapping(
      const std::string&, const std::string&, std::int32_t) {
    return core::Result<void>::failure({core::ErrorCategory::configuration,
        "Local identity cleanup unsupported", false});
  }
};

}  // namespace opentag::services
