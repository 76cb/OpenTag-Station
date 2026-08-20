#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "domain/spool.hpp"

namespace opentag::domain {

struct SpoolIdentity {
  std::optional<std::string> instance_uuid;
  std::optional<std::string> package_uuid;
  std::optional<std::string> material_uuid;
  std::optional<std::uint64_t> gtin;
  std::optional<std::string> brand_specific_package_id;
  std::optional<std::string> brand_specific_material_id;
  std::optional<std::string> brand_name;
  std::optional<std::string> material_name;
  std::optional<std::string> material_abbreviation;
  std::optional<std::string> nfc_uid;
};

struct ConfirmedSpoolMapping {
  SpoolId spool_id{0};
  std::optional<std::string> instance_uuid;
  std::optional<std::string> nfc_uid;
};

}  // namespace opentag::domain
