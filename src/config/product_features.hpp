#pragma once

// Community is postponed to 1.1 after a physical ESP32-S3 inflater-state overwrite.
// Opt-in is for development tests only; production 1.0 must keep this disabled.
#ifndef OPENTAG_ENABLE_COMMUNITY
#define OPENTAG_ENABLE_COMMUNITY 0
#endif

namespace opentag::config {
inline constexpr bool community_enabled = OPENTAG_ENABLE_COMMUNITY != 0;
}

#define OPENTAG_STRINGIFY_FEATURE_(value) #value
#define OPENTAG_STRINGIFY_FEATURE(value) OPENTAG_STRINGIFY_FEATURE_(value)
#define OPENTAG_COMMUNITY_VALUE OPENTAG_STRINGIFY_FEATURE(OPENTAG_ENABLE_COMMUNITY)
