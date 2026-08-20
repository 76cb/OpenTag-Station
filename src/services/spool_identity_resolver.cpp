#include "services/spool_identity_resolver.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace opentag::services {
namespace {

core::Error invalid_identity(const std::string& message) {
  return {core::ErrorCategory::configuration, message, false};
}

std::string uuid_text(const std::array<std::uint8_t, 16>& uuid) {
  static constexpr std::array<std::size_t, 4> separators{4U, 6U, 8U, 10U};
  std::ostringstream output;
  output << std::hex << std::nouppercase << std::setfill('0');
  for (std::size_t index = 0U; index < uuid.size(); ++index) {
    if (std::find(separators.begin(), separators.end(), index) != separators.end()) {
      output << '-';
    }
    output << std::setw(2) << static_cast<unsigned>(uuid[index]);
  }
  return output.str();
}

std::string json_string(const std::string& value) {
  JsonDocument encoded;
  encoded.set(value);
  std::string result;
  serializeJson(encoded, result);
  return result;
}

std::optional<std::string> decode_scalar(const std::string& encoded) {
  JsonDocument document;
  if (deserializeJson(document, encoded)) return std::nullopt;
  if (document.is<const char*>()) return std::string(document.as<const char*>());
  if (document.is<std::uint64_t>()) {
    return std::to_string(document.as<std::uint64_t>());
  }
  return std::nullopt;
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  });
  return value;
}

bool equal_text(const std::string& left, const std::string& right) {
  return lowercase(left) == lowercase(right);
}

void add_identifier(std::set<std::string>& output, const std::optional<std::string>& value) {
  if (value.has_value() && !value->empty()) output.insert(lowercase(*value));
}

bool matches_any_identifier(
    const domain::Spool& spool,
    const domain::SpoolIdentity& identity) {
  std::set<std::string> identifiers;
  if (identity.gtin.has_value()) identifiers.insert(std::to_string(*identity.gtin));
  add_identifier(identifiers, identity.package_uuid);
  add_identifier(identifiers, identity.material_uuid);
  add_identifier(identifiers, identity.brand_specific_package_id);
  add_identifier(identifiers, identity.brand_specific_material_id);
  if (identifiers.empty()) return false;
  if (spool.article_number.has_value() &&
      identifiers.count(lowercase(*spool.article_number)) != 0U) {
    return true;
  }
  static constexpr std::array<const char*, 7> keys{
      "gtin",
      "opentag_package_uuid",
      "opentag_material_uuid",
      "package_uuid",
      "material_uuid",
      "package_id",
      "material_id",
  };
  for (const auto* key : keys) {
    const auto found = spool.extra_json.find(key);
    if (found == spool.extra_json.end()) continue;
    const auto decoded = decode_scalar(found->second);
    if (decoded.has_value() && identifiers.count(lowercase(*decoded)) != 0U) {
      return true;
    }
  }
  return false;
}

SpoolResolution resolved(
    SpoolResolutionStatus status,
    SpoolMatchSource source,
    std::vector<domain::Spool> candidates = {}) {
  return {status, source, std::move(candidates)};
}

}  // namespace

domain::SpoolIdentity identity_from_openprinttag(
    const nfc::openprinttag::MaterialRecord& material,
    const nfc::nfcv::Uid& uid) {
  domain::SpoolIdentity result;
  if (material.instance_uuid.has_value()) {
    result.instance_uuid = uuid_text(*material.instance_uuid);
  }
  if (material.package_uuid.has_value()) {
    result.package_uuid = uuid_text(*material.package_uuid);
  }
  if (material.material_uuid.has_value()) {
    result.material_uuid = uuid_text(*material.material_uuid);
  }
  result.gtin = material.gtin;
  result.brand_specific_package_id = material.brand_specific_package_id;
  result.brand_specific_material_id = material.brand_specific_material_id;
  result.brand_name = material.brand_name;
  result.material_name = material.material_name;
  result.material_abbreviation = material.material_abbreviation;
  result.nfc_uid = uid.hex();
  return result;
}

core::Result<SpoolResolution> SpoolIdentityResolver::exact_extra_match(
    const std::string& key,
    const std::string& value,
    SpoolMatchSource source) {
  integrations::SpoolFilter filter;
  filter.extra_json[key] = json_string(value);
  filter.allow_archived = true;
  filter.maximum_results = 3U;
  auto matches = inventory_.find_spools(filter);
  if (!matches.ok()) {
    return core::Result<SpoolResolution>::failure(matches.error());
  }
  if (matches.value().empty()) {
    return core::Result<SpoolResolution>::success(
        resolved(SpoolResolutionStatus::not_found, source));
  }
  if (matches.value().size() > 1U) {
    return core::Result<SpoolResolution>::success(resolved(
        SpoolResolutionStatus::conflict, source, matches.value()));
  }
  return core::Result<SpoolResolution>::success(resolved(
      SpoolResolutionStatus::matched, source, matches.value()));
}

