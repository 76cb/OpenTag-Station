#pragma once

#include "config/schema.hpp"

#ifndef OPENTAG_PROJECT_VERSION
#define OPENTAG_PROJECT_VERSION "unknown"
#endif
#ifndef OPENTAG_GIT_SHA
#define OPENTAG_GIT_SHA "unknown"
#endif
#ifndef OPENTAG_BUILD_DATE
#define OPENTAG_BUILD_DATE "unknown"
#endif
#ifndef OPENTAG_ESP32_PLATFORM
#define OPENTAG_ESP32_PLATFORM "unknown"
#endif
#ifndef OPENTAG_ARDUINO_FRAMEWORK
#define OPENTAG_ARDUINO_FRAMEWORK "unknown"
#endif
#ifndef OPENTAG_RFAL_REVISION
#define OPENTAG_RFAL_REVISION "unknown"
#endif
#ifndef OPENTAG_OPENPRINTTAG_REVISION
#define OPENTAG_OPENPRINTTAG_REVISION "unknown"
#endif

namespace opentag::diagnostics {

struct BuildInfo {
  const char* project_version;
  const char* git_sha;
  const char* build_date;
  const char* esp32_platform;
  const char* arduino_framework;
  const char* rfal_revision;
  const char* openprinttag_revision;
  int config_schema;
};

inline constexpr BuildInfo build_info = {
    OPENTAG_PROJECT_VERSION,
    OPENTAG_GIT_SHA,
    OPENTAG_BUILD_DATE,
    OPENTAG_ESP32_PLATFORM,
    OPENTAG_ARDUINO_FRAMEWORK,
    OPENTAG_RFAL_REVISION,
    OPENTAG_OPENPRINTTAG_REVISION,
    config::schema_version,
};

}  // namespace opentag::diagnostics
