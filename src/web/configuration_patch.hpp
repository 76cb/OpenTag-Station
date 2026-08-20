#pragma once

#include "config/configuration_service.hpp"
#include "core/result.hpp"
#include "web/api_router.hpp"

namespace opentag::web {

[[nodiscard]] core::Result<config::Configuration> apply_configuration_patch(
    const config::VersionedConfiguration& current,
    const api::ConfigurationPatchMutation& patch);

}  // namespace opentag::web
