#include "nfc/formats/openprinttag/initializer.hpp"
#include "nfc/writer_journal_codec.hpp"
#include "services/tag_writer_service.hpp"
#include "config/configuration_service.hpp"
#include <cstring>
#include <fstream>
#include <functional>
#include <unity.h>
using namespace opentag;
namespace {
using R = core::Result<void>;
R failure() {
  return R::failure(
      {core::ErrorCategory::nfc_communication, "injected", false});
}
struct Reader : nfc::IWriterReader {
  nfc::nfcv::Uid uid{{0xe0, 4, 1, 8, 0x66, 0x27, 0xd8, 0xd4}};
  nfc::nfcv::TagGeometry geo{4, 80};
  std::array<std::uint8_t, 320> bytes{};
  std::array<std::uint8_t, 80> locks{};
  int count = 1, writes = 0, reads = 0, fail_write = -1, corrupt_read = -1;
  std::uint64_t generation = 3;
  std::uint32_t system = 77, errors = 0;
  std::function<void()> before_inventory;
  std::function<void()> before_write;
  Reader() {
    bytes[312] = 0x17;
    bytes[319] = 0x92;
  }
  R initialize() override { return R::success(); }
  int field_calls = 0;
  R field_on() override {
    ++field_calls;
    return R::success();
  }
  R field_off() override { return R::success(); }
  R health() override { return R::success(); }
  core::Result<std::vector<nfc::nfcv::Uid>> inventory() override {
    if (before_inventory)
      before_inventory();
    return core::Result<std::vector<nfc::nfcv::Uid>>::success(
        std::vector<nfc::nfcv::Uid>(count, uid));
  }
  core::Result<nfc::nfcv::TagGeometry>
  geometry(const nfc::nfcv::Uid &) override {
    return core::Result<nfc::nfcv::TagGeometry>::success(geo);
  }
  R read_blocks(const nfc::nfcv::Uid &, std::size_t f, std::size_t n,
                std::size_t s, std::uint8_t *out) override {
    std::memcpy(out, bytes.data() + f * s, n * s);
    return R::success();
  }
  core::Result<nfc::WriterSystemInformation>
  writer_system_information(const nfc::nfcv::Uid &) override {
    nfc::WriterSystemInformation info;
    info.length = 1;
    info.bytes[0] = system;
    return core::Result<nfc::WriterSystemInformation>::success(info);
  }
  R security_read(const nfc::nfcv::Uid &, std::size_t block, std::uint8_t *out,
                  std::uint8_t &security) override {
    ++reads;
    security = locks[block];
    std::memcpy(out, bytes.data() + block * 4, 4);
    if (reads == corrupt_read)
      out[0] ^= 1;
    return R::success();
  }
  R commit_openprinttag_block(const nfc::nfcv::Uid &, std::size_t block,
                              const std::uint8_t *data) override {
    if (before_write)
      before_write();
    if (writes++ == fail_write)
      return failure();
    TEST_ASSERT_LESS_THAN(78, block);
    std::memcpy(bytes.data() + block * 4, data, 4);
    return R::success();
  }
  std::uint32_t now_ms() const override { return 10; }
  std::uint32_t bus_errors() const override { return errors; }
};
const char *canonical =
    R"({"id":12,"used_weight":100,"initial_weight":1000,"spool_weight":200,"archived":false,"extra":{},"filament":{"id":4,"name":"PLA Blue","material":"PLA","weight":1000,"density":1.24,"diameter":1.75,"color_hex":"123456","article_number":"SKU123","settings_extruder_temp":215,"vendor":{"id":2,"name":"Acme"}}})";
const std::array<std::uint8_t, 16> uuid{1,    2,  3,  4,  5,  6,  0x47, 8,
                                        0x89, 10, 11, 12, 13, 14, 15,   16};
struct Fixture {
  Reader reader;
  nfc::WriterPlan plan;
  nfc::OpenPrintTagWriter writer{reader, [this] { return reader.generation; }};
  network::BackendDocument spool;
  Fixture() { deserializeJson(spool, canonical); }
  void prepare() {
    TEST_ASSERT_TRUE(writer.read(plan).ok());
    auto r = nfc::openprinttag::map_spoolman(spool.as<JsonObjectConst>(), uuid,
                                             plan, false);
    TEST_ASSERT_TRUE_MESSAGE(r.ok(), r.ok() ? "" : r.error().message.c_str());
    TEST_ASSERT_TRUE(writer.plan(plan).ok());
  }
  R execute() {
    return writer.execute(plan, [](const char *, std::size_t, std::size_t) {});
  }
};
void blank_and_complete_verification() {
  Fixture f;
  f.prepare();
  TEST_ASSERT_TRUE(f.plan.blank);
  TEST_ASSERT_GREATER_THAN(1, f.plan.count);
  TEST_ASSERT_TRUE(f.execute().ok());
  TEST_ASSERT_TRUE(f.plan.verified);
  TEST_ASSERT_EQUAL_MEMORY(f.plan.target.data(), f.reader.bytes.data(), 320);
  TEST_ASSERT_EQUAL_HEX8(0x17, f.reader.bytes[312]);
  const auto writes = f.reader.writes;
  TEST_ASSERT_TRUE(f.execute().ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
}
void nonblank_rejected() {
  Fixture f;
  f.reader.bytes[16] = 1;
  TEST_ASSERT_FALSE(f.writer.read(f.plan).ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void geometry_rejected() {
  Fixture f;
  f.reader.geo.block_count = 79;
  TEST_ASSERT_FALSE(f.writer.read(f.plan).ok());
}
void multiple_rejected() {
  Fixture f;
  f.reader.count = 2;
  TEST_ASSERT_FALSE(f.writer.read(f.plan).ok());
}
void locked_rejected() {
  Fixture f;
  f.reader.locks[50] = 1;
  TEST_ASSERT_FALSE(f.writer.read(f.plan).ok());
}
void unstable_rejected() {
  Fixture f;
  f.reader.corrupt_read = 90;
  TEST_ASSERT_FALSE(f.writer.read(f.plan).ok());
}
void removed_before_write() {
  Fixture f;
  f.prepare();
  f.reader.count = 0;
  TEST_ASSERT_FALSE(f.execute().ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void replaced_before_write() {
  Fixture f;
  f.prepare();
  f.reader.uid.bytes[7] ^= 1;
  TEST_ASSERT_FALSE(f.execute().ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void generation_changed() {
  Fixture f;
  f.prepare();
  ++f.reader.generation;
  TEST_ASSERT_FALSE(f.execute().ok());
}
void system_changed() {
  Fixture f;
  f.prepare();
  ++f.reader.system;
  TEST_ASSERT_FALSE(f.execute().ok());
}
void protection_changed() {
  Fixture f;
  f.prepare();
  f.reader.locks[1] = 1;
  TEST_ASSERT_FALSE(f.execute().ok());
}
void geometry_changed() {
  Fixture f;
  f.prepare();
  f.reader.geo.block_count = 81;
  TEST_ASSERT_FALSE(f.execute().ok());
}
void write_failure_stops() {
  Fixture f;
  f.prepare();
  f.reader.fail_write = 1;
  TEST_ASSERT_FALSE(f.execute().ok());
  TEST_ASSERT_EQUAL(2, f.reader.writes);
  TEST_ASSERT_FALSE(f.execute().ok());
  TEST_ASSERT_EQUAL(2, f.reader.writes);
}
void block_mismatch_stops() {
  Fixture f;
  f.prepare();
  f.reader.corrupt_read = f.reader.reads + 82;
  TEST_ASSERT_FALSE(f.execute().ok());
  TEST_ASSERT_EQUAL(1, f.reader.writes);
}
void final_mismatch_stops() {
  Fixture f;
  f.prepare();
  f.reader.corrupt_read = f.reader.reads + 80 + 2 * f.plan.count + 2;
  TEST_ASSERT_FALSE(f.execute().ok());
  TEST_ASSERT_FALSE(f.plan.verified);
}
void removed_mid_write() {
  Fixture f;
  f.prepare();
  f.reader.before_inventory = [&] {
    if (f.reader.writes == 1)
      f.reader.count = 0;
  };
  TEST_ASSERT_FALSE(f.execute().ok());
  TEST_ASSERT_EQUAL(1, f.reader.writes);
}
void bus_error_refuses() {
  Fixture f;
  f.prepare();
  f.reader.errors = 1;
  TEST_ASSERT_FALSE(f.execute().ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void preserved_plan_refused() {
  Fixture f;
  f.prepare();
  f.plan.target[312] ^= 1;
  TEST_ASSERT_FALSE(f.writer.plan(f.plan).ok());
}
void malformed_target_refused() {
  Fixture f;
  f.prepare();
  f.plan.target[0] = 0;
  TEST_ASSERT_FALSE(f.writer.plan(f.plan).ok());
}
void no_change_rewrite() {
  Fixture f;
  f.prepare();
  TEST_ASSERT_TRUE(f.execute().ok());
  f.prepare();
  TEST_ASSERT_EQUAL(0, f.plan.count);
  TEST_ASSERT_TRUE(f.execute().ok());
}
void one_block_rewrite() {
  Fixture f;
  f.prepare();
  TEST_ASSERT_TRUE(f.execute().ok());
  TEST_ASSERT_TRUE(f.writer.read(f.plan).ok());
  auto weight = nfc::openprinttag::Codec::update_consumed_weight(
      {f.plan.original.data(), 312}, 101);
  TEST_ASSERT_TRUE(weight.ok());
  std::copy(weight.value().begin(), weight.value().end(),
            f.plan.target.begin());
  TEST_ASSERT_TRUE(f.writer.plan(f.plan).ok());
  TEST_ASSERT_EQUAL(1, f.plan.count);
  TEST_ASSERT_TRUE(f.execute().ok());
}
void aux_only_update() {
  Fixture f;
  f.prepare();
  TEST_ASSERT_TRUE(f.execute().ok());
  TEST_ASSERT_TRUE(f.writer.read(f.plan).ok());
  f.spool["used_weight"] = 125;
  TEST_ASSERT_TRUE(nfc::openprinttag::map_spoolman(
                       f.spool.as<JsonObjectConst>(), uuid, f.plan, true)
                       .ok());
  TEST_ASSERT_TRUE(f.writer.plan(f.plan).ok());
  for (std::size_t i = 0; i < f.plan.count; ++i)
    TEST_ASSERT_GREATER_OR_EQUAL(69, f.plan.blocks[i]);
  TEST_ASSERT_TRUE(f.execute().ok());
}
void metadata_rewrite() {
  Fixture f;
  f.prepare();
  TEST_ASSERT_TRUE(f.execute().ok());
  f.spool["filament"]["name"] = "Red PETG";
  f.spool["filament"]["material"] = "PETG";
  f.prepare();
  TEST_ASSERT_GREATER_THAN(1, f.plan.count);
  TEST_ASSERT_TRUE(f.execute().ok());
}
void mapped_fields() {
  Fixture f;
  f.prepare();
  auto &m = f.plan.proposed.material;
  TEST_ASSERT_TRUE(m.instance_uuid == uuid);
  TEST_ASSERT_EQUAL_STRING("PLA Blue", m.material_name->c_str());
  TEST_ASSERT_EQUAL_STRING("Acme", m.brand_name->c_str());
  TEST_ASSERT_EQUAL_STRING("PLA", m.material_abbreviation->c_str());
  TEST_ASSERT_EQUAL_STRING("SKU123", m.brand_specific_package_id->c_str());
  TEST_ASSERT_EQUAL(0, *m.material_class);
  TEST_ASSERT_EQUAL(1000, *m.nominal_netto_full_weight);
  TEST_ASSERT_EQUAL(1000, *m.actual_netto_full_weight);
  TEST_ASSERT_EQUAL(200, *m.empty_container_weight);
  TEST_ASSERT_EQUAL(100, *m.consumed_weight);
  TEST_ASSERT_FLOAT_WITHIN(.0001, 1.24, *m.density);
  TEST_ASSERT_FLOAT_WITHIN(.0001, 1.75, *m.filament_diameter);
  TEST_ASSERT_FALSE(m.preheat_temperature);
  TEST_ASSERT_EQUAL(0, *m.material_type);
  TEST_ASSERT_EQUAL(0x12, m.primary_color->red);
  TEST_ASSERT_EQUAL(0x34, m.primary_color->green);
  TEST_ASSERT_EQUAL(0x56, m.primary_color->blue);
}
void unknown_optional_fields() {
  Fixture f;
  f.spool["initial_weight"] = nullptr;
  f.spool["spool_weight"] = nullptr;
  f.spool["filament"]["weight"] = nullptr;
  f.spool["filament"]["color_hex"] = nullptr;
  f.prepare();
  TEST_ASSERT_FALSE(f.plan.proposed.material.actual_netto_full_weight);
  TEST_ASSERT_FALSE(f.plan.proposed.material.empty_container_weight);
  TEST_ASSERT_FALSE(f.plan.proposed.material.primary_color);
}
void real_zero_preserved() {
  Fixture f;
  f.spool["used_weight"] = 0;
  f.spool["spool_weight"] = 0;
  f.prepare();
  TEST_ASSERT_TRUE(f.plan.proposed.material.empty_container_weight.has_value());
  TEST_ASSERT_EQUAL(0, *f.plan.proposed.material.empty_container_weight);
  TEST_ASSERT_EQUAL(0, *f.plan.proposed.material.consumed_weight);
}
void uuid_validation() {
  const auto text = nfc::openprinttag::instance_uuid_text(uuid);
  TEST_ASSERT_TRUE(nfc::openprinttag::parse_instance_uuid(text).ok());
  TEST_ASSERT_FALSE(nfc::openprinttag::parse_instance_uuid(
                        "00000000-0000-0000-0000-000000000000")
                        .ok());
  TEST_ASSERT_FALSE(nfc::openprinttag::parse_instance_uuid("nope").ok());
  TEST_ASSERT_FALSE(nfc::openprinttag::parse_instance_uuid(
                        "zz223344-5566-4788-9900-aabbccddeeff")
                        .ok());
}
void repeated_plans_release_workspace() {
  const auto baseline = network::backend_json_allocator.used();
  for (int i = 0; i < 50; ++i) {
    Fixture f;
    f.prepare();
  }
  TEST_ASSERT_EQUAL(baseline, network::backend_json_allocator.used());
}
struct Http : network::IHttpTransport {
  std::function<std::string(const network::HttpRequest &)> custom;
  int custom_status = 200;
  network::BackendDocument spool;
  network::BackendDocument previous;
  int patches = 0;
  int clears = 0;
  bool multiple_uid = false, fail_clear = false, bad_clear_readback = false,
       fail_target = false, final_uid_conflict = false;
  bool offline = false, duplicate = false, bad_page = false;
  bool fail_edit = false, mismatch_edit = false;
  std::vector<std::string> urls;
  std::vector<std::string> events;
  Http() {
    deserializeJson(spool, canonical);
    deserializeJson(
        previous,
        R"({"id":9,"archived":true,"extra":{"opentag_instance_uuid":"\"12345678-1234-4234-9234-123456789012\"","keep":"\"unchanged\""}})");
  }
  void claim_previous(const std::string &uid) {
    network::BackendDocument value;
    value.set(uid);
    std::string encoded;
    serializeJson(value, encoded);
    previous["extra"]["nfc_uid"] = encoded;
  }
  core::Result<network::HttpResponse>
  perform(const network::HttpRequest &request) override {
    urls.push_back(request.url);
    if (offline)
      return core::Result<network::HttpResponse>::failure(
          {core::ErrorCategory::network, "offline", true});
    network::BackendDocument result;
    auto path = request.url.substr(request.url.find("/api/v1") + 7);
    events.push_back(request.method + " " + path);
    if (path == "/field/spool")
      deserializeJson(
          result,
          R"([{"key":"opentag_instance_uuid","name":"UUID","field_type":"text"},{"key":"nfc_uid","name":"UID","field_type":"text"}])");
    else if (path == "/spool/" + std::to_string(spool["id"].as<int>())) {
      if (request.method == "PATCH") {
        if (fail_target)
          return core::Result<network::HttpResponse>::failure(
              {core::ErrorCategory::network, "target PATCH failed", true});
        network::BackendDocument patch;
        deserializeJson(patch, request.body);
        if (fail_edit && !patch["extra"].is<JsonObject>())
          return core::Result<network::HttpResponse>::failure(
              {core::ErrorCategory::network, "edit PATCH failed", true});
        if (!mismatch_edit)
          for (auto field : patch.as<JsonObjectConst>()) {
            if (std::string(field.key().c_str()) == "extra") {
              for (auto extra : field.value().as<JsonObjectConst>())
                spool["extra"][extra.key()] = extra.value();
            } else
              spool[field.key()] = field.value();
          }
        ++patches;
      }
      result.set(spool);
    } else if (path == "/filament/4") {
      if (request.method == "PATCH") {
        if (fail_edit)
          return core::Result<network::HttpResponse>::failure(
              {core::ErrorCategory::network, "edit PATCH failed", true});
        network::BackendDocument patch;
        deserializeJson(patch, request.body);
        if (!mismatch_edit)
          for (auto field : patch.as<JsonObjectConst>())
            spool["filament"][field.key()] = field.value();
        ++patches;
      }
      result.set(spool["filament"]);
    } else if (path == "/spool/9") {
      if (request.method == "PATCH") {
        if (fail_clear)
          return core::Result<network::HttpResponse>::failure(
              {core::ErrorCategory::network, "previous PATCH failed", true});
        network::BackendDocument patch;
        deserializeJson(patch, request.body);
        TEST_ASSERT_EQUAL(1, patch["extra"].size());
        TEST_ASSERT_TRUE(
            patch["extra"].as<JsonObject>().containsKey("nfc_uid"));
        TEST_ASSERT_TRUE(patch["extra"]["nfc_uid"].isNull());
        ++clears;
        if (!bad_clear_readback)
          previous["extra"].remove("nfc_uid");
      }
      result.set(previous);
    } else if (path.find("extra.nfc_uid") != std::string::npos) {
      auto a = result.to<JsonArray>();
      if (!previous["extra"]["nfc_uid"].isNull())
        a.add<JsonObject>()["id"] = 9;
      if (!spool["extra"]["nfc_uid"].isNull())
        a.add<JsonObject>()["id"] = spool["id"].as<int>();
      if (multiple_uid || (final_uid_conflict && patches))
        a.add<JsonObject>()["id"] = 99;
    } else if (path.find("extra.opentag_instance_uuid") != std::string::npos) {
      auto a = result.to<JsonArray>();
      if (duplicate)
        a.add<JsonObject>()["id"] = 99;
      else if (!spool["extra"]["opentag_instance_uuid"].isNull())
        a.add<JsonObject>()["id"] = spool["id"].as<int>();
    } else if (bad_page)
      result["unexpected"] = true;
    else {
      auto a = result.to<JsonArray>();
      for (int i = 0; i < 8; ++i) {
        auto item = a.add<JsonObject>();
        item["id"] = i + 1;
        item["name"] = "live";
      }
    }
    network::ResponseBody body;
    const auto replacement = custom ? custom(request) : std::string{};
    if (replacement.empty())
      serializeJson(result, body);
    else
      body.append(replacement.data(), replacement.size());
    return core::Result<network::HttpResponse>::success(
        {custom_status, std::move(body), "application/json"});
  }
};
struct CacheDocuments : config::IConfigurationDocumentStore {
  std::optional<std::string> document;
  bool fail_save = false;
  core::Result<std::optional<std::string>> load_configuration_document() override {
    return core::Result<std::optional<std::string>>::success(document);
  }
  core::Result<std::optional<std::string>> load_configuration_backup_document() override {
    return core::Result<std::optional<std::string>>::success(std::nullopt);
  }
  R save_configuration_document(std::string_view value) override {
    if (fail_save) return failure();
    document = std::string(value);
    return R::success();
  }
};
struct CacheLegacy : services::IScaleCalibrationStore {
  core::Result<std::optional<services::ScaleCalibration>> load_scale_calibration() override {
    return core::Result<std::optional<services::ScaleCalibration>>::success(std::nullopt);
  }
  R save_scale_calibration(const services::ScaleCalibration&) override { return R::success(); }
  R clear_scale_calibration() override { return R::success(); }
};
struct Cache {
  CacheDocuments documents;
  CacheLegacy legacy;
  config::ConfigurationService configuration{documents, legacy};
  Cache() { TEST_ASSERT_TRUE(configuration.initialize().ok()); }
  void stale(const std::string& uid) {
    domain::ConfirmedSpoolMapping mapping;
    mapping.spool_id = 17; mapping.nfc_uid = uid;
    mapping.instance_uuid = "00112233-4455-6677-8899-aabbccddeeff";
    TEST_ASSERT_TRUE(configuration.confirm_spool_identity_mapping(mapping).ok());
    mapping.spool_id = 18; mapping.nfc_uid = "E004000000000099";
    mapping.instance_uuid = "00112233-4455-6677-8899-aabbccddee00";
    TEST_ASSERT_TRUE(configuration.confirm_spool_identity_mapping(mapping).ok());
  }
};
R accept_verified_association(const domain::ConfirmedSpoolMapping&) { return R::success(); }
struct ServiceFixture {
  Reader reader;
  Http http;
  integrations::spoolman::SpoolmanAdapter adapter{http,
                                                  {"http://spoolman.test"}};
  network::BackendDocument view;
  Cache cache;
  int sync_calls = 0;
  services::TagWriterService service{
      adapter, reader, [this] { return reader.generation; },
      [](std::uint8_t *p, std::size_t n) { std::memset(p, 0x44, n); },
      [this](const auto &b) { deserializeJson(view, b.data(), b.size()); }, nullptr, {},
      [this](const domain::ConfirmedSpoolMapping& mapping) {
        ++sync_calls;
        TEST_ASSERT_TRUE(service.physical_pass());
        TEST_ASSERT_GREATER_THAN(0, http.patches);
        TEST_ASSERT_TRUE(http.urls.back().find("extra.nfc_uid") != std::string::npos);
        return cache.configuration.sync_verified_spool_identity_mapping(mapping);
      }, [](const std::string& query,unsigned offset) {
        TEST_ASSERT_EQUAL_STRING("Acme",query.c_str());TEST_ASSERT_EQUAL(0,offset);
        network::BackendDocument page;
        deserializeJson(page,R"({"items":[{"id":"acme_blue","manufacturer":"Acme","name":"Blue","material":"PLA","density":1.24,"diameter":1.75}],"has_more":false})");
        return core::Result<network::BackendDocument>::success(std::move(page));
      }};
  R run(const char *command) {
    network::BackendDocument d;
    deserializeJson(d, command);
    return service.process(d.as<JsonObjectConst>());
  }
  R confirm() {
    network::BackendDocument c;
    c["action"] = "write";
    for (const auto *key : {"uid", "generation", "spool_id",
                            "previous_spool_id", "target_checksum"})
      c[key] = view[key];
    return service.process(c.as<JsonObjectConst>());
  }
};
void canonical_association_readback() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
  TEST_ASSERT_TRUE(f.confirm().ok());
  TEST_ASSERT_EQUAL(1, f.http.patches);
  TEST_ASSERT_EQUAL_STRING("complete", f.view["phase"].as<const char *>());
  const auto writes = f.reader.writes;
  TEST_ASSERT_FALSE(f.run(R"({"action":"retry_association"})").ok());
  TEST_ASSERT_EQUAL_STRING("failed", f.view["phase"].as<const char *>());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
  TEST_ASSERT_EQUAL(1, f.http.patches);
}
void association_pending_retry_without_write() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.http.offline = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL_STRING("association_pending",
                           f.view["phase"].as<const char *>());
  const auto writes = f.reader.writes;
  f.http.offline = false;
  TEST_ASSERT_TRUE(f.run(R"({"action":"retry_association"})").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
  TEST_ASSERT_EQUAL_STRING("complete", f.view["phase"].as<const char *>());
}
void duplicate_uuid_refuses_before_write() {
  ServiceFixture f;
  f.http.duplicate = true;
  TEST_ASSERT_FALSE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void confirmation_mismatch_refuses() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.view["target_checksum"] = "00000000";
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void catalog_pagination_and_search() {
  ServiceFixture f;
  for (const auto *entity : {"vendor", "filament", "spool"}) {
    network::BackendDocument c;
    c["action"] = "catalog";
    c["entity"] = entity;
    c["offset"] = 1024;
    c["search"] = "PLA & Blue";
    TEST_ASSERT_TRUE(f.service.process(c.as<JsonObjectConst>()).ok());
    TEST_ASSERT_EQUAL(1032, f.view["next_offset"].as<int>());
    TEST_ASSERT_TRUE(f.view["has_more"].as<bool>());
    TEST_ASSERT_TRUE(f.http.urls.back().find("offset=1024") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(f.http.urls.back().find("PLA%20%26%20Blue") !=
                     std::string::npos);
  }
}
void catalog_malformed_and_timeout() {
  ServiceFixture f;
  f.http.bad_page = true;
  TEST_ASSERT_FALSE(
      f.run(R"({"action":"catalog","entity":"spool","offset":0})").ok());
  f.http.offline = true;
  TEST_ASSERT_FALSE(
      f.run(R"({"action":"catalog","entity":"spool","offset":0})").ok());
}
void community_contract_drift_rejected() {
  ServiceFixture f;
  TEST_ASSERT_FALSE(
      f.run(R"({"action":"import_preview","contract":"unknown","entry":{}})")
          .ok());
  TEST_ASSERT_EQUAL(0, f.http.urls.size());
}
void community_missing_required_rejected() {
  ServiceFixture f;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"import_preview","contract":"spoolmandb-community/0a39c9b5","entry":{"id":"x","manufacturer":"Acme","name":"PLA","material":"PLA","density":null,"diameter":1.75}})")
          .ok());
  TEST_ASSERT_EQUAL(0, f.http.urls.size());
}
const char *import_command =
    R"({"action":"import_preview","contract":"spoolmandb-community/0a39c9b5","entry":{"id":"acme_blue","manufacturer":"Acme","name":"Blue","material":"PLA","density":1.24,"diameter":1.75,"weight":1000,"spool_weight":null,"spool_type":null,"is_refill":false,"color_hex":null}})";
void community_import_case(int failure_mode, bool existing) {
  ServiceFixture f;
  int vendor_posts = 0, filament_posts = 0, canonical_reads = 0;
  f.http.custom = [&](const network::HttpRequest &r) -> std::string {
    if (r.url.find("/vendor?") != std::string::npos)
      return existing ? "[{\"id\":2}]" : "[]";
    if (r.url.find("/filament?") != std::string::npos)
      return existing ? "[{\"id\":4}]" : "[]";
    if (r.url.find("/vendor") != std::string::npos && r.method == "POST") {
      ++vendor_posts;
      if (failure_mode == 1)
        f.http.custom_status = 500;
      return "{\"id\":2}";
    }
    if (r.url.find("/vendor/2") != std::string::npos)
      return "{\"id\":2,\"name\":\"Acme\"}";
    if (r.url.find("/filament") != std::string::npos && r.method == "POST") {
      ++filament_posts;
      if (failure_mode == 2)
        f.http.custom_status = 500;
      return "{\"id\":4}";
    }
    if (r.url.find("/filament/4") != std::string::npos) {
      ++canonical_reads;
      if (failure_mode == 3)
        return "{";
      return "{\"id\":4,\"name\":\"Canonical Blue\"}";
    }
    return {};
  };
  TEST_ASSERT_TRUE(f.run(import_command).ok());
  TEST_ASSERT_TRUE(f.view["proposed_filament"]["spool_weight"].isNull());
  network::BackendDocument confirm;
  confirm["action"] = "import";
  confirm["import_token"] = f.view["import_token"];
  auto result = f.service.process(confirm.as<JsonObjectConst>());
  TEST_ASSERT_EQUAL(failure_mode == 0, result.ok());
  if (!failure_mode) {
    TEST_ASSERT_EQUAL_STRING("Canonical Blue",
                             f.view["filament"]["name"].as<const char *>());
    TEST_ASSERT_EQUAL(1, canonical_reads);
  }
  if (existing) {
    TEST_ASSERT_EQUAL(0, vendor_posts);
    TEST_ASSERT_EQUAL(0, filament_posts);
  }
  if (failure_mode == 1)
    TEST_ASSERT_EQUAL(0, filament_posts);
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void community_success_and_canonical_readback() {
  community_import_case(0, false);
}
void community_duplicate_import_reuses() { community_import_case(0, true); }
void station_community_selection_uses_import_review() {
  ServiceFixture f;
  f.http.custom=[](const network::HttpRequest& r)->std::string {
    return r.url.find("/vendor?")!=std::string::npos||r.url.find("/filament?")!=std::string::npos?"[]":"";
  };
  TEST_ASSERT_TRUE(f.run(R"({"action":"community_search","search":"Acme","offset":0,"_operation_id":42})").ok());
  TEST_ASSERT_EQUAL_STRING("community",f.view["phase"]);
  TEST_ASSERT_EQUAL(42,f.view["operation_id"].as<int>());
  TEST_ASSERT_TRUE(f.run(R"({"action":"community_select","id":"acme_blue","import_name":"My blue PLA"})").ok());
  TEST_ASSERT_EQUAL_STRING("import_preview",f.view["phase"]);
  TEST_ASSERT_EQUAL_STRING("My blue PLA",f.view["proposed_filament"]["name"]);
  TEST_ASSERT_EQUAL_STRING("Blue",f.view["source"]["name"]);
  TEST_ASSERT_FALSE(f.run(R"({"action":"community_select","id":"other"})").ok());
  TEST_ASSERT_EQUAL(0,f.reader.reads);TEST_ASSERT_EQUAL(0,f.reader.writes);TEST_ASSERT_EQUAL(0,f.http.patches);
}
void community_vendor_failure() { community_import_case(1, false); }
void community_filament_failure() { community_import_case(2, false); }
void community_canonical_failure() { community_import_case(3, false); }
void community_oversized_and_unknown_fields() {
  ServiceFixture f;
  network::BackendDocument c;
  deserializeJson(c, import_command);
  c["entry"]["name"] = std::string(65, 'x');
  TEST_ASSERT_FALSE(f.service.process(c.as<JsonObjectConst>()).ok());
  c["entry"]["name"] = "Blue";
  c["entry"]["future_field"] = true;
  TEST_ASSERT_FALSE(f.service.process(c.as<JsonObjectConst>()).ok());
  TEST_ASSERT_EQUAL(0, f.http.urls.size());
}
void same_spool_identity_retained() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  const auto identity = f.view["instance_uuid"].as<std::string>();
  TEST_ASSERT_TRUE(f.confirm().ok());
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL_STRING(identity.c_str(),
                           f.view["instance_uuid"].as<const char *>());
  TEST_ASSERT_EQUAL(0, f.view["total_blocks"].as<int>());
}
void preview_psram_failure_has_no_write() {
  ServiceFixture f;
  network::BackendDocument c;
  c["action"] = "preview";
  c["spool_id"] = 12;
  network::backend_allocation_admission = [](std::size_t) { return false; };
  auto result = f.service.process(c.as<JsonObjectConst>());
  network::backend_allocation_admission = nullptr;
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
  TEST_ASSERT_EQUAL(0, f.http.urls.size());
}
void repeated_catalog_releases_parser() {
  const auto before = network::backend_json_allocator.used();
  {
    ServiceFixture f;
    for (int i = 0; i < 100; ++i)
      TEST_ASSERT_TRUE(
          f.run(
               R"({"action":"catalog","entity":"spool","offset":10000,"search":"Blue"})")
              .ok());
  }
  TEST_ASSERT_EQUAL(before, network::backend_json_allocator.used());
}
struct Journal : nfc::WriterJournal {
  nfc::WriterPlan saved;
  bool present = false, permit = true, fail_clear = false;
  int spool = 0;
  std::uint32_t backend = 0;
  bool load(nfc::WriterPlan &p, std::int32_t &id, std::uint32_t &b) override {
    if (!present)
      return false;
    p = saved;
    p.attempted = true;
    p.verified = p.cleanup_pending;
    id = spool;
    b = backend;
    return true;
  }
  bool save(const nfc::WriterPlan &p, std::int32_t id,
            std::uint32_t b) override {
    if (!permit)
      return false;
    saved = p;
    spool = id;
    backend = b;
    present = true;
    return true;
  }
  bool clear() override {
    if (!permit || fail_clear)
      return false;
    present = false;
    return true;
  }
};
void recovery_case(bool replacement, bool move = false) {
  Reader reader;
  Http http;
  Journal journal;
  if (move)
    http.claim_previous(reader.uid.hex());
  reader.before_write = [&] { TEST_ASSERT_TRUE(journal.present); };
  integrations::spoolman::SpoolmanAdapter adapter{http,
                                                  {"http://spoolman.test"}};
  network::BackendDocument view;
  auto generation = [&] { return reader.generation; };
  auto random = [](std::uint8_t *p, std::size_t n) { std::memset(p, 0x33, n); };
  auto publish = [&](const auto &body) {
    deserializeJson(view, body.data(), body.size());
  };
  {
    services::TagWriterService service(adapter, reader, generation, random,
                                       publish, &journal, {}, accept_verified_association);
    network::BackendDocument c;
    c["action"] = "preview";
    c["spool_id"] = 12;
    TEST_ASSERT_TRUE(service.process(c.as<JsonObjectConst>()).ok());
    c.clear();
    c["action"] = "write";
    for (const auto *key : {"uid", "generation", "spool_id",
                            "previous_spool_id", "target_checksum"})
      c[key] = view[key];
    http.offline = !move;
    http.fail_target = move;
    TEST_ASSERT_FALSE(service.process(c.as<JsonObjectConst>()).ok());
    TEST_ASSERT_TRUE(journal.present);
    if (move) {
      TEST_ASSERT_EQUAL(9, journal.saved.previous_spool_id);
      TEST_ASSERT_EQUAL(1, http.clears);
      TEST_ASSERT_EQUAL_STRING("association_pending",
                               view["phase"].as<const char *>());
    }
  }
  const auto writes = reader.writes;
  http.offline = false;
  http.fail_target = false;
  if (replacement)
    reader.uid.bytes[7] ^= 1;
  {
    services::TagWriterService service(adapter, reader, generation, random,
                                       publish, &journal, {}, accept_verified_association);
    network::BackendDocument c;
    c["action"] = "preview";
    c["spool_id"] = 12;
    TEST_ASSERT_TRUE(service.process(c.as<JsonObjectConst>()).ok());
    if (replacement) {
      TEST_ASSERT_FALSE(service.physical_pass());
      TEST_ASSERT_EQUAL_STRING("preview", view["phase"].as<const char *>());
      TEST_ASSERT_EQUAL(writes, reader.writes);
      TEST_ASSERT_EQUAL(0, http.patches);
      TEST_ASSERT_TRUE(journal.present);
      return;
    }
    TEST_ASSERT_EQUAL_STRING("association_pending",
                             view["phase"].as<const char *>());
    TEST_ASSERT_EQUAL(move ? 9 : 0, view["previous_spool_id"].as<int>());
    c.clear();
    c["action"] = "retry_association";
    TEST_ASSERT_TRUE(service.process(c.as<JsonObjectConst>()).ok());
  }
  TEST_ASSERT_EQUAL(writes, reader.writes);
  TEST_ASSERT_FALSE(journal.present);
  if (move)
    TEST_ASSERT_EQUAL(1, http.clears);
}
void recovery_after_reboot_associates_without_write() { recovery_case(false); }
void uid_move_recovery_after_reboot_without_write() {
  recovery_case(false, true);
}
void journal_does_not_recover_replacement_uid() { recovery_case(true); }
void repeated_writer_lifecycles_release_storage() {
  const auto baseline = network::backend_json_allocator.used();
  for (int i = 0; i < 20; ++i) {
    canonical_association_readback();
    association_pending_retry_without_write();
    recovery_case(false);
    {
      ServiceFixture f;
      for (int preview = 0; preview < 5; ++preview)
        TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
      f.reader.fail_write = 2;
      TEST_ASSERT_FALSE(f.confirm().ok());
      TEST_ASSERT_FALSE(f.service.physical_pass());
      TEST_ASSERT_EQUAL(0, f.http.patches);
    }
    TEST_ASSERT_EQUAL(baseline, network::backend_json_allocator.used());
  }
}
void settings_change_invalidates_confirmation() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.adapter.configure({"http://different-spoolman.test"});
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void malformed_canonical_and_import_ids_fail_closed() {
  ServiceFixture f;
  f.http.spool["filament"].remove("id");
  TEST_ASSERT_FALSE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
  f.http.custom = [](const network::HttpRequest &) { return "[{\"id\":0}]"; };
  TEST_ASSERT_FALSE(f.run(import_command).ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
}
void spool_material_filter_uses_documented_parameter() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"catalog","entity":"spool","offset":0,"material":"PLA"})")
          .ok());
  TEST_ASSERT_TRUE(f.http.urls.back().find("filament.material=PLA") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(
      f.run(R"({"action":"catalog","entity":"spool","offset":0,"material":""})")
          .ok());
  TEST_ASSERT_TRUE(f.http.urls.back().find("filament.material=") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"catalog","entity":"filament","offset":0,"material":"","article_number":""})")
          .ok());
  TEST_ASSERT_TRUE(f.http.urls.back().find("material=") == std::string::npos);
  TEST_ASSERT_TRUE(f.http.urls.back().find("article_number=") ==
                   std::string::npos);
}
void journal_failure_prevents_write() {
  Reader reader;
  Http http;
  Journal journal;
  journal.permit = false;
  integrations::spoolman::SpoolmanAdapter adapter{http,
                                                  {"http://spoolman.test"}};
  network::BackendDocument view;
  services::TagWriterService service(
      adapter, reader, [&] { return reader.generation; },
      [](std::uint8_t *p, std::size_t n) { std::memset(p, 0x33, n); },
      [&](const auto &b) { deserializeJson(view, b.data(), b.size()); },
      &journal);
  network::BackendDocument c;
  c["action"] = "preview";
  c["spool_id"] = 12;
  TEST_ASSERT_TRUE(service.process(c.as<JsonObjectConst>()).ok());
  c.clear();
  c["action"] = "write";
  for (const auto *key : {"uid", "generation", "spool_id", "previous_spool_id",
                          "target_checksum"})
    c[key] = view[key];
  TEST_ASSERT_FALSE(service.process(c.as<JsonObjectConst>()).ok());
  TEST_ASSERT_EQUAL(0, reader.writes);
}
void partial_recovery_requires_known_blocks() {
  Fixture f;
  f.prepare();
  f.reader.fail_write = 3;
  TEST_ASSERT_FALSE(f.execute().ok());
  nfc::WriterPlan next;
  TEST_ASSERT_TRUE(f.writer.read(next, &f.plan).ok());
  TEST_ASSERT_TRUE(next.recovery);
  f.reader.bytes[200] = 0x73;
  TEST_ASSERT_FALSE(f.writer.read(next, &f.plan).ok());
}
void diameter_current_key_and_legacy_compatibility() {
  Fixture f;
  f.prepare();
  TEST_ASSERT_EQUAL(0, f.plan.proposed.material.unknown_main_fields);
  TEST_ASSERT_FLOAT_WITHIN(.00001, 1.75,
                           *f.plan.proposed.material.filament_diameter);
  std::ofstream image(".pio/writer-mapping.bin", std::ios::binary);
  image.write(reinterpret_cast<const char *>(f.plan.target.data()), 312);
  image.close();
  TEST_ASSERT_TRUE(image.good());
  f.spool["filament"]["diameter"] = 1.001;
  f.prepare();
  TEST_ASSERT_FLOAT_WITHIN(.0000001, 1.001,
                           *f.plan.proposed.material.filament_diameter);
  f.spool["filament"]["diameter"] = 1e300;
  TEST_ASSERT_FALSE(nfc::openprinttag::map_spoolman(
                        f.spool.as<JsonObjectConst>(), uuid, f.plan, false)
                        .ok());
}

void prepare_interrupted_rewrite(Fixture &f) {
  f.prepare();
  TEST_ASSERT_TRUE(f.execute().ok());
  f.spool["filament"]["name"] = "PLA Gold";
  f.spool["used_weight"] = 250;
  f.prepare();
  f.plan.attempted = true;
}
void make_valid_partial(Fixture &f) {
  prepare_interrupted_rewrite(f);
  TEST_ASSERT_GREATER_THAN(1, f.plan.count);
  const auto block = f.plan.blocks[0];
  std::copy_n(f.plan.target.data() + block * 4, 4,
              f.reader.bytes.data() + block * 4);
  nfc::openprinttag::DecodedTag decoded;
  TEST_ASSERT_TRUE(
      nfc::openprinttag::Codec::decode({f.reader.bytes.data(), 312}, decoded)
          .ok());
  TEST_ASSERT_TRUE(decoded.material.validation.valid());
  TEST_ASSERT_TRUE(f.reader.bytes != f.plan.original &&
                   f.reader.bytes != f.plan.target);
}
void journal_valid_decode_partial_is_recovery() {
  Fixture f;
  make_valid_partial(f);
  nfc::WriterPlan read;
  TEST_ASSERT_TRUE(f.writer.read(read, &f.plan).ok());
  TEST_ASSERT_TRUE(read.recovery);
  TEST_ASSERT_TRUE(read.journal_state ==
                   nfc::WriterPlan::JournalState::partial);
}
void journal_exact_original_is_normal() {
  Fixture f;
  prepare_interrupted_rewrite(f);
  nfc::WriterPlan read;
  TEST_ASSERT_TRUE(f.writer.read(read, &f.plan).ok());
  TEST_ASSERT_FALSE(read.recovery);
  TEST_ASSERT_TRUE(read.journal_state ==
                   nfc::WriterPlan::JournalState::original);
}
void journal_exact_target_is_classified_before_decode() {
  Fixture f;
  prepare_interrupted_rewrite(f);
  f.reader.bytes = f.plan.target;
  nfc::WriterPlan read;
  TEST_ASSERT_TRUE(f.writer.read(read, &f.plan).ok());
  TEST_ASSERT_FALSE(read.recovery);
  TEST_ASSERT_TRUE(read.journal_state == nfc::WriterPlan::JournalState::target);
  TEST_ASSERT_TRUE(read.current.material.validation.valid());
}
void corrupt_valid_text(Reader &reader) {
  const char text[] = "PLA Gold";
  auto found = std::search(reader.bytes.begin(), reader.bytes.begin() + 312,
                           text, text + 8);
  TEST_ASSERT_TRUE(found != reader.bytes.begin() + 312);
  *found = 'Q';
  nfc::openprinttag::DecodedTag decoded;
  TEST_ASSERT_TRUE(
      nfc::openprinttag::Codec::decode({reader.bytes.data(), 312}, decoded)
          .ok());
  TEST_ASSERT_TRUE(decoded.material.validation.valid());
}
void journal_valid_decode_unauthorized_block_refused() {
  Fixture f;
  prepare_interrupted_rewrite(f);
  f.reader.bytes = f.plan.target;
  corrupt_valid_text(f.reader);
  nfc::WriterPlan read;
  TEST_ASSERT_FALSE(f.writer.read(read, &f.plan).ok());
  TEST_ASSERT_FALSE(read.verified);
}
void journal_system_security_geometry_mismatch_refused() {
  for (int kind = 0; kind < 3; ++kind) {
    Fixture f;
    prepare_interrupted_rewrite(f);
    if (kind == 0)
      ++f.plan.system.bytes[0];
    if (kind == 1)
      f.plan.security[79] = 1;
    if (kind == 2)
      --f.plan.geometry.block_count;
    nfc::WriterPlan read;
    TEST_ASSERT_FALSE(f.writer.read(read, &f.plan).ok());
  }
}
void journal_survives_multiple_unconfirmed_previews() {
  Fixture f;
  make_valid_partial(f);
  Http http;
  Journal journal;
  journal.saved = f.plan;
  journal.present = true;
  journal.spool = 12;
  integrations::spoolman::SpoolmanAdapter adapter{http,
                                                  {"http://spoolman.test"}};
  network::BackendDocument view, command;
  services::TagWriterService service(
      adapter, f.reader, [&] { return f.reader.generation; },
      [](std::uint8_t *p, std::size_t n) { std::memset(p, 0x33, n); },
      [&](const auto &b) { deserializeJson(view, b.data(), b.size()); },
      &journal);
  command["action"] = "preview";
  command["spool_id"] = 12;
  TEST_ASSERT_TRUE(service.process(command.as<JsonObjectConst>()).ok());
  TEST_ASSERT_TRUE(view["recovering_interrupted_write"].as<bool>());
  // Even a later valid decode cannot replace the durable transaction's bounds.
  f.reader.bytes = f.plan.target;
  corrupt_valid_text(f.reader);
  TEST_ASSERT_FALSE(service.process(command.as<JsonObjectConst>()).ok());
  TEST_ASSERT_FALSE(service.physical_pass());
}
void uid_unowned_and_target_owned() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(0, f.view["previous_spool_id"].as<int>());
  TEST_ASSERT_TRUE(f.confirm().ok());
  const auto writes = f.reader.writes;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(0, f.view["previous_spool_id"].as<int>());
  TEST_ASSERT_TRUE(f.confirm().ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
}
void uid_repurpose_preview_discloses_and_binds_owner() {
  ServiceFixture f;
  f.http.claim_previous(f.reader.uid.hex());
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(9, f.view["previous_spool_id"].as<int>());
  TEST_ASSERT_EQUAL(12, f.view["spool_id"].as<int>());
  TEST_ASSERT_EQUAL_STRING(f.reader.uid.hex().c_str(),
                           f.view["uid"].as<const char *>());
  TEST_ASSERT_TRUE(f.view["repurpose"].as<bool>());
  std::string warnings;
  serializeJson(f.view["warnings"], warnings);
  TEST_ASSERT_TRUE(warnings.find("association will move") != std::string::npos);
  f.view["previous_spool_id"] = 0;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void uid_multiple_owners_refused() {
  ServiceFixture f;
  f.http.claim_previous(f.reader.uid.hex());
  f.http.multiple_uid = true;
  TEST_ASSERT_FALSE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void uid_move_clears_only_previous_uid_and_verifies() {
  ServiceFixture f;
  f.http.claim_previous("e0:04:01:08:66:27:d8:d4");
  const auto old_uuid =
      f.http.previous["extra"]["opentag_instance_uuid"].as<std::string>();
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_TRUE(f.confirm().ok());
  TEST_ASSERT_EQUAL(1, f.http.clears);
  TEST_ASSERT_EQUAL(1, f.http.patches);
  TEST_ASSERT_TRUE(f.http.previous["extra"]["nfc_uid"].isNull());
  TEST_ASSERT_EQUAL_STRING(
      old_uuid.c_str(),
      f.http.previous["extra"]["opentag_instance_uuid"].as<const char *>());
  TEST_ASSERT_EQUAL_STRING("\"unchanged\"",
                           f.http.previous["extra"]["keep"].as<const char *>());
  auto clear =
      std::find(f.http.events.begin(), f.http.events.end(), "PATCH /spool/9");
  auto target =
      std::find(f.http.events.begin(), f.http.events.end(), "PATCH /spool/12");
  TEST_ASSERT_TRUE(clear < target);
  TEST_ASSERT_TRUE(std::find(clear + 1, target, "GET /spool/9") != target);
  for (const auto &url : f.http.urls)
    if (url.find("extra.nfc_uid") != std::string::npos)
      TEST_ASSERT_TRUE(url.find("allow_archived=true") != std::string::npos);
  TEST_ASSERT_TRUE(f.http.previous["archived"].as<bool>());
}
void uid_previous_clear_failure_stops_target() {
  ServiceFixture f;
  f.http.claim_previous(f.reader.uid.hex());
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.http.fail_clear = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
  TEST_ASSERT_EQUAL_STRING("association_pending",
                           f.view["phase"].as<const char *>());
  const auto writes = f.reader.writes;
  f.http.fail_clear = false;
  TEST_ASSERT_TRUE(f.run(R"({"action":"retry_association"})").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
}
void uid_previous_clear_readback_failure_stops_target() {
  ServiceFixture f;
  f.http.claim_previous(f.reader.uid.hex());
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.http.bad_clear_readback = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
}
void uid_target_patch_failure_after_clear_is_retryable() {
  ServiceFixture f;
  f.http.claim_previous(f.reader.uid.hex());
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.http.fail_target = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_TRUE(f.http.previous["extra"]["nfc_uid"].isNull());
  TEST_ASSERT_EQUAL_STRING("association_pending",
                           f.view["phase"].as<const char *>());
  const auto writes = f.reader.writes;
  f.http.fail_target = false;
  TEST_ASSERT_TRUE(f.run(R"({"action":"retry_association"})").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
  TEST_ASSERT_EQUAL(1, f.http.clears);
}
void uid_owner_changed_after_preview_is_not_cleared() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.http.claim_previous(f.reader.uid.hex());
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.http.clears);
  TEST_ASSERT_EQUAL(0, f.http.patches);
  TEST_ASSERT_EQUAL_STRING("association_pending",
                           f.view["phase"].as<const char *>());
}
void uid_final_uniqueness_recheck_blocks_success() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.http.final_uid_conflict = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL_STRING("association_pending",
                           f.view["phase"].as<const char *>());
  const auto writes = f.reader.writes;
  f.http.final_uid_conflict = false;
  TEST_ASSERT_TRUE(f.run(R"({"action":"retry_association"})").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
}
void uid_formatted_queries_and_cross_format_conflicts() {
  ServiceFixture f;
  bool conflict = false;
  f.http.custom = [&](const network::HttpRequest &request) -> std::string {
    if (request.url.find("extra.nfc_uid=") == std::string::npos)
      return {};
    if (request.url.find("%3A") != std::string::npos &&
        request.url.find("e0") != std::string::npos)
      return R"([{"id":9}])";
    if (conflict && request.url.find("E0") != std::string::npos)
      return R"([{"id":99}])";
    return "[]";
  };
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(9, f.view["previous_spool_id"].as<int>());
  conflict = true;
  TEST_ASSERT_FALSE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void assert_edit_conflict(ServiceFixture &f, const char *request) {
  const auto calls = f.reader.field_calls;
  const auto reads = f.reader.reads;
  const auto result = f.run(request);
  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_TRUE(result.error().category == core::ErrorCategory::conflict);
  TEST_ASSERT_TRUE(f.view["edit_conflict"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("failed", f.view["phase"].as<const char *>());
  TEST_ASSERT_EQUAL(0, f.http.patches);
  TEST_ASSERT_EQUAL(calls, f.reader.field_calls);
  TEST_ASSERT_EQUAL(reads, f.reader.reads);
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void edit_used_weight_expected_matches() {
  ServiceFixture f;
  f.http.spool["used_weight"] = 0;
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":0},"changes":{"used_weight":25}})")
          .ok());
  TEST_ASSERT_EQUAL(1, f.http.patches);
  TEST_ASSERT_EQUAL(25, f.view["spool"]["used_weight"].as<int>());
}
void edit_used_weight_conflict_and_explicit_retry() {
  ServiceFixture f;
  f.http.spool["used_weight"] = 15;
  assert_edit_conflict(
      f,
      R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":0},"changes":{"used_weight":25}})");
  TEST_ASSERT_EQUAL(15, f.view["spool"]["used_weight"].as<int>());
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":15},"changes":{"used_weight":25}})")
          .ok());
  TEST_ASSERT_FALSE(f.view["edit_conflict"].as<bool>());
}
void edit_unrelated_change_is_preserved() {
  ServiceFixture f;
  f.http.spool["location"] = "Changed elsewhere";
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":100,"location":"Old location"},"changes":{"used_weight":25}})")
          .ok());
  TEST_ASSERT_EQUAL(1, f.http.patches);
  TEST_ASSERT_EQUAL_STRING("Changed elsewhere",
                           f.view["spool"]["location"].as<const char *>());
}
void edit_filament_expected_matches() {
  ServiceFixture f;
  f.http.spool["filament"]["weight"] = 777.12;
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"update_filament","filament_id":4,"expected":{"weight":777.12},"changes":{"weight":1000}})")
          .ok());
  TEST_ASSERT_EQUAL(1, f.http.patches);
  TEST_ASSERT_EQUAL(1000, f.view["filament"]["weight"].as<int>());
}
void edit_filament_expected_conflicts() {
  ServiceFixture f;
  assert_edit_conflict(
      f,
      R"({"action":"update_filament","filament_id":4,"spool_id":12,"expected":{"weight":777.12},"changes":{"weight":1000}})");
  TEST_ASSERT_EQUAL(4, f.view["filament"]["id"].as<int>());
  TEST_ASSERT_EQUAL(1000, f.view["filament"]["weight"].as<int>());
}
void edit_multiple_fields_conflict_before_any_patch() {
  ServiceFixture f;
  assert_edit_conflict(
      f,
      R"({"action":"update_spool","spool_id":12,"expected":{"initial_weight":1000,"used_weight":0},"changes":{"initial_weight":900,"used_weight":25}})");
  TEST_ASSERT_EQUAL(1000, f.http.spool["initial_weight"].as<int>());
}
void edit_missing_expected_rejected() {
  ServiceFixture f;
  for (
      const auto *request :
      {R"({"action":"update_spool","spool_id":12,"changes":{"used_weight":25}})",
       R"({"action":"update_spool","spool_id":12,"expected":{},"changes":{"used_weight":25}})",
       R"({"action":"update_filament","filament_id":4,"expected":{"weight":1000},"changes":{"weight":900,"name":"New"}})"})
    TEST_ASSERT_FALSE(f.run(request).ok());
  TEST_ASSERT_EQUAL(0, f.http.events.size());
  TEST_ASSERT_EQUAL(0, f.reader.field_calls);
}
void edit_unknown_expected_rejected() {
  ServiceFixture f;
  for (
      const auto *request :
      {R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":100,"extra":null},"changes":{"used_weight":25}})",
       R"({"action":"update_filament","filament_id":4,"expected":{"weight":1000,"vendor_id":null},"changes":{"weight":900}})",
       R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":100,"initial_weight|used_weight":null},"changes":{"used_weight":25}})"})
    TEST_ASSERT_FALSE(f.run(request).ok());
  TEST_ASSERT_EQUAL(0, f.http.events.size());
}
void edit_optional_expected_null_and_zero() {
  for (bool missing : {false, true}) {
    ServiceFixture f;
    if (!missing)
      f.http.spool["price"] = nullptr;
    TEST_ASSERT_TRUE(
        f.run(
             R"({"action":"update_spool","spool_id":12,"expected":{"price":null},"changes":{"price":25}})")
            .ok());
    TEST_ASSERT_EQUAL(1, f.http.patches);
  }
  ServiceFixture f;
  f.http.spool["spool_weight"] = 0;
  assert_edit_conflict(
      f,
      R"({"action":"update_spool","spool_id":12,"expected":{"spool_weight":null},"changes":{"spool_weight":130}})");
  ServiceFixture g;
  g.http.spool["filament"]["weight"] = nullptr;
  TEST_ASSERT_TRUE(
      g.run(
           R"({"action":"update_filament","filament_id":4,"expected":{"weight":null},"changes":{"weight":1000}})")
          .ok());
}
void edit_invalid_expected_types_rejected() {
  ServiceFixture f;
  for (
      const auto *request :
      {R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":null},"changes":{"used_weight":25}})",
       R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":"100"},"changes":{"used_weight":25}})",
       R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":true},"changes":{"used_weight":25}})",
       R"({"action":"update_filament","filament_id":4,"expected":{"weight":-1},"changes":{"weight":1000}})"})
    TEST_ASSERT_FALSE(f.run(request).ok());
  TEST_ASSERT_EQUAL(0, f.http.events.size());
}
void edit_conflict_invalidates_exact_write_preview() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  network::BackendDocument command;
  command["action"] = "write";
  for (auto key : {"uid", "generation", "spool_id", "previous_spool_id",
                   "target_checksum"})
    command[key] = f.view[key];
  f.http.spool["used_weight"] = 115;
  assert_edit_conflict(
      f,
      R"({"action":"update_spool","spool_id":12,"expected":{"used_weight":100},"changes":{"used_weight":125}})");
  const auto calls = f.reader.field_calls;
  TEST_ASSERT_FALSE(f.service.process(command.as<JsonObjectConst>()).ok());
  TEST_ASSERT_EQUAL(calls, f.reader.field_calls);
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void edit_allowlisted_spool_fields_and_no_nfc() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"initial_weight":1100,"used_weight":0,"spool_weight":130,"price":25.5,"location":"Shelf","lot_nr":"Batch","comment":"Verified"},"expected":{"initial_weight":1000,"used_weight":100,"spool_weight":200,"price":null,"location":null,"lot_nr":null,"comment":null}})")
          .ok());
  TEST_ASSERT_EQUAL(1100, f.view["spool"]["initial_weight"].as<int>());
  TEST_ASSERT_EQUAL(0, f.view["spool"]["used_weight"].as<int>());
  TEST_ASSERT_EQUAL_STRING("updated", f.view["phase"].as<const char *>());
  TEST_ASSERT_EQUAL(1, f.http.patches);
  TEST_ASSERT_EQUAL(0, f.reader.field_calls);
  TEST_ASSERT_EQUAL(0, f.reader.reads);
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void edit_allowlisted_filament_and_selected_spool_readback() {
  ServiceFixture f;
  f.http.spool["filament"]["weight"] = 777.12;
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"update_filament","filament_id":4,"spool_id":12,"changes":{"name":"Sunlu PLA+ 2.0 Black","material":"PLA+","weight":1000,"density":1.24,"diameter":1.75,"spool_weight":130,"color_hex":"000000","article_number":"SKU","settings_extruder_temp":215,"settings_bed_temp":60},"expected":{"name":"PLA Blue","material":"PLA","weight":777.12,"density":1.24,"diameter":1.75,"spool_weight":null,"color_hex":"123456","article_number":"SKU123","settings_extruder_temp":215,"settings_bed_temp":null}})")
          .ok());
  TEST_ASSERT_EQUAL(1000, f.view["filament"]["weight"].as<int>());
  TEST_ASSERT_EQUAL(1000, f.view["spool"]["filament"]["weight"].as<int>());
  TEST_ASSERT_EQUAL(0, f.reader.field_calls);
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void edit_rejects_unknown_spool_field() {
  ServiceFixture f;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"initial_weight|used_weight":1},"expected":{"initial_weight|used_weight":null}})")
          .ok());
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"extra":{"nfc_uid":"x"}},"expected":{"extra":null}})")
          .ok());
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"filament_id":99},"expected":{"filament_id":null}})")
          .ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
}
void edit_rejects_unknown_filament_field() {
  ServiceFixture f;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_filament","filament_id":4,"changes":{"vendor_id":99},"expected":{"vendor_id":null}})")
          .ok());
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_filament","filament_id":4,"changes":{"external_id":"x"},"expected":{"external_id":null}})")
          .ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
}
void edit_rejects_invalid_numbers_types_and_strings() {
  ServiceFixture f;
  for (auto json : {R"({"used_weight":-1})", R"({"used_weight":100001})",
                    R"({"used_weight":1e999})", R"({"used_weight":"1000"})",
                    R"({"used_weight":true})", R"({"location":12})"}) {
    network::BackendDocument c;
    c["action"] = "update_spool";
    c["spool_id"] = 12;
    network::BackendDocument change;
    deserializeJson(change, json);
    c["changes"].set(change);
    c["expected"].to<JsonObject>();
    TEST_ASSERT_FALSE(f.service.process(c.as<JsonObjectConst>()).ok());
  }
  for (auto json :
       {R"({"density":0})", R"({"diameter":11})", R"({"weight":0})",
        R"({"color_hex":"red"})", R"({"settings_bed_temp":60.5})"}) {
    network::BackendDocument c;
    c["action"] = "update_filament";
    c["filament_id"] = 4;
    network::BackendDocument change;
    deserializeJson(change, json);
    c["changes"].set(change);
    c["expected"].to<JsonObject>();
    TEST_ASSERT_FALSE(f.service.process(c.as<JsonObjectConst>()).ok());
  }
  TEST_ASSERT_EQUAL(0, f.http.patches);
  TEST_ASSERT_EQUAL(0, f.reader.field_calls);
}
void edit_rejects_malformed_canonical_record() {
  ServiceFixture f;
  f.http.spool["filament"]["density"] = nullptr;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"used_weight":0},"expected":{"used_weight":100}})")
          .ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
}
void edit_string_bounds_include_embedded_nul() {
  ServiceFixture f;
  for (bool embedded : {false, true}) {
    network::BackendDocument c;
    c["action"] = "update_spool";
    c["spool_id"] = 12;
    std::string value(65, 'x');
    if (embedded)
      value[1] = '\0';
    c["changes"]["location"] = value;
    c["expected"]["location"] = nullptr;
    TEST_ASSERT_FALSE(f.service.process(c.as<JsonObjectConst>()).ok());
  }
  TEST_ASSERT_EQUAL(0, f.http.patches);
}
void edit_rejects_wrong_target_and_spool_relationship() {
  ServiceFixture f;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_filament","filament_id":4,"spool_id":9,"changes":{"weight":1000},"expected":{"weight":1000}})")
          .ok());
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_spool","spool_id":0,"changes":{"used_weight":0},"expected":{"used_weight":100}})")
          .ok());
  f.http.spool["id"] = 999;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"used_weight":0},"expected":{"used_weight":100}})")
          .ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
}
void edit_patch_failure_keeps_failure_state() {
  ServiceFixture f;
  f.http.fail_edit = true;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"used_weight":0},"expected":{"used_weight":100}})")
          .ok());
  TEST_ASSERT_EQUAL_STRING("failed", f.view["phase"].as<const char *>());
  TEST_ASSERT_EQUAL(100, f.http.spool["used_weight"].as<int>());
  TEST_ASSERT_EQUAL(0, f.reader.field_calls);
}
void edit_readback_mismatch_fails() {
  ServiceFixture f;
  f.http.mismatch_edit = true;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_filament","filament_id":4,"changes":{"weight":777},"expected":{"weight":1000}})")
          .ok());
  TEST_ASSERT_EQUAL_STRING("failed", f.view["phase"].as<const char *>());
  TEST_ASSERT_TRUE(f.view["filament"].isNull());
}
void edit_only_patches_changed_values() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"used_weight":100},"expected":{"used_weight":100}})")
          .ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
  TEST_ASSERT_EQUAL(2, f.http.events.size());
}
void edit_invalidates_old_preview() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  network::BackendDocument c;
  c["action"] = "write";
  for (auto key : {"uid", "generation", "spool_id", "previous_spool_id",
                   "target_checksum"})
    c[key] = f.view[key];
  const auto calls = f.reader.field_calls;
  TEST_ASSERT_TRUE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"used_weight":0},"expected":{"used_weight":100}})")
          .ok());
  TEST_ASSERT_FALSE(f.service.process(c.as<JsonObjectConst>()).ok());
  TEST_ASSERT_EQUAL(calls, f.reader.field_calls);
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void edit_cannot_bypass_pending_association() {
  ServiceFixture f;
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.http.offline = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  f.http.offline = false;
  TEST_ASSERT_FALSE(
      f.run(
           R"({"action":"update_spool","spool_id":12,"changes":{"used_weight":0},"expected":{"used_weight":100}})")
          .ok());
  TEST_ASSERT_EQUAL_STRING("association_pending",
                           f.view["phase"].as<const char *>());
}

