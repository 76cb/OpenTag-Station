#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "config/configuration_service.hpp"
#include "application/operation_registry.hpp"
#include "domain/weight.hpp"
#include "integrations/filabridge/filabridge_adapter.hpp"
#include "integrations/spoolman/spoolman_adapter.hpp"
#include "nfc/formats/openprinttag/codec.hpp"
#include "nfc/protocols/nfcv/tag.hpp"
#include "services/spool_identity_resolver.hpp"
#include "services/station_workflow.hpp"

namespace opentag::application {

struct BackendRuntimeSnapshot {
  bool connected{false};
  bool healthy{false};
  bool version_formally_tested{false};
  std::string version;
  std::uint32_t capability_bits{0U};
  std::optional<core::Error> last_error;
};

struct BackendWorkerSnapshot {
  BackendRuntimeSnapshot spoolman;
  BackendRuntimeSnapshot filabridge;
  std::size_t pending{0U};
  std::uint64_t revision{0U};
};

class BackendWorker final {
 public:
  BackendWorker(
      config::ConfigurationService& configuration,
      integrations::spoolman::SpoolmanAdapter& spoolman,
      integrations::filabridge::FilaBridgeAdapter& filabridge,
      services::SpoolIdentityResolver& resolver,
      services::StationWorkflow& workflow,
      OperationRegistry& operations)
      : configuration_(configuration),
        spoolman_(spoolman),
        filabridge_(filabridge),
        resolver_(resolver),
        workflow_(workflow),
        operations_(operations) {}

  [[nodiscard]] bool start();
  [[nodiscard]] bool submit_identified_spool(
      const nfc::openprinttag::MaterialRecord& material,
      const nfc::nfcv::Uid& uid,
      domain::WeightReading physical_weight,
      domain::EmptyWeightCandidates supplemental_empty_weights = {});
  [[nodiscard]] CommandReceipt submit_assignment_operation(
      std::string printer_id,
      int backend_toolhead_id,
      bool replace_occupied_confirmed,
      bool advanced_active_print_override,
      std::optional<std::uint64_t> expected_spool_generation,
      std::optional<domain::SpoolId> expected_spool_id,
      services::ToolheadMutationPrecondition precondition,
      std::optional<std::uint64_t> expected_printer_revision = std::nullopt);
  [[nodiscard]] CommandReceipt submit_unassignment_operation(
      std::string printer_id,
      int backend_toolhead_id,
      bool advanced_active_print_override,
      std::optional<std::uint64_t> expected_spool_generation,
      services::ToolheadMutationPrecondition precondition,
      std::optional<std::uint64_t> expected_printer_revision = std::nullopt);
  [[nodiscard]] CommandReceipt submit_refresh();
  [[nodiscard]] BackendWorkerSnapshot snapshot() const;
  [[nodiscard]] TaskHandle_t task_handle() const { return task_; }
  [[nodiscard]] std::size_t pending() const {
    return pending_.load(std::memory_order_relaxed);
  }

 private:
  enum class CommandType : std::uint8_t {
    identified_spool,
    assign,
    unassign,
    refresh,
  };

  struct Command {
    CommandType type{CommandType::refresh};
    nfc::openprinttag::MaterialRecord material;
    nfc::nfcv::Uid uid;
    domain::WeightReading physical_weight;
    domain::EmptyWeightCandidates supplemental_empty_weights;
    std::string printer_id;
    int backend_toolhead_id{0};
    bool replace_occupied_confirmed{false};
    bool advanced_active_print_override{false};
    std::optional<std::uint64_t> expected_spool_generation;
    std::optional<domain::SpoolId> expected_spool_id;
    services::ToolheadMutationPrecondition precondition;
    std::optional<std::uint64_t> expected_printer_revision;
    std::uint64_t operation_id{0U};
    std::uint32_t enqueued_at_ms{0U};
  };

  static void task_entry(void* context);
  void run();
  void probe_backends(std::uint64_t operation_id = 0U);
  [[nodiscard]] bool apply_backend_settings_if_changed();
  void process(Command& command);
  [[nodiscard]] bool enqueue(Command* command);

  static constexpr std::uint32_t probe_interval_ms = 30000U;
  config::ConfigurationService& configuration_;
  integrations::spoolman::SpoolmanAdapter& spoolman_;
  integrations::filabridge::FilaBridgeAdapter& filabridge_;
  services::SpoolIdentityResolver& resolver_;
  services::StationWorkflow& workflow_;
  OperationRegistry& operations_;
  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
  std::atomic_size_t pending_{0U};
  std::uint32_t last_probe_ms_{0U};
  std::optional<std::uint64_t> applied_backend_settings_revision_;
  bool spoolman_configured_{false};
  bool filabridge_configured_{false};
  bool wifi_offline_published_{false};
  mutable std::mutex status_mutex_;
  BackendWorkerSnapshot status_;
  static constexpr std::uint32_t destructive_command_expiry_ms = 15000U;
};

}  // namespace opentag::application
