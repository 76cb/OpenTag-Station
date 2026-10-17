#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "config/configuration_service.hpp"
#include "integrations/inventory.hpp"
#include "network/http_transport.hpp"

namespace opentag::services { class TagWriterService; }

namespace opentag::integrations::spoolman {

struct SpoolmanStatus {
  bool connected{false};
  bool healthy{false};
  bool version_formally_tested{false};
  std::string version;
  std::string git_commit;
  BackendCapabilities capabilities;
  // Whether the configured identity keys exist as Spool text extra fields.
  // Unset until a full probe has read /field/spool.
  // True when the station can link tags to spools: either both extra fields
  // exist, or Spoolman links tags itself (BackendCapability::native_tags).
  std::optional<bool> identity_fields_ready;
  // Whether the two extra fields themselves exist.
  std::optional<bool> extra_identity_fields;
  std::string identity_fields_message;
  std::optional<core::Error> last_error;
};

class SpoolmanAdapter final : public ISpoolInventory {
 public:
  SpoolmanAdapter(
      network::IHttpTransport& transport,
      config::SpoolmanSettings settings)
      : transport_(transport), settings_(std::move(settings)) {}

  void configure(config::SpoolmanSettings settings);
  [[nodiscard]] core::Result<SpoolmanStatus> probe(bool full = true);
  [[nodiscard]] SpoolmanStatus status() const { return status_; }

  [[nodiscard]] core::Result<std::vector<domain::Spool>> list_spools() override;
  [[nodiscard]] core::Result<std::vector<domain::Spool>> find_spools(
      const SpoolFilter& filter) override;
  [[nodiscard]] core::Result<domain::Spool> get_spool(
      domain::SpoolId id) override;
  [[nodiscard]] core::Result<domain::Spool> create_spool(
      const CreateSpoolRequest& request) override;
  [[nodiscard]] core::Result<domain::Spool> set_remaining_weight(
      domain::SpoolId id,
      const RemainingWeightUpdate& update) override;
  [[nodiscard]] core::Result<std::vector<std::string>> list_locations() override;
  [[nodiscard]] core::Result<std::vector<ExtraFieldDefinition>>
  list_extra_fields() override;
  [[nodiscard]] BackendCapabilities capabilities() const override {
    return status_.capabilities;
  }

  // Empty when every key exists as a Spool text extra field; otherwise a
  // user-facing explanation of which fields to create in Spoolman.
  [[nodiscard]] static std::string text_field_problem(
      const std::vector<ExtraFieldDefinition>& fields,
      const std::vector<std::string>& keys);

  [[nodiscard]] core::Result<domain::Spool> set_extra_field(
      domain::SpoolId id,
      const std::string& key,
      const std::optional<std::string>& json_encoded_value);

  // Spoolman 0.27+: tells Spoolman a tag was placed on this station so a
  // paired Spoolman page can jump to its spool. Best effort; never fails and
  // never changes status().
  void report_tag_scan(const std::string& uid, const std::string& reader_id,
                       const std::string& reader_name);

 private:
  friend class opentag::services::TagWriterService;
  [[nodiscard]] core::Result<network::HttpResponse> request(
      const std::string& method,
      const std::string& endpoint,
      const std::string& body = {},
      std::size_t maximum_response_bytes = 32768U);
  // Like request(), but any HTTP status is returned to the caller, which
  // needs the status and body of 404/409 tag responses.
  [[nodiscard]] core::Result<network::HttpResponse> request_any_status(
      const std::string& method,
      const std::string& endpoint,
      const std::string& body = {},
      std::size_t maximum_response_bytes = 32768U);
  [[nodiscard]] network::HttpRequest build_request(
      const std::string& method,
      const std::string& endpoint,
      const std::string& body,
      std::size_t maximum_response_bytes) const;
  [[nodiscard]] core::Result<std::vector<domain::Spool>> parse_spool_list(
      network::ResponseBody& body) const;
  [[nodiscard]] core::Result<domain::Spool> parse_spool(
      network::ResponseBody& body) const;
  [[nodiscard]] core::Result<void> probe_read_capabilities();
  // Spoolman silently ignores extra.<key> filters for undefined fields, which
  // would return arbitrary spools. Refuse such queries until every filtered
  // key is known to exist as a Spool text field.
  [[nodiscard]] core::Result<void> require_extra_filters_defined(
      const std::map<std::string, std::string>& filters);
  [[nodiscard]] std::string endpoint(const std::string& path) const;

  network::IHttpTransport& transport_;
  config::SpoolmanSettings settings_;
  SpoolmanStatus status_;
  std::set<std::string> verified_extra_keys_;
};

}  // namespace opentag::integrations::spoolman
