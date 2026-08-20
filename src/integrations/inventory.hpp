#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "domain/spool.hpp"
#include "integrations/backend_capabilities.hpp"

namespace opentag::integrations {

struct SpoolFilter {
  std::optional<std::string> filament_name;
  std::optional<std::string> material;
  std::optional<std::string> vendor_name;
  std::optional<std::string> location;
  std::map<std::string, std::string> extra_json;
  bool allow_archived{false};
  std::size_t maximum_results{256U};
};

struct RemainingWeightUpdate {
  float expected_used_grams{0.0F};
  float remaining_grams{0.0F};
  float concurrency_tolerance_grams{0.05F};
  float verification_tolerance_grams{0.25F};
};

struct CreateSpoolRequest {
  std::int32_t filament_id{0};
  std::optional<float> initial_grams;
  std::optional<float> remaining_grams;
  std::optional<float> empty_spool_grams;
  std::optional<std::string> location;
  std::map<std::string, std::string> extra_json;
};

enum class ExtraFieldKind {
  text,
  integer,
  float_number,
  boolean,
  datetime,
  choice,
  unknown,
};

struct ExtraFieldDefinition {
  std::string key;
  std::string name;
  ExtraFieldKind kind{ExtraFieldKind::unknown};
  bool multi_choice{false};
};

class ISpoolInventory {
 public:
  virtual ~ISpoolInventory() = default;
  virtual core::Result<std::vector<domain::Spool>> list_spools() = 0;
  virtual core::Result<std::vector<domain::Spool>> find_spools(
      const SpoolFilter& filter) = 0;
  virtual core::Result<domain::Spool> get_spool(domain::SpoolId id) = 0;
  virtual core::Result<domain::Spool> create_spool(
      const CreateSpoolRequest& request) = 0;
  virtual core::Result<domain::Spool> set_remaining_weight(
      domain::SpoolId id,
      const RemainingWeightUpdate& update) = 0;
  virtual core::Result<std::vector<std::string>> list_locations() = 0;
  virtual core::Result<std::vector<ExtraFieldDefinition>> list_extra_fields() = 0;
  [[nodiscard]] virtual BackendCapabilities capabilities() const = 0;
};

}  // namespace opentag::integrations
