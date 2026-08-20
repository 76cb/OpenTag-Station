#pragma once

#include <string>

namespace opentag::core {

enum class ErrorCategory {
  network,
  authentication,
  backend_unavailable,
  api_changed,
  invalid_response,
  conflict,
  nfc_communication,
  nfc_crc,
  multiple_tags,
  unsupported_tag,
  invalid_openprinttag,
  tag_write_protected,
  tag_removed,
  scale_unavailable,
  scale_unstable,
  configuration,
  storage,
  firmware_update,
};

struct Error {
  ErrorCategory category;
  std::string message;
  bool retryable{false};
};

}  // namespace opentag::core
