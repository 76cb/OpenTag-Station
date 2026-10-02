#pragma once

#include <string>
#include <utility>
#include <vector>

#include "config/configuration_service.hpp"
#include "core/result.hpp"
#include "domain/spool.hpp"
#include "domain/spool_identity.hpp"
#include "integrations/inventory.hpp"
#include "nfc/formats/openprinttag/codec.hpp"
#include "nfc/protocols/nfcv/tag.hpp"
#include "services/spool_identity_store.hpp"

namespace opentag::services {

enum class SpoolMatchSource {
  none,
  configured_identity_field,
  confirmed_identity_cache,
  nfc_uid,
  package_or_material_identity,
  metadata,
};

enum class SpoolResolutionStatus {
  matched,
  ambiguous,
  not_found,
  conflict,
};

struct SpoolResolution {
  SpoolResolutionStatus status{SpoolResolutionStatus::not_found};
  SpoolMatchSource source{SpoolMatchSource::none};
  std::vector<domain::Spool> candidates;

  [[nodiscard]] const domain::Spool* match() const {
    return status == SpoolResolutionStatus::matched && candidates.size() == 1U
               ? &candidates.front()
               : nullptr;
  }
};

[[nodiscard]] domain::SpoolIdentity identity_from_openprinttag(
    const nfc::openprinttag::MaterialRecord& material,
    const nfc::nfcv::Uid& uid);

class ISpoolIdentityResolver {
 public:
  virtual ~ISpoolIdentityResolver() = default;
  [[nodiscard]] virtual core::Result<SpoolResolution> resolve(
      const domain::SpoolIdentity& identity) = 0;
};

class SpoolIdentityResolver final : public ISpoolIdentityResolver {
 public:
  SpoolIdentityResolver(
      integrations::ISpoolInventory& inventory,
      ISpoolIdentityMappingStore& mappings,
      config::SpoolmanSettings settings)
      : inventory_(inventory), mappings_(mappings), settings_(std::move(settings)) {}

  void configure(config::SpoolmanSettings settings) {
    settings_ = std::move(settings);
  }
  [[nodiscard]] core::Result<SpoolResolution> resolve(
      const domain::SpoolIdentity& identity) override;
  [[nodiscard]] core::Result<void> confirm(
      const domain::SpoolIdentity& identity,
      domain::SpoolId spool_id);

 private:
  [[nodiscard]] core::Result<SpoolResolution> exact_extra_match(
      const std::string& key,
      const std::string& value,
      SpoolMatchSource source);
  [[nodiscard]] core::Result<SpoolResolution> cached_match(
      const domain::SpoolIdentity& identity,
      bool by_nfc_uid);

  integrations::ISpoolInventory& inventory_;
  ISpoolIdentityMappingStore& mappings_;
  config::SpoolmanSettings settings_;
};

}  // namespace opentag::services
