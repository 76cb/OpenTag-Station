#pragma once
#include <ArduinoJson.h>

#include "nfc/protocols/nfcv/read_protocol.hpp"
#include "nfc/read_only_service.hpp"

namespace opentag::web {
template <class T>
inline void nfc_optional(JsonObject out, const char* key,
                         const std::optional<T>& value) {
  if (value)
    out[key] = *value;
  else
    out[key] = nullptr;
}
inline void write_nfc(JsonObject out, const nfc::ReadSnapshot& status,
                      std::optional<float> measured = std::nullopt) {
  out.clear();
  out["state"] = nfc::to_string(status.state);
  out["enabled"] = true;
  out["available"] = status.initialized;
  const bool deferred = status.state == nfc::ReadState::deferred;
  out["bringup_state"] = deferred ? "deferred" : status.initialized ? "ready" : "initializing_rfal";
  if (deferred) out["reason"] = "provisioning";
  else out["reason"] = nullptr;
  out["present"] = status.present;
  out["generation"] = status.generation;
  out["last_seen_ms"] = status.last_seen_ms;
  out["bus_errors"] = status.bus_errors;
  out["transport"] = "Wire1 I2C 100 kHz";
  out["read_only"] = true;
  out["guarded_writer"] = "/api/v1/tag-writer";
  out["owner_task"] = "opentag-backend";
  out["block_count"] = status.geometry.block_count;
  out["block_size"] = status.geometry.block_size;
  out["bytes"] = status.geometry.capacity();
  if (status.uid)
    out["uid"] = nfc::nfcv::format_diagnostic_uid(status.uid->bytes).data();
  else
    out["uid"] = nullptr;
  if (status.checksum)
    out["checksum"] =
        nfc::nfcv::format_diagnostic_checksum(*status.checksum).data();
  else
    out["checksum"] = nullptr;
  out["decode"] = status.tag                                    ? "pass"
                  : status.state == nfc::ReadState::unsupported ? "fail"
                                                                : "pending";
  if (status.error)
    out["last_error"] = status.error->message;
  else
    out["last_error"] = nullptr;
  auto inventory = out["inventory"].to<JsonObject>();
  inventory["tag_count"] = status.tag_count;
  inventory["present"] = status.present;
  inventory["uid"] = out["uid"];
  inventory["technology"] = "NFC-V / ISO15693";
  auto geometry = out["geometry"].to<JsonObject>();
  geometry["block_size"] = status.geometry.block_size;
  geometry["block_count"] = status.geometry.block_count;
  for (const auto key :
       {"material_name", "material_abbreviation", "material_type", "brand_name",
        "nominal_full_weight", "actual_full_weight", "consumed_weight",
        "remaining_weight", "color"})
    out[key] = nullptr;
  nfc_optional(out, "measured_weight", measured);
  if (status.tag) {
    const auto& m = status.tag->decoded.material;
    nfc_optional(out, "material_name", m.material_name);
    nfc_optional(out, "material_abbreviation", m.material_abbreviation);
    nfc_optional(out, "material_type", m.material_type);
    nfc_optional(out, "brand_name", m.brand_name);
    nfc_optional(out, "nominal_full_weight", m.nominal_netto_full_weight);
    nfc_optional(out, "actual_full_weight", m.actual_netto_full_weight);
    nfc_optional(out, "consumed_weight", m.consumed_weight);
    nfc_optional(out, "remaining_weight", m.remaining_weight());
    if (m.primary_color) {
      auto color = out["color"].to<JsonArray>();
      color.add(m.primary_color->red);
      color.add(m.primary_color->green);
      color.add(m.primary_color->blue);
      color.add(m.primary_color->alpha);
    }
    const auto& e = status.tag->decoded.envelope;
    out["usable_bytes"] = e.capability_capacity;
    out["auxiliary_bytes"] = e.auxiliary ? e.auxiliary->size : 0U;
    out["auxiliary_offset"] = e.auxiliary ? e.auxiliary->absolute_offset : 0U;
    out["identified_at_ms"] = status.tag->identified_at_ms;
  }
}
}  // namespace opentag::web