core::Result<SpoolResolution> SpoolIdentityResolver::cached_match(
    const domain::SpoolIdentity& identity,
    bool by_nfc_uid) {
  const auto stored = mappings_.load_spool_identity_mappings();
  if (!stored.ok()) return core::Result<SpoolResolution>::failure(stored.error());
  std::set<domain::SpoolId> spool_ids;
  for (const auto& mapping : stored.value()) {
    const bool matches = by_nfc_uid
        ? identity.nfc_uid.has_value() && mapping.nfc_uid.has_value() &&
            equal_text(*mapping.nfc_uid, *identity.nfc_uid)
        : identity.instance_uuid.has_value() &&
            mapping.instance_uuid.has_value() &&
            equal_text(*mapping.instance_uuid, *identity.instance_uuid);
    if (matches) spool_ids.insert(mapping.spool_id);
  }
  if (spool_ids.empty()) {
    return core::Result<SpoolResolution>::success(resolved(
        SpoolResolutionStatus::not_found,
        by_nfc_uid ? SpoolMatchSource::nfc_uid
                   : SpoolMatchSource::confirmed_identity_cache));
  }
  std::vector<domain::Spool> candidates;
  for (const auto id : spool_ids) {
    const auto spool = inventory_.get_spool(id);
    if (!spool.ok()) return core::Result<SpoolResolution>::failure(spool.error());
    candidates.push_back(spool.value());
  }
  const auto source = by_nfc_uid ? SpoolMatchSource::nfc_uid
                                 : SpoolMatchSource::confirmed_identity_cache;
  if (candidates.size() > 1U) {
    return core::Result<SpoolResolution>::success(resolved(
        SpoolResolutionStatus::conflict, source, std::move(candidates)));
  }
  if (identity.instance_uuid.has_value() &&
      candidates.front().openprinttag_instance_uuid.has_value() &&
      !equal_text(
          *identity.instance_uuid,
          *candidates.front().openprinttag_instance_uuid)) {
    return core::Result<SpoolResolution>::success(resolved(
        SpoolResolutionStatus::conflict, source, std::move(candidates)));
  }
  return core::Result<SpoolResolution>::success(resolved(
      SpoolResolutionStatus::matched, source, std::move(candidates)));
}

core::Result<SpoolResolution> SpoolIdentityResolver::resolve(
    const domain::SpoolIdentity& identity) {
  if (identity.instance_uuid.has_value()) {
    auto result = exact_extra_match(
        settings_.identity_field,
        *identity.instance_uuid,
        SpoolMatchSource::configured_identity_field);
    if (!result.ok() || result.value().status != SpoolResolutionStatus::not_found) {
      return result;
    }
    result = cached_match(identity, false);
    if (!result.ok() || result.value().status != SpoolResolutionStatus::not_found) {
      return result;
    }
  }

  if (identity.nfc_uid.has_value()) {
    auto result = exact_extra_match(
        settings_.nfc_uid_field, *identity.nfc_uid, SpoolMatchSource::nfc_uid);
    if (!result.ok() || result.value().status != SpoolResolutionStatus::not_found) {
      return result;
    }
    result = cached_match(identity, true);
    if (!result.ok() || result.value().status != SpoolResolutionStatus::not_found) {
      return result;
    }
  }

  integrations::SpoolFilter filter;
  filter.vendor_name = identity.brand_name;
  filter.material = identity.material_abbreviation.has_value()
                        ? identity.material_abbreviation
                        : identity.material_name;
  filter.allow_archived = false;
  filter.maximum_results = 128U;
  auto candidates = inventory_.find_spools(filter);
  if (!candidates.ok()) {
    return core::Result<SpoolResolution>::failure(candidates.error());
  }

  std::vector<domain::Spool> identifier_matches;
  for (const auto& spool : candidates.value()) {
    if (matches_any_identifier(spool, identity)) {
      identifier_matches.push_back(spool);
    }
  }
  if (identifier_matches.size() == 1U) {
    return core::Result<SpoolResolution>::success(resolved(
        SpoolResolutionStatus::matched,
        SpoolMatchSource::package_or_material_identity,
        std::move(identifier_matches)));
  }
  if (identifier_matches.size() > 1U) {
    return core::Result<SpoolResolution>::success(resolved(
        SpoolResolutionStatus::ambiguous,
        SpoolMatchSource::package_or_material_identity,
        std::move(identifier_matches)));
  }

  std::vector<domain::Spool> metadata_matches;
  for (const auto& spool : candidates.value()) {
    const bool vendor_matches = !identity.brand_name.has_value() ||
        equal_text(spool.vendor, *identity.brand_name);
    const auto& material = identity.material_abbreviation.has_value()
                               ? identity.material_abbreviation
                               : identity.material_name;
    const bool material_matches = !material.has_value() ||
        equal_text(spool.material, *material) || equal_text(spool.subtype, *material);
    if (vendor_matches && material_matches) metadata_matches.push_back(spool);
  }
  if (metadata_matches.size() == 1U) {
    return core::Result<SpoolResolution>::success(resolved(
        SpoolResolutionStatus::matched,
        SpoolMatchSource::metadata,
        std::move(metadata_matches)));
  }
  if (metadata_matches.size() > 1U) {
    return core::Result<SpoolResolution>::success(resolved(
        SpoolResolutionStatus::ambiguous,
        SpoolMatchSource::metadata,
        std::move(metadata_matches)));
  }
  return core::Result<SpoolResolution>::success(
      resolved(SpoolResolutionStatus::not_found, SpoolMatchSource::none));
}

core::Result<void> SpoolIdentityResolver::confirm(
    const domain::SpoolIdentity& identity,
    domain::SpoolId spool_id) {
  if (spool_id <= 0 ||
      (!identity.instance_uuid.has_value() && !identity.nfc_uid.has_value())) {
    return core::Result<void>::failure(
        invalid_identity("confirmed mapping requires a spool and stable identity"));
  }
  domain::ConfirmedSpoolMapping mapping;
  mapping.spool_id = spool_id;
  mapping.instance_uuid = identity.instance_uuid;
  mapping.nfc_uid = identity.nfc_uid;
  return mappings_.confirm_spool_identity_mapping(mapping);
}

}  // namespace opentag::services
