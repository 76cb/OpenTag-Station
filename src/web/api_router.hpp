#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/result.hpp"
#include "network/backend_memory.hpp"

namespace opentag::web::api {

using JsonBody = network::ResponseBody;

inline constexpr char version[] = "v1";
inline constexpr char prefix[] = "/api/v1";
inline constexpr std::size_t maximum_request_body_bytes = 16384U;
inline constexpr std::size_t maximum_snapshot_json_bytes = 24576U;
inline constexpr std::size_t maximum_response_body_bytes = 32768U;
inline constexpr std::size_t maximum_request_path_bytes = 256U;
inline constexpr std::size_t maximum_request_headers = 16U;
inline constexpr std::size_t maximum_request_header_bytes = 1024U;
inline constexpr std::size_t maximum_firmware_image_bytes = 0x500000U;

enum class Method : std::uint8_t {
  get,
  post,
  patch,
  put,
  delete_method,
  head,
  options,
  unknown,
};

[[nodiscard]] const char* to_string(Method method);
[[nodiscard]] bool valid_sha256_hex(std::string_view value);
[[nodiscard]] bool parse_canonical_generation(
    std::string_view value,
    std::uint64_t& generation);

struct Header {
  std::string name;
  std::string value;
};

struct Request {
  Method method{Method::unknown};
  std::string path;
  std::vector<Header> headers;
  std::string body;
  bool provisioning_transport{false};
};

struct Response {
  std::int32_t status{500};
  std::vector<Header> headers;
  JsonBody body;
  std::optional<std::uint64_t> delivered_network_connect_operation;
  // Even complete PSRAM exhaustion must send valid JSON, never a partial 2xx
  // receipt. The literal is static and survives synchronous httpd transmission.
  bool encoding_failed() const { return body.failed() || body.overflowed() || body.empty(); }
  std::int32_t wire_status() const { return encoding_failed() ? 503 : status; }
  std::string_view wire_body() const {
    return encoding_failed()
        ? std::string_view(R"({"api_version":"v1","ok":false,"error":{"code":"resource_unavailable","message":"Response workspace unavailable; retry with the same request key","retryable":true}})")
        : std::string_view(body);
  }
};

// Shared mapping for buffered API owners and the dedicated binary transport.
[[nodiscard]] Response response_for_context_error(const core::Error& error);

enum class BodyTransport : std::uint8_t {
  buffered_json,
  streaming_binary,
};

struct RouteMetadata {
  Method method;
  const char* path_pattern;
  std::size_t maximum_body_bytes;
  bool mutation;
  BodyTransport body_transport{BodyTransport::buffered_json};
};

inline constexpr std::array<RouteMetadata, 35U> routes = {{
    {Method::get, "/api/v1/tag-writer", 0U, false},
    {Method::post, "/api/v1/tag-writer", 4096U, true},
    {Method::get, "/api/v1/status", 0U, false},
    {Method::get, "/api/v1/device", 0U, false},
    {Method::get, "/api/v1/health", 0U, false},
    {Method::get, "/api/v1/network", 0U, false},
    {Method::post, "/api/v1/network/scan", 256U, true},
    {Method::post, "/api/v1/network/connect", 1024U, true},
    {Method::post, "/api/v1/network/setup-mode", 256U, true},
    {Method::get, "/api/v1/scale", 0U, false},
    {Method::post, "/api/v1/scale/weigh", 256U, true},
    {Method::post, "/api/v1/scale/update", 256U, true},
    {Method::post, "/api/v1/scale/tare", 256U, true},
    {Method::post, "/api/v1/scale/calibrate", 512U, true},
    {Method::get, "/api/v1/nfc", 0U, false},
    {Method::get, "/api/v1/nfc/tag", 0U, false},
    {Method::post, "/api/v1/nfc/read", 256U, true},
    {Method::get, "/api/v1/spool", 0U, false},
    {Method::post, "/api/v1/spool/confirm", 512U, true},
    {Method::get, "/api/v1/printers", 0U, false},
    {Method::get, "/api/v1/toolheads", 0U, false},
    {Method::post, "/api/v1/toolheads/{id}/assign", 2048U, true},
    {Method::post, "/api/v1/toolheads/{id}/unassign", 2048U, true},
    {Method::get, "/api/v1/config", 0U, false},
    {Method::patch, "/api/v1/config", maximum_request_body_bytes, true},
    {Method::get, "/api/v1/diagnostics", 0U, false},
    {Method::get, "/api/v1/logs", 0U, false},
    {Method::post, "/api/v1/backends/test", 256U, true},
    {Method::get, "/api/v1/update", 0U, false},
    {Method::post,
     "/api/v1/update/upload",
     maximum_firmware_image_bytes,
     true,
     BodyTransport::streaming_binary},
    {Method::post, "/api/v1/update/reboot", 512U, true},
    {Method::post, "/api/v1/update/cancel", 512U, true},
    {Method::post, "/api/v1/device/reboot", 256U, true},
    {Method::post, "/api/v1/device/factory-reset", 256U, true},
    {Method::get, "/api/v1/operations/{id}", 0U, false},
}};

enum class Resource : std::uint8_t {
  tag_writer,
  status,
  device,
  health,
  network,
  scale,
  nfc,
  nfc_tag,
  spool,
  printers,
  toolheads,
  redacted_configuration,
  diagnostics,
  logs,
  update,
  // Compatibility name retained while the Phase 9 application context is
  // replaced by the Phase 10 update-service snapshot.
  update_boundary = update,
};

struct EmptyMutation {};
struct TagWriterMutation { std::string json; };
struct SpoolConfirmationMutation {
  std::uint64_t spool_generation{0};
  std::int32_t spool_id{0};
};

struct ScaleCalibrationMutation {
  float reference_grams{0.0F};
};

struct NetworkConnectMutation {
  std::uint64_t expected_revision{0U};
  std::string ssid;
  std::optional<std::string> password;
  std::optional<std::string> hostname;
  std::optional<std::string> access_token;
};

struct ToolheadMutationPreconditions {
  std::string printer_id;
  std::optional<std::int32_t> expected_current_spool_id;
  std::string expected_printer_state;
  std::uint64_t spool_generation{0U};
  std::uint64_t printer_revision{0U};
  bool advanced_override{false};
};

struct ToolheadAssignmentMutation {
  std::int32_t backend_toolhead_id{0};
  std::int32_t expected_spool_id{0};
  ToolheadMutationPreconditions preconditions;
  bool replace_occupied_confirmed{false};
};

struct ToolheadUnassignmentMutation {
  std::int32_t backend_toolhead_id{0};
  ToolheadMutationPreconditions preconditions;
};

struct DevicePatch {
  std::optional<std::string> hostname;
  std::optional<std::uint8_t> brightness_percent;
  std::optional<std::uint32_t> dim_after_ms;
  std::optional<std::uint32_t> sleep_after_ms;
  std::optional<std::string> update_channel;
};

struct WifiPatch {
  std::optional<std::string> ssid;
  std::optional<std::string> password;
  std::optional<bool> auto_reconnect;
  std::optional<std::uint32_t> connect_timeout_ms;
  std::optional<std::uint32_t> reconnect_initial_ms;
  std::optional<std::uint32_t> reconnect_max_ms;
};

struct WebPatch {
  std::optional<std::string> access_token;
};

struct SpoolmanPatch {
  std::optional<std::string> url;
  std::optional<std::string> authentication_token;
  std::optional<std::string> identity_field;
  std::optional<std::string> nfc_uid_field;
  std::optional<std::string> ca_certificate_pem;
};

struct FilaBridgePatch {
  std::optional<std::string> url;
  std::optional<std::string> authentication_token;
  std::optional<std::string> selected_printer_id;
  std::optional<std::string> ca_certificate_pem;
};

struct ScaleProfilePatch {
  std::optional<std::string> id;
  std::optional<std::string> model;
  std::optional<std::uint32_t> rated_capacity_grams;
  std::optional<float> overload_ratio;
};

struct ToolheadProfilePatch {
  std::int32_t backend_id{0};
  std::string display_name;
  float nozzle_diameter_mm{0.4F};
  bool enabled{true};
  std::string nozzle_material;
  std::uint16_t maximum_temperature_c{0U};
  std::string notes;
};

struct ReconciliationPatch {
  std::optional<bool> auto_update_after_weigh;
  std::optional<float> normal_tolerance_grams;
  std::optional<float> warning_tolerance_grams;
};

struct ConfigurationPatchMutation {
  std::uint64_t expected_revision{0U};
  std::optional<DevicePatch> device;
  std::optional<WifiPatch> wifi;
  std::optional<WebPatch> web;
  std::optional<SpoolmanPatch> spoolman;
  std::optional<FilaBridgePatch> filabridge;
  std::optional<ScaleProfilePatch> scale_profile;
  std::optional<std::vector<ToolheadProfilePatch>> toolheads;
  std::optional<ReconciliationPatch> reconciliation;
};

struct WeightUpdateMutation { std::uint64_t measurement_id{0}; };

struct DeviceControlMutation {
  std::string confirmation;
};

struct UpdateControlMutation {
  std::uint64_t upload_operation_id{0U};
  std::uint64_t expected_generation{0U};
  std::string expected_sha256;
  std::string confirmation;
};

enum class MutationKind : std::uint8_t {
  tag_writer,
  scale_weigh,
  scale_update,
  scale_tare,
  scale_calibration,
  nfc_read,
  toolhead_assignment,
  toolhead_unassignment,
  configuration_patch,
  backend_test,
  spool_confirmation,
  update_reboot,
  update_cancel,
  reboot,
  factory_reset,
  network_scan,
  network_connect,
  network_setup_mode,
};

using MutationPayload = std::variant<
    TagWriterMutation,
    WeightUpdateMutation,
    EmptyMutation,
    SpoolConfirmationMutation,
    ScaleCalibrationMutation,
    NetworkConnectMutation,
    ToolheadAssignmentMutation,
    ToolheadUnassignmentMutation,
    ConfigurationPatchMutation,
    DeviceControlMutation,
    UpdateControlMutation>;

struct Mutation {
  MutationKind kind{MutationKind::scale_tare};
  std::string idempotency_key;
  std::uint64_t payload_digest{0U};
  MutationPayload payload{EmptyMutation{}};
  bool provisioning_transport{false};
};

struct OperationReceipt {
  std::uint64_t operation_id{0U};
};

class IApiContext {
 public:
  virtual ~IApiContext() = default;

  [[nodiscard]] virtual bool authorize_mutation(
      std::string_view bearer_token) = 0;
  [[nodiscard]] virtual bool authorize_provisioning() = 0;

  // Returns one complete, bounded JSON value. For redacted_configuration the
  // implementation must construct an allowlisted view and never return a raw
  // persisted document or credential values.
  [[nodiscard]] virtual core::Result<JsonBody> snapshot_json(
      Resource resource) = 0;
  [[nodiscard]] virtual core::Result<std::optional<JsonBody>>
  operation_status_json(std::uint64_t operation_id) = 0;
  [[nodiscard]] virtual core::Result<OperationReceipt> submit(
      const Mutation& mutation) = 0;
};

class Router final {
 public:
  explicit Router(IApiContext& context) : context_(context) {}

  [[nodiscard]] Response handle(const Request& request);

 private:
  IApiContext& context_;
};

}  // namespace opentag::web::api
