#include "services/tag_writer_service.hpp"
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
  R field_on() override { return R::success(); }
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
    else if (path == "/spool/12") {
      if (request.method == "PATCH") {
        if (fail_target)
          return core::Result<network::HttpResponse>::failure(
              {core::ErrorCategory::network, "target PATCH failed", true});
        network::BackendDocument patch;
        deserializeJson(patch, request.body);
        spool["extra"].set(patch["extra"]);
        ++patches;
      }
      result.set(spool);
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
        a.add<JsonObject>()["id"] = 12;
      if (multiple_uid || (final_uid_conflict && patches))
        a.add<JsonObject>()["id"] = 99;
    } else if (path.find("extra.opentag_instance_uuid") != std::string::npos) {
      auto a = result.to<JsonArray>();
      if (duplicate)
        a.add<JsonObject>()["id"] = 99;
      else if (!spool["extra"]["opentag_instance_uuid"].isNull())
        a.add<JsonObject>()["id"] = 12;
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
struct ServiceFixture {
  Reader reader;
  Http http;
  integrations::spoolman::SpoolmanAdapter adapter{http,
                                                  {"http://spoolman.test"}};
  network::BackendDocument view;
  services::TagWriterService service{
      adapter, reader, [this] { return reader.generation; },
      [](std::uint8_t *p, std::size_t n) { std::memset(p, 0x44, n); },
      [this](const auto &b) { deserializeJson(view, b.data(), b.size()); }};
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
  bool present = false, permit = true;
  int spool = 0;
  std::uint32_t backend = 0;
  bool load(nfc::WriterPlan &p, std::int32_t &id, std::uint32_t &b) override {
    if (!present)
      return false;
    p = saved;
    p.attempted = true;
    p.verified = false;
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
  void clear() override { present = false; }
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
                                       publish, &journal);
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
                                       publish, &journal);
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
} // namespace
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
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
