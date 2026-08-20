#pragma once

#ifndef OPENTAG_CONFIG_SCHEMA_VERSION
#define OPENTAG_CONFIG_SCHEMA_VERSION 3
#endif

namespace opentag::config {

inline constexpr int schema_version = OPENTAG_CONFIG_SCHEMA_VERSION;
inline constexpr int minimum_readable_schema = 1;
inline constexpr int maximum_readable_schema = 2;

}  // namespace opentag::config
