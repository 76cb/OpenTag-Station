#pragma once
#include "integrations/spoolman/spoolman_adapter.hpp"
#include "network/backend_json.hpp"
#include "domain/spool_identity.hpp"
#include "nfc/formats/openprinttag/spoolman_mapping.hpp"
#include "nfc/writer_journal.hpp"
#include "services/restart_guard.hpp"
namespace opentag::services {
// Bound of one published writer snapshot (and of one Spoolman writer response).
inline constexpr std::size_t writer_snapshot_maximum_bytes = 24576U;
// Largest serialized view a writer step accepts before it publishes.
inline constexpr std::size_t writer_view_maximum_bytes = 24000U;
class TagWriterService {
public:
  using Publish = std::function<void(const network::ResponseBody &)>;
  using Random = std::function<void(std::uint8_t *, std::size_t)>;
  // Invoked only after physical and canonical remote verification.
  using VerifiedClearMapping = std::function<core::Result<void>(
      const std::string &, const std::string &, std::int32_t)>;
  using VerifiedAssociation = std::function<core::Result<void>(
      const domain::ConfirmedSpoolMapping &)>;
  TagWriterService(integrations::spoolman::SpoolmanAdapter &spoolman,
                   nfc::IWriterReader &reader,
                   std::function<std::uint64_t()> generation, Random random,
                   Publish publish, nfc::WriterJournal *journal = nullptr,
                   VerifiedClearMapping clear_mapping = {},
                   VerifiedAssociation sync_mapping = {})
      : spoolman_(spoolman), writer_(reader, std::move(generation)),
        random_(std::move(random)), publish_(std::move(publish)),
        journal_(journal), clear_mapping_(std::move(clear_mapping)),
        sync_mapping_(std::move(sync_mapping)) {}
  // Every action but a catalog search is refused while a restart is pending.
  void set_restart_pending(RestartPending pending) { restart_pending_ = std::move(pending); }
  core::Result<void> process(JsonObjectConst command);
  // Publishes any durable recovery prompt. `reason` (optional) explains the
  // failure that returned the station to that prompt.
  core::Result<void> restore_cleanup(const std::string &reason = {});
  bool physical_pass() const { return plan_ && plan_->verified; }

private:
  using WriterProgress = nfc::OpenPrintTagWriter::Progress;
  core::Result<network::BackendDocument>
  api(const char *method, const std::string &path, JsonVariantConst body = {});
  core::Result<void> catalog(JsonObjectConst command);
  core::Result<void> create_spool(JsonObjectConst command);
  core::Result<void> edit_record(JsonObjectConst command, bool filament);
  core::Result<void> edit_and_report(JsonObjectConst command, bool filament);
  core::Result<void> prepare(JsonObjectConst command);
  core::Result<void> prepare_clear();
  core::Result<void> require_identity_fields();
  core::Result<void> discard_recovery(JsonObjectConst command);
  bool has_durable_recovery();
  std::string journaled_other_uid();
  core::Result<void> commit_clear(JsonObjectConst command);
  core::Result<void> commit_write(JsonObjectConst command);
  core::Result<void> unlink();
  core::Result<void> unlink_final_check();
  core::Result<void> persist_clear();
  bool restore_ready();
  core::Result<void> associate();
  core::Result<void> associate_extra_fields();
  core::Result<void> unique_identity();
  core::Result<std::int32_t> uid_owner();
  core::Result<void> prepare_uid_owner();
  core::Result<void> clear_previous_uid();
  // Spoolman 0.27+ tag links (POST/DELETE /spool/{id}/tag). A tag may have
  // been linked by another client in the reversed byte order, so both
  // spellings are looked up; `uid` is the spelling Spoolman holds.
  struct NativeHold {
    std::int32_t spool{0};
    std::string uid;
  };
  core::Result<std::vector<NativeHold>> native_holders();
  core::Result<std::int32_t> native_filament_holder();
  core::Result<void> native_unlink(const NativeHold &hold);
  core::Result<void> link_native();
  core::Result<void> unlink_native();
  // Remembers the Spoolman URL and extra-field keys a preview was made under.
  void capture_settings();
  // True when any of those differs from the adapter's current settings.
  bool settings_changed() const;
  void publish(const char *phase, const char *message = "",
               std::size_t done = 0, std::size_t total = 0);
  integrations::spoolman::SpoolmanAdapter &spoolman_;
  nfc::OpenPrintTagWriter writer_;
  Random random_;
  Publish publish_;
  std::unique_ptr<nfc::WriterPlan, network::ExternalDelete<nfc::WriterPlan>>
      plan_;
  network::BackendDocument view_;
  std::int32_t spool_id_{0};
  std::string uuid_, settings_url_, identity_key_, uid_key_;
  std::uint64_t operation_id_{0};
  bool association_pending_{false};
  // Set by require_identity_fields(): whether the two extra fields exist and
  // whether Spoolman links tags itself. At least one is true when it passes.
  bool extras_{true};
  bool native_{false};
  nfc::WriterJournal *journal_{nullptr};
  VerifiedClearMapping clear_mapping_;
  VerifiedAssociation sync_mapping_;
  RestartPending restart_pending_;
  bool unlink_pending_{false};
  bool clear_recovery_required_{false};
  // A pending clear was recorded under different Spoolman settings.
  bool clear_settings_changed_{false};
};
} // namespace opentag::services
