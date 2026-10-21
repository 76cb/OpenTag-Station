#pragma once
#include "nfc/openprinttag_writer.hpp"
#include <ArduinoJson.h>
#include <string>
#include <vector>
namespace opentag::nfc::openprinttag {
// A Spoolman value that a full write (initialize/rewrite) leaves off the tag.
// map_spoolman() never truncates: an over-length or unmappable value is
// omitted, and this list is how the preview tells the user so.
struct DroppedField {
  const char *field;   // name, vendor, material, material_type,
                       // article_number or color
  std::string value;   // the Spoolman value (bounded for display)
  std::string reason;  // e.g. "longer than 7 characters"
  std::string warning; // one complete sentence for the preview warnings
};
// Same input and limits as map_spoolman(); no tag bytes are touched.
std::vector<DroppedField> dropped_fields(JsonObjectConst spool);
// Input MUST be GET /spool/{id} canonical readback, never a client-supplied object.
core::Result<void> map_spoolman(JsonObjectConst spool,
                                const std::array<std::uint8_t, 16> &uuid,
                                WriterPlan &plan, bool mutable_only);
core::Result<std::array<std::uint8_t, 16>>
parse_instance_uuid(const std::string &text);
std::string instance_uuid_text(const std::array<std::uint8_t, 16> &uuid);
} // namespace opentag::nfc::openprinttag
