#pragma once
#include "integrations/spoolman/spoolman_adapter.hpp"
#include "network/backend_json.hpp"
#include "nfc/formats/openprinttag/spoolman_mapping.hpp"
#include "nfc/writer_journal.hpp"
namespace opentag::services {
class TagWriterService {
public:
  using Publish = std::function<void(const network::ResponseBody &)>;
  using Random = std::function<void(std::uint8_t *, std::size_t)>;
  TagWriterService(integrations::spoolman::SpoolmanAdapter &spoolman,
                   nfc::IWriterReader &reader,
                   std::function<std::uint64_t()> generation, Random random,
                   Publish publish, nfc::WriterJournal *journal = nullptr)
      : spoolman_(spoolman), writer_(reader, std::move(generation)),
        random_(std::move(random)), publish_(std::move(publish)),
        journal_(journal) {}
  core::Result<void> process(JsonObjectConst command);
  bool physical_pass() const { return plan_ && plan_->verified; }

private:
  core::Result<network::BackendDocument>
  api(const char *method, const std::string &path, JsonVariantConst body = {});
  core::Result<void> catalog(JsonObjectConst command);
  core::Result<void> import_preview(JsonObjectConst command);
  core::Result<void> import_commit(JsonObjectConst command);
  core::Result<void> create_spool(JsonObjectConst command);
  core::Result<void> edit_record(JsonObjectConst command, bool filament);
  core::Result<void> edit_and_report(JsonObjectConst command, bool filament);
  core::Result<void> prepare(JsonObjectConst command);
  core::Result<void> associate();
  core::Result<void> unique_identity();
  core::Result<std::int32_t> uid_owner();
  core::Result<void> prepare_uid_owner();
  core::Result<void> clear_previous_uid();
  void publish(const char *phase, const char *message = "",
               std::size_t done = 0, std::size_t total = 0);
  integrations::spoolman::SpoolmanAdapter &spoolman_;
  nfc::OpenPrintTagWriter writer_;
  Random random_;
  Publish publish_;
  std::unique_ptr<nfc::WriterPlan, network::ExternalDelete<nfc::WriterPlan>>
      plan_;
  network::BackendDocument view_, import_;
  std::int32_t spool_id_{0}, import_vendor_{0}, import_filament_{0};
  std::string uuid_, import_token_, settings_url_, identity_key_, uid_key_;
  std::uint64_t preview_serial_{0};
  bool association_pending_{false};
  nfc::WriterJournal *journal_{nullptr};
};
} // namespace opentag::services