struct ClearFixture {
  Reader reader;
  Http http;
  Journal journal;
  integrations::spoolman::SpoolmanAdapter adapter{http,
                                                  {"http://spoolman.test"}};
  network::BackendDocument view;
  int mappings = 0;
  bool mapping_ok = true;
  Cache cache;
  std::unique_ptr<services::TagWriterService> service;
  ClearFixture() {
    Fixture source;
    source.prepare();
    reader.bytes = source.plan.target;
    http.spool["extra"]["nfc_uid"] = "\"" + reader.uid.hex() + "\"";
    http.spool["extra"]["opentag_instance_uuid"] =
        "\"" + nfc::openprinttag::instance_uuid_text(uuid) + "\"";
    http.spool["extra"]["keep"] = "\"untouched\"";
    restart();
  }
  void restart() {
    service = std::make_unique<services::TagWriterService>(
        adapter, reader, [this] { return reader.generation; },
        [](std::uint8_t *, std::size_t) {},
        [this](const auto &body) {
          deserializeJson(view, body.data(), body.size());
        },
        &journal,
        [this](const std::string &uid, const std::string &instance,
               std::int32_t owner) {
          TEST_ASSERT_EQUAL_STRING(reader.uid.hex().c_str(), uid.c_str());
          TEST_ASSERT_EQUAL_STRING(
              nfc::openprinttag::instance_uuid_text(uuid).c_str(),
              instance.c_str());
          TEST_ASSERT_EQUAL(http.spool["id"].as<int>(), owner);
          ++mappings;
          TEST_ASSERT_TRUE(http.spool["extra"]["nfc_uid"].isNull());
          TEST_ASSERT_TRUE(http.spool["extra"]["opentag_instance_uuid"].isNull());
          TEST_ASSERT_TRUE(http.urls.back().find("extra.opentag_instance_uuid") != std::string::npos);
          return mapping_ok ? cache.configuration.clear_verified_spool_identity_mapping(uid, instance, owner) : failure();
        });
  }
  R run(const char *action) {
    network::BackendDocument c;
    c["action"] = action;
    return service->process(c.as<JsonObjectConst>());
  }
  R confirm() {
    network::BackendDocument c;
    c["action"] = "clear";
    for (auto key :
         {"uid", "generation", "current_checksum", "target_checksum"})
      c[key] = view[key];
    return service->process(c.as<JsonObjectConst>());
  }
  void preview() {
    auto r = run("clear_preview");
    TEST_ASSERT_TRUE_MESSAGE(r.ok(), r.ok() ? "" : r.error().message.c_str());
    TEST_ASSERT_EQUAL_STRING("clear_preview", view["phase"].as<const char *>());
  }
};
void clear_valid_and_unlink_only_owned_fields() {
  ClearFixture f;
  f.preview();
  f.reader.before_write = [&] {
    TEST_ASSERT_TRUE(f.journal.present);
    TEST_ASSERT_EQUAL(0, f.http.urls.size());
  };
  auto r = f.confirm();
  TEST_ASSERT_TRUE_MESSAGE(r.ok(), r.ok() ? "" : r.error().message.c_str());
  TEST_ASSERT_TRUE(std::all_of(f.reader.bytes.begin(),
                               f.reader.bytes.begin() + 312,
                               [](auto b) { return !b; }));
  TEST_ASSERT_EQUAL(0x17, f.reader.bytes[312]);
  TEST_ASSERT_EQUAL(0x92, f.reader.bytes[319]);
  TEST_ASSERT_EQUAL(1, f.http.patches);
  TEST_ASSERT_EQUAL(1, f.mappings);
  TEST_ASSERT_FALSE(f.journal.present);
  TEST_ASSERT_EQUAL_STRING("\"untouched\"",
                           f.http.spool["extra"]["keep"].as<const char *>());
  TEST_ASSERT_EQUAL(100, f.http.spool["used_weight"].as<int>());
  TEST_ASSERT_EQUAL_STRING("cleared", f.view["phase"].as<const char *>());
}
void clear_empty_envelope() {
  ClearFixture f;
  auto image = nfc::openprinttag::Initializer::generate({312, 4, 32, {}});
  TEST_ASSERT_TRUE(image.ok());
  std::copy(image.value().bytes.begin(), image.value().bytes.end(),
            f.reader.bytes.begin());
  f.preview();
  TEST_ASSERT_TRUE(f.confirm().ok());
}
void clear_already_blank_zero_writes() {
  ClearFixture f;
  std::fill_n(f.reader.bytes.begin(), 312, 0);
  f.preview();
  TEST_ASSERT_TRUE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
}
void clear_refuses_unsupported_sources() {
  for (int fault = 0; fault < 4; ++fault) {
    ClearFixture f;
    if (fault == 0)
      f.reader.bytes[0] = 0x42;
    if (fault == 1)
      f.reader.geo.block_count = 79;
    if (fault == 2)
      f.reader.count = 2;
    if (fault == 3)
      f.reader.locks[30] = 1;
    TEST_ASSERT_FALSE(f.run("clear_preview").ok());
    TEST_ASSERT_EQUAL(0, f.reader.writes);
    TEST_ASSERT_EQUAL(0, f.http.patches);
  }
}
void clear_confirmation_physical_fences() {
  for (int fault = 0; fault < 6; ++fault) {
    ClearFixture f;
    f.preview();
    if (fault == 0)
      f.reader.uid.bytes[7] ^= 1;
    if (fault == 1)
      ++f.reader.generation;
    if (fault == 2)
      f.reader.bytes[16] ^= 1;
    if (fault == 3)
      ++f.reader.system;
    if (fault == 4)
      f.reader.locks[30] = 1;
    if (fault == 5)
      f.reader.errors = 1;
    TEST_ASSERT_FALSE(f.confirm().ok());
    TEST_ASSERT_EQUAL(0, f.reader.writes);
    TEST_ASSERT_EQUAL(0, f.http.patches);
  }
}
void clear_requires_every_exact_confirmation_value() {
  for (auto key : {"uid", "generation", "current_checksum", "target_checksum"}) {
    ClearFixture f;
    f.preview();
    f.view[key] = "changed";
    const auto reads = f.reader.reads;
    TEST_ASSERT_FALSE(f.confirm().ok());
    TEST_ASSERT_EQUAL(reads, f.reader.reads);
    TEST_ASSERT_EQUAL(0, f.reader.writes);
    TEST_ASSERT_EQUAL(0, f.http.patches);
  }
}
void clear_readback_faults_stop_unlink() {
  for (int final = 0; final < 2; ++final) {
    ClearFixture f;
    f.preview();
    f.reader.corrupt_read =
        f.reader.reads + 80 +
        (final ? 2 * f.view["changed_blocks"].size() + 2 : 2);
    TEST_ASSERT_FALSE(f.confirm().ok());
    TEST_ASSERT_EQUAL(0, f.http.patches);
    TEST_ASSERT_TRUE(f.journal.present);
  }
}
void clear_power_loss_recovery() {
  for (int point : {0, 1, 5, 12}) {
    ClearFixture f;
    f.preview();
    f.reader.fail_write = point;
    TEST_ASSERT_FALSE(f.confirm().ok());
    TEST_ASSERT_TRUE(f.journal.present);
    TEST_ASSERT_EQUAL(0, f.http.patches);
    f.reader.fail_write = -1;
    f.restart();
    f.preview();
    TEST_ASSERT_TRUE(f.confirm().ok());
    TEST_ASSERT_FALSE(f.journal.present);
  }
}
void clear_header_last_and_unknown_mixture_refused() {
  ClearFixture f;
  f.preview();
  f.reader.fail_write = 2;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.journal.saved.blocks[f.journal.saved.count - 1]);
  f.reader.bytes[250] = 0x55;
  f.restart();
  TEST_ASSERT_FALSE(f.run("clear_preview").ok());
}
void clear_offline_retry_without_nfc() {
  ClearFixture f;
  f.preview();
  f.http.offline = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL_STRING("unlink_pending",
                           f.view["phase"].as<const char *>());
  TEST_ASSERT_TRUE(f.journal.saved.cleanup_pending);
  auto writes = f.reader.writes, reads = f.reader.reads;
  f.http.offline = false;
  f.reader.count = 0;
  TEST_ASSERT_TRUE(f.run("retry_unlink").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
  TEST_ASSERT_EQUAL(reads, f.reader.reads);
}
void clear_restart_pending_without_nfc() {
  ClearFixture f;
  f.preview();
  f.http.fail_target = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  auto writes = f.reader.writes, reads = f.reader.reads;
  f.restart();
  TEST_ASSERT_TRUE(f.service->restore_cleanup().ok());
  TEST_ASSERT_EQUAL_STRING("unlink_pending",
                           f.view["phase"].as<const char *>());
  f.http.fail_target = false;
  f.reader.count = 0;
  TEST_ASSERT_TRUE(f.run("retry_unlink").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
  TEST_ASSERT_EQUAL(reads, f.reader.reads);
}
void clear_mapping_retry_and_owner_conflict() {
  ClearFixture f;
  f.preview();
  f.mapping_ok = false;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_TRUE(f.journal.present);
  auto writes = f.reader.writes;
  f.restart();
  f.http.claim_previous(f.reader.uid.hex());
  TEST_ASSERT_FALSE(f.run("retry_unlink").ok());
  TEST_ASSERT_EQUAL(1, f.http.patches);
  f.http.previous["extra"].remove("nfc_uid");
  f.mapping_ok = true;
  TEST_ASSERT_TRUE(f.run("retry_unlink").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
}

void clear_recovery_refuses_different_tag_without_losing_journal() {
  ClearFixture f;
  f.preview();
  f.reader.fail_write = 2;
  TEST_ASSERT_FALSE(f.confirm().ok());
  auto original_uid = f.journal.saved.uid;
  Fixture other;
  other.prepare();
  f.reader.bytes = other.plan.target;
  f.reader.uid.bytes[7] ^= 1;
  f.restart();
  TEST_ASSERT_FALSE(f.run("clear_preview").ok());
  TEST_ASSERT_TRUE(f.journal.saved.uid == original_uid);
  TEST_ASSERT_EQUAL(0, f.http.patches);
}

void clear_ambiguous_owner_refused() {
  ClearFixture f;
  f.preview();
  f.http.multiple_uid = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.http.patches);
  TEST_ASSERT_TRUE(f.journal.saved.cleanup_pending);
}
void clear_journal_save_failure_zero_writes() {
  ClearFixture f;
  f.preview();
  f.journal.permit = false;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
  TEST_ASSERT_EQUAL(0, f.http.patches);
}
void clear_readback_unlink_mismatch_pending() {
  ClearFixture f;
  f.preview();
  f.http.mismatch_edit = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL_STRING("unlink_pending",
                           f.view["phase"].as<const char *>());
  TEST_ASSERT_EQUAL(0, f.mappings);
}
void journal_v3_roundtrip_and_legacy() {
  Fixture f;
  f.prepare();
  nfc::WriterJournalRecord bytes;
  nfc::encode_writer_journal(bytes, f.plan, 12, 123);
  nfc::WriterPlan p;
  std::int32_t spool;
  std::uint32_t backend;
  TEST_ASSERT_TRUE(nfc::decode_writer_journal({bytes.data(), bytes.size()}, p,
                                              spool, backend));
  TEST_ASSERT_EQUAL(12, spool);
  TEST_ASSERT_EQUAL(123, backend);
  for (auto size : {813, 817}) {
    auto old = bytes;
    std::memcpy(old.data(), size == 813 ? "OPTWR001" : "OPTWR002", 8);
    nfc::journal_put(old.data(), size - 4,
                     nfc::nfcv::diagnostic_checksum(old.data(), size - 4));
    TEST_ASSERT_TRUE(nfc::decode_writer_journal({old.data(), std::size_t(size)},
                                                p, spool, backend));
    TEST_ASSERT_TRUE(p.operation == nfc::WriterPlan::Operation::write);
    TEST_ASSERT_FALSE(p.cleanup_pending);
  }
  f.plan.original = f.plan.target;
  TEST_ASSERT_TRUE(f.writer.plan_clear(f.plan).ok());
  f.plan.cleanup_pending = true;
  f.plan.cleanup_owner_bound = true;
  f.plan.cleared_instance = uuid;
  nfc::encode_writer_journal(bytes, f.plan, 12, 123);
  TEST_ASSERT_TRUE(nfc::decode_writer_journal({bytes.data(), bytes.size()}, p,
                                              spool, backend));
  TEST_ASSERT_TRUE(p.verified);
  TEST_ASSERT_TRUE(p.cleared_instance == uuid);
  bytes[485] = 1;
  nfc::journal_put(bytes.data(), 832,
                   nfc::nfcv::diagnostic_checksum(bytes.data(), 832));
  TEST_ASSERT_FALSE(nfc::decode_writer_journal({bytes.data(), bytes.size()}, p,
                                               spool, backend));
}

void verified_association_repairs_stale_cache_after_remote_checks() {
  ServiceFixture f;
  f.cache.stale(f.reader.uid.hex());
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  TEST_ASSERT_EQUAL(0, f.sync_calls);
  f.cache.documents.fail_save = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL_STRING("association_pending", f.view["phase"].as<const char*>());
  TEST_ASSERT_EQUAL(17, f.cache.configuration.snapshot().spool_identity_mappings[0].spool_id);
  const auto writes = f.reader.writes, reads = f.reader.reads;
  f.cache.documents.fail_save = false;
  TEST_ASSERT_TRUE(f.run(R"({"action":"retry_association"})").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
  TEST_ASSERT_EQUAL(reads, f.reader.reads);
  config::ConfigurationService restored(f.cache.documents, f.cache.legacy);
  TEST_ASSERT_TRUE(restored.initialize().ok());
  const auto mappings = restored.snapshot().spool_identity_mappings;
  TEST_ASSERT_EQUAL(2, mappings.size());
  TEST_ASSERT_EQUAL(12, mappings[0].spool_id);
  TEST_ASSERT_EQUAL_STRING(f.reader.uid.hex().c_str(), mappings[0].nfc_uid->c_str());
  TEST_ASSERT_EQUAL_STRING(f.view["instance_uuid"].as<const char*>(), mappings[0].instance_uuid->c_str());
  TEST_ASSERT_EQUAL(18, mappings[1].spool_id);
}
void failed_remote_association_never_updates_cache() {
  ServiceFixture f;
  f.cache.stale(f.reader.uid.hex());
  TEST_ASSERT_TRUE(f.run(R"({"action":"preview","spool_id":12})").ok());
  f.http.final_uid_conflict = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL(0, f.sync_calls);
  TEST_ASSERT_EQUAL(17, f.cache.configuration.snapshot().spool_identity_mappings[0].spool_id);
}
void clear_stale_cache_persistence_failure_recovers_without_nfc() {
  ClearFixture f;
  f.http.spool["id"] = 28;
  f.cache.stale(f.reader.uid.hex());
  f.cache.documents.fail_save = true;
  f.preview();
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL_STRING("unlink_pending", f.view["phase"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("local_identity", f.view["cleanup_stage"].as<const char*>());
  TEST_ASSERT_TRUE(f.journal.present);
  TEST_ASSERT_EQUAL(17, f.cache.configuration.snapshot().spool_identity_mappings[0].spool_id);
  const auto writes = f.reader.writes, reads = f.reader.reads;
  f.restart();
  f.reader.count = 0;
  f.cache.documents.fail_save = false;
  TEST_ASSERT_TRUE(f.run("retry_unlink").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
  TEST_ASSERT_EQUAL(reads, f.reader.reads);
  TEST_ASSERT_FALSE(f.journal.present);
  TEST_ASSERT_EQUAL_STRING("cleared", f.view["phase"].as<const char*>());
  config::ConfigurationService restored(f.cache.documents, f.cache.legacy);
  TEST_ASSERT_TRUE(restored.initialize().ok());
  TEST_ASSERT_EQUAL(1, restored.snapshot().spool_identity_mappings.size());
  TEST_ASSERT_EQUAL(18, restored.snapshot().spool_identity_mappings[0].spool_id);
}
void journal_cleanup_failure_is_distinct_and_retryable() {
  ClearFixture f;
  f.preview();
  f.journal.fail_clear = true;
  TEST_ASSERT_FALSE(f.confirm().ok());
  TEST_ASSERT_EQUAL_STRING("journal", f.view["cleanup_stage"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("unlink_pending", f.view["phase"].as<const char*>());
  TEST_ASSERT_TRUE(f.journal.present);
  const auto writes = f.reader.writes, reads = f.reader.reads;
  f.journal.fail_clear = false;
  TEST_ASSERT_TRUE(f.run("retry_unlink").ok());
  TEST_ASSERT_EQUAL(writes, f.reader.writes);
  TEST_ASSERT_EQUAL(reads, f.reader.reads);
}

void restart_v3_verified_clear_repairs_stale_cache_without_nfc() {
  ClearFixture f;
  f.cache.stale(f.reader.uid.hex());
  f.preview();
  f.mapping_ok = false;
  TEST_ASSERT_FALSE(f.confirm().ok());
  // Reproduce the reported UID, blank-target checksum and 34 changed blocks
  // in a V3 cleanup-pending record. No physical tag is needed after restart.
  f.http.spool["id"] = f.journal.spool = 28;
  f.journal.saved.original.fill(0);
  f.journal.saved.target.fill(0);
  for (std::size_t block = 0; block < 34; ++block)
    f.journal.saved.original[block * 4] = 1;
  TEST_ASSERT_EQUAL_STRING("E00401086627D8D4", f.journal.saved.uid.hex().c_str());
  TEST_ASSERT_EQUAL_HEX32(0x97B79EC5, nfc::nfcv::diagnostic_checksum(
      f.journal.saved.target.data(), f.journal.saved.target.size()));
  // Exercise the real V3 byte codec, not merely an in-memory WriterPlan copy.
  nfc::WriterJournalRecord bytes;
  nfc::encode_writer_journal(bytes, f.journal.saved, f.journal.spool, f.journal.backend);
  TEST_ASSERT_TRUE(nfc::decode_writer_journal({bytes.data(),bytes.size()},
      f.journal.saved, f.journal.spool, f.journal.backend));
  TEST_ASSERT_TRUE(f.journal.saved.cleanup_pending);
  TEST_ASSERT_TRUE(f.journal.saved.cleanup_owner_bound);
  f.restart();
  f.reader.count = 0;
  f.reader.writes = f.reader.reads = f.reader.field_calls = 0;
  f.mapping_ok = true;
  TEST_ASSERT_TRUE(f.service->restore_cleanup().ok());
  TEST_ASSERT_EQUAL_STRING("unlink_pending", f.view["phase"].as<const char*>());
  TEST_ASSERT_TRUE(f.run("retry_unlink").ok());
  TEST_ASSERT_EQUAL(0, f.reader.writes);
  TEST_ASSERT_EQUAL(0, f.reader.reads);
  TEST_ASSERT_EQUAL(0, f.reader.field_calls);
  TEST_ASSERT_FALSE(f.journal.present);
  TEST_ASSERT_EQUAL_STRING("cleared", f.view["phase"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Tag cleared and verified. Ready to reuse.", f.view["message"].as<const char*>());
}

} // namespace
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(verified_association_repairs_stale_cache_after_remote_checks);
  RUN_TEST(failed_remote_association_never_updates_cache);
  RUN_TEST(clear_stale_cache_persistence_failure_recovers_without_nfc);
  RUN_TEST(restart_v3_verified_clear_repairs_stale_cache_without_nfc);
  RUN_TEST(journal_cleanup_failure_is_distinct_and_retryable);
  RUN_TEST(clear_recovery_refuses_different_tag_without_losing_journal);
  RUN_TEST(clear_valid_and_unlink_only_owned_fields);
  RUN_TEST(clear_empty_envelope);
  RUN_TEST(clear_already_blank_zero_writes);
  RUN_TEST(clear_refuses_unsupported_sources);
  RUN_TEST(clear_confirmation_physical_fences);
  RUN_TEST(clear_requires_every_exact_confirmation_value);
  RUN_TEST(clear_readback_faults_stop_unlink);
  RUN_TEST(clear_power_loss_recovery);
  RUN_TEST(clear_header_last_and_unknown_mixture_refused);
  RUN_TEST(clear_offline_retry_without_nfc);
  RUN_TEST(clear_restart_pending_without_nfc);
  RUN_TEST(clear_mapping_retry_and_owner_conflict);
  RUN_TEST(clear_ambiguous_owner_refused);
  RUN_TEST(clear_journal_save_failure_zero_writes);
  RUN_TEST(clear_readback_unlink_mismatch_pending);
  RUN_TEST(journal_v3_roundtrip_and_legacy);
  RUN_TEST(edit_used_weight_expected_matches);
  RUN_TEST(edit_used_weight_conflict_and_explicit_retry);
  RUN_TEST(edit_unrelated_change_is_preserved);
  RUN_TEST(edit_filament_expected_matches);
  RUN_TEST(edit_filament_expected_conflicts);
  RUN_TEST(edit_multiple_fields_conflict_before_any_patch);
  RUN_TEST(edit_missing_expected_rejected);
  RUN_TEST(edit_unknown_expected_rejected);
  RUN_TEST(edit_optional_expected_null_and_zero);
  RUN_TEST(edit_invalid_expected_types_rejected);
  RUN_TEST(edit_conflict_invalidates_exact_write_preview);
  RUN_TEST(edit_allowlisted_spool_fields_and_no_nfc);
  RUN_TEST(edit_allowlisted_filament_and_selected_spool_readback);
  RUN_TEST(edit_rejects_unknown_spool_field);
  RUN_TEST(edit_rejects_unknown_filament_field);
  RUN_TEST(edit_rejects_invalid_numbers_types_and_strings);
  RUN_TEST(edit_rejects_malformed_canonical_record);
  RUN_TEST(edit_string_bounds_include_embedded_nul);
  RUN_TEST(edit_rejects_wrong_target_and_spool_relationship);
  RUN_TEST(edit_patch_failure_keeps_failure_state);
  RUN_TEST(edit_readback_mismatch_fails);
  RUN_TEST(edit_only_patches_changed_values);
  RUN_TEST(edit_invalidates_old_preview);
  RUN_TEST(edit_cannot_bypass_pending_association);
  RUN_TEST(blank_and_complete_verification);
  RUN_TEST(nonblank_rejected);
  RUN_TEST(geometry_rejected);
  RUN_TEST(multiple_rejected);
  RUN_TEST(locked_rejected);
  RUN_TEST(unstable_rejected);
  RUN_TEST(removed_before_write);
  RUN_TEST(replaced_before_write);
  RUN_TEST(generation_changed);
  RUN_TEST(system_changed);
  RUN_TEST(protection_changed);
  RUN_TEST(geometry_changed);
  RUN_TEST(write_failure_stops);
  RUN_TEST(block_mismatch_stops);
  RUN_TEST(final_mismatch_stops);
  RUN_TEST(removed_mid_write);
  RUN_TEST(bus_error_refuses);
  RUN_TEST(preserved_plan_refused);
  RUN_TEST(malformed_target_refused);
  RUN_TEST(no_change_rewrite);
  RUN_TEST(one_block_rewrite);
  RUN_TEST(aux_only_update);
  RUN_TEST(metadata_rewrite);
  RUN_TEST(mapped_fields);
  RUN_TEST(unknown_optional_fields);
  RUN_TEST(real_zero_preserved);
  RUN_TEST(uuid_validation);
  RUN_TEST(repeated_plans_release_workspace);
  RUN_TEST(canonical_association_readback);
  RUN_TEST(association_pending_retry_without_write);
  RUN_TEST(duplicate_uuid_refuses_before_write);
  RUN_TEST(confirmation_mismatch_refuses);
  RUN_TEST(catalog_pagination_and_search);
  RUN_TEST(catalog_malformed_and_timeout);
  RUN_TEST(community_contract_drift_rejected);
  RUN_TEST(community_missing_required_rejected);
  RUN_TEST(community_success_and_canonical_readback);
  RUN_TEST(community_duplicate_import_reuses);
  RUN_TEST(station_community_selection_uses_import_review);
  RUN_TEST(community_vendor_failure);
  RUN_TEST(community_filament_failure);
  RUN_TEST(community_canonical_failure);
  RUN_TEST(community_oversized_and_unknown_fields);
  RUN_TEST(same_spool_identity_retained);
  RUN_TEST(preview_psram_failure_has_no_write);
  RUN_TEST(repeated_catalog_releases_parser);
  RUN_TEST(recovery_after_reboot_associates_without_write);
  RUN_TEST(journal_does_not_recover_replacement_uid);
  RUN_TEST(repeated_writer_lifecycles_release_storage);
  RUN_TEST(settings_change_invalidates_confirmation);
  RUN_TEST(malformed_canonical_and_import_ids_fail_closed);
  RUN_TEST(spool_material_filter_uses_documented_parameter);
  RUN_TEST(journal_failure_prevents_write);
  RUN_TEST(partial_recovery_requires_known_blocks);
  RUN_TEST(diameter_current_key_and_legacy_compatibility);
  RUN_TEST(journal_valid_decode_partial_is_recovery);
  RUN_TEST(journal_exact_original_is_normal);
  RUN_TEST(journal_exact_target_is_classified_before_decode);
  RUN_TEST(journal_valid_decode_unauthorized_block_refused);
  RUN_TEST(journal_system_security_geometry_mismatch_refused);
  RUN_TEST(journal_survives_multiple_unconfirmed_previews);
  RUN_TEST(uid_unowned_and_target_owned);
  RUN_TEST(uid_repurpose_preview_discloses_and_binds_owner);
  RUN_TEST(uid_multiple_owners_refused);
  RUN_TEST(uid_move_clears_only_previous_uid_and_verifies);
  RUN_TEST(uid_previous_clear_failure_stops_target);
  RUN_TEST(uid_previous_clear_readback_failure_stops_target);
  RUN_TEST(uid_target_patch_failure_after_clear_is_retryable);
  RUN_TEST(uid_owner_changed_after_preview_is_not_cleared);
  RUN_TEST(uid_final_uniqueness_recheck_blocks_success);
  RUN_TEST(uid_move_recovery_after_reboot_without_write);
  RUN_TEST(uid_formatted_queries_and_cross_format_conflicts);
  return UNITY_END();
}
