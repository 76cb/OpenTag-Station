#pragma once
#include "nfc/openprinttag_writer.hpp"
#include <ArduinoJson.h>
namespace opentag::nfc::openprinttag {
// Input MUST be GET /spool/{id} canonical readback, never a Community object.
core::Result<void> map_spoolman(JsonObjectConst spool,
                                const std::array<std::uint8_t, 16> &uuid,
                                WriterPlan &plan, bool mutable_only);
core::Result<std::array<std::uint8_t, 16>>
parse_instance_uuid(const std::string &text);
std::string instance_uuid_text(const std::array<std::uint8_t, 16> &uuid);
} // namespace opentag::nfc::openprinttag
