#include <unity.h>

#include <algorithm>
#include <cstring>

#include "../fixtures/openprinttag_initializer_7e09cc3_312_aux32.hpp"
#include "nfc/presentation.hpp"
#include "nfc/read_only_service.hpp"
#include "nfc/worker_startup.hpp"
#include "web/nfc_json.hpp"

using namespace opentag;
using core::Result;
using nfc::nfcv::Uid;
namespace {
Uid known{{0xE0, 0x04, 0x01, 0x08, 0x66, 0x27, 0xD8, 0xD4}};
class Fake final : public nfc::IReadOnlyReader {
 public:
  std::vector<Uid> tags;
  std::vector<std::uint8_t> image;
  nfc::nfcv::TagGeometry shape{4, 80};
  unsigned reads = 0, offs = 0, ons = 0, inventories = 0;
  std::uint32_t time = 0;
  bool broken = false, unhealthy = false, truncated = false, fallback = false,
       changed = false, slow = false, init_failed = false, off_failed = false,
       inconsistent = false;
  Fake() {
    const auto* hex = openprinttag_initializer_7e09cc3_312_aux32_hex;
    for (std::size_t i = 0; i < std::strlen(hex); i += 2) {
      const auto digit = [](char c) {
        return c <= '9' ? c - '0' : c - 'a' + 10;
      };
      image.push_back((digit(hex[i]) << 4) | digit(hex[i + 1]));
    }
    image.resize(320, 0);
  }
  Result<void> initialize() override {
    return init_failed ? failure(false) : Result<void>::success();
  }
  Result<void> field_on() override {
    ++ons;
    return Result<void>::success();
  }
  Result<void> field_off() override {
    ++offs;
    return off_failed ? failure(false) : Result<void>::success();
  }
  Result<void> health() override {
    return unhealthy ? failure(false) : Result<void>::success();
  }
  Result<std::vector<Uid>> inventory() override {
    ++inventories;
    if (broken)
      return Result<std::vector<Uid>>::failure(
          {core::ErrorCategory::nfc_communication, "transport", false});
    return Result<std::vector<Uid>>::success(tags);
  }
  Result<nfc::nfcv::TagGeometry> geometry(const Uid&) override {
    return Result<nfc::nfcv::TagGeometry>::success(shape);
  }
  Result<void> read_blocks(const Uid&, std::size_t first, std::size_t count,
                           std::size_t size, std::uint8_t* out) override {
    ++reads;
    if (slow) time += 16000;
    if (changed) tags.clear();
    if (truncated) return failure(false);
    if (fallback && count > 1) return failure(true);
    if ((first + count) * size > image.size()) return failure(false);
    std::copy_n(image.data() + first * size, count * size, out);
    if (inconsistent && reads > 10) out[0] ^= 1U;
    return Result<void>::success();
  }
  std::uint32_t now_ms() const override { return time; }
  std::uint32_t bus_errors() const override {
    return broken || unhealthy ? 1 : 0;
  }
  Result<void> failure(bool retry) {
    return Result<void>::failure(
        {core::ErrorCategory::nfc_communication, "read failed", retry});
  }
};
void tick(Fake& f, nfc::ReadOnlyService& s, unsigned count = 1) {
  for (unsigned i = 0; i < count; ++i) {
    f.time += 500;
    s.poll();
  }
}
void blank_approved_tag_is_distinct_and_clears_on_removal(){Fake f;f.tags={known};std::fill_n(f.image.begin(),312,0);f.image[319]=0x92;nfc::ReadOnlyService service(f);tick(f,service,3);TEST_ASSERT_TRUE(service.snapshot().blank_compatible);TEST_ASSERT_FALSE(service.snapshot().tag);TEST_ASSERT_FALSE(service.snapshot().error);JsonDocument doc;web::write_nfc(doc.to<JsonObject>(),service.snapshot());TEST_ASSERT_TRUE(doc["blank_compatible"].as<bool>());f.tags.clear();tick(f,service);TEST_ASSERT_FALSE(service.snapshot().blank_compatible);}
void no_tag() {
  Fake f;
  nfc::ReadOnlyService s(f);
  tick(f, s, 8);
  TEST_ASSERT_FALSE(s.snapshot().present);
  TEST_ASSERT_EQUAL(0, f.reads);
  TEST_ASSERT_EQUAL(f.ons, f.offs);
}
void populated_tag() {
  Fake f;
  f.tags = {known};
  auto decoded = std::make_unique<nfc::openprinttag::DecodedTag>();
  TEST_ASSERT_TRUE(
      nfc::openprinttag::Codec::decode(core::ByteView(f.image), *decoded).ok());
  const std::uint8_t material[] = {0xA2, 0x0A, 0x64, 'T', 'e', 's', 't',
                                   0x0B, 0x65, 'B',  'r', 'a', 'n', 'd'};
  std::copy(std::begin(material), std::end(material),
            f.image.begin() + decoded->envelope.main.absolute_offset);
  nfc::ReadOnlyService service(f);
  tick(f, service, 3);
  TEST_ASSERT_NOT_NULL(service.snapshot().tag.get());
  TEST_ASSERT_EQUAL_STRING(
      "Test", service.snapshot().tag->decoded.material.material_name->c_str());
  JsonDocument doc;
  web::write_nfc(doc.to<JsonObject>(), service.snapshot(), 1250.0F);
  TEST_ASSERT_EQUAL_STRING("Brand", doc["brand_name"]);
  TEST_ASSERT_EQUAL_FLOAT(1250, doc["measured_weight"].as<float>());
}
void initialization_cleanup_and_consistency_errors() {
  Fake init;
  init.init_failed = true;
  nfc::ReadOnlyService first(init);
  tick(init, first);
  TEST_ASSERT_FALSE(first.snapshot().initialized);
  TEST_ASSERT_TRUE(first.snapshot().error.has_value());
  Fake off;
  off.off_failed = true;
  nfc::ReadOnlyService second(off);
  tick(off, second);
  TEST_ASSERT_TRUE(second.snapshot().error.has_value());
  TEST_ASSERT_EQUAL(off.ons, off.offs);
  Fake changed;
  changed.tags = {known};
  changed.inconsistent = true;
  nfc::ReadOnlyService third(changed);
  tick(changed, third, 3);
  TEST_ASSERT_NULL(third.snapshot().tag.get());
  TEST_ASSERT_TRUE(third.snapshot().error.has_value());
  TEST_ASSERT_EQUAL(changed.ons, changed.offs);
}
void empty_valid_tag_and_suppression() {
  Fake f;
  f.tags = {known};
  nfc::ReadOnlyService s(f);
  tick(f, s, 2);
  TEST_ASSERT_EQUAL(0, f.reads);
  tick(f, s);
  auto a = s.snapshot();
  TEST_ASSERT_NOT_NULL(a.tag.get());
  TEST_ASSERT_EQUAL_HEX32(0x9E639911, *a.checksum);
  TEST_ASSERT_FALSE(a.tag->decoded.material.material_name);
  TEST_ASSERT_EQUAL(276, a.tag->decoded.envelope.auxiliary->absolute_offset);
  const auto reads = f.reads;
  tick(f, s, 25);
  TEST_ASSERT_EQUAL(reads, f.reads);
  TEST_ASSERT_EQUAL(a.generation, s.snapshot().generation);
  TEST_ASSERT_EQUAL(f.ons, f.offs);
}
void removal_replacement() {
  Fake f;
  f.tags = {known};
  nfc::ReadOnlyService s(f);
  tick(f, s, 3);
  const auto g = s.snapshot().generation;
  f.tags.clear();
  tick(f, s);
  TEST_ASSERT_NULL(s.snapshot().tag.get());
  f.tags = {known};
  tick(f, s, 3);
  TEST_ASSERT_NOT_NULL(s.snapshot().tag.get());
  TEST_ASSERT_GREATER_THAN(g, s.snapshot().generation);
  Uid other = known;
  other.bytes[7]++;
  f.tags = {other};
  tick(f, s);
  TEST_ASSERT_NULL(s.snapshot().tag.get());
  tick(f, s, 2);
  TEST_ASSERT_TRUE(s.snapshot().tag->uid == other);
}
void multiple_and_bounce() {
  Fake f;
  f.tags = {known};
  nfc::ReadOnlyService s(f);
  tick(f, s);
  f.tags.clear();
  tick(f, s);
  f.tags = {known};
  tick(f, s, 2);
  TEST_ASSERT_EQUAL(0, f.reads);
  f.tags = {known, known};
  tick(f, s);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(nfc::ReadState::multiple),
                        static_cast<int>(s.snapshot().state));
  TEST_ASSERT_EQUAL(0, f.reads);
}
void invalid_format() {
  Fake f;
  f.tags = {known};
  f.image.assign(320, 0);
  nfc::ReadOnlyService s(f);
  tick(f, s, 3);
  TEST_ASSERT_TRUE(s.snapshot().present);
  TEST_ASSERT_TRUE(s.snapshot().blank_compatible);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(nfc::ReadState::blank),
                        static_cast<int>(s.snapshot().state));
  const auto reads = f.reads;
  tick(f, s, 10);
  TEST_ASSERT_EQUAL(reads, f.reads);
}
void geometry_error() {
  Fake f;
  f.tags = {known};
  f.shape = {32, 200};
  nfc::ReadOnlyService s(f);
  tick(f, s, 3);
  TEST_ASSERT_EQUAL(0, f.reads);
  TEST_ASSERT_TRUE(s.snapshot().error.has_value());
  TEST_ASSERT_EQUAL(f.ons, f.offs);
}
void truncated_error() {
  Fake f;
  f.tags = {known};
  f.truncated = true;
  nfc::ReadOnlyService s(f);
  tick(f, s, 3);
  TEST_ASSERT_EQUAL(1, f.reads);
  TEST_ASSERT_NULL(s.snapshot().tag.get());
  TEST_ASSERT_EQUAL(f.ons, f.offs);
}
void transport_not_absence() {
  Fake f;
  f.unhealthy = true;
  nfc::ReadOnlyService s(f);
  tick(f, s);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(nfc::ReadState::error),
                        static_cast<int>(s.snapshot().state));
  TEST_ASSERT_EQUAL(1, s.snapshot().bus_errors);
}
void fallback_and_changed_uid() {
  Fake f;
  f.tags = {known};
  f.fallback = true;
  nfc::ReadOnlyService s(f);
  tick(f, s, 3);
  TEST_ASSERT_NOT_NULL(s.snapshot().tag.get());
  TEST_ASSERT_GREATER_THAN(150, f.reads);
  Fake b;
  b.tags = {known};
  b.changed = true;
  nfc::ReadOnlyService t(b);
  tick(b, t, 3);
  TEST_ASSERT_NULL(t.snapshot().tag.get());
  TEST_ASSERT_EQUAL(b.ons, b.offs);
}
void deadline() {
  Fake f;
  f.tags = {known};
  f.slow = true;
  nfc::ReadOnlyService s(f);
  tick(f, s, 3);
  TEST_ASSERT_NULL(s.snapshot().tag.get());
  TEST_ASSERT_EQUAL(f.ons, f.offs);
}
void api_and_presentation() {
  Fake f;
  f.tags = {known};
  nfc::ReadOnlyService s(f);
  tick(f, s, 3);
  JsonDocument doc;
  web::write_nfc(doc.to<JsonObject>(), s.snapshot());
  TEST_ASSERT_EQUAL_STRING("openprinttag", doc["state"]);
  TEST_ASSERT_EQUAL_STRING("E0:04:01:08:66:27:D8:D4", doc["uid"]);
  TEST_ASSERT_EQUAL_STRING("9E639911", doc["checksum"]);
  TEST_ASSERT_TRUE(doc["material_name"].isNull());
  TEST_ASSERT_TRUE(doc["read_only"].as<bool>());
  TEST_ASSERT_TRUE(
      nfc::describe(s.snapshot()).find("OpenPrintTag recognized") !=
      std::string::npos);
  f.tags.clear();
  tick(f, s);
  web::write_nfc(doc.to<JsonObject>(), s.snapshot());
  TEST_ASSERT_TRUE(doc["uid"].isNull());
}
void provisioning_defers_without_hardware_failure() {
  nfc::WorkerStartup startup;
  nfc::ReadSnapshot status;
  startup.describe(status);
  JsonDocument doc;
  web::write_nfc(doc.to<JsonObject>(), status);
  TEST_ASSERT_EQUAL_STRING("deferred", doc["state"]);
  TEST_ASSERT_EQUAL_STRING("provisioning", doc["reason"]);
  TEST_ASSERT_TRUE(doc["enabled"].as<bool>());
  TEST_ASSERT_FALSE(doc["available"].as<bool>());
  TEST_ASSERT_TRUE(doc["last_error"].isNull());
  TEST_ASSERT_TRUE(nfc::describe(status).find("NFC deferred: provisioning") != std::string::npos);
  startup.enable_when_configured(false, true, true, false, false);
  startup.enable_when_configured(true, false, true, false, false);
  startup.enable_when_configured(true, true, false, false, false);
  startup.enable_when_configured(true, true, true, true, false);
  startup.enable_when_configured(true, true, true, false, true);
  TEST_ASSERT_FALSE(startup.enabled());
  startup.enable_when_configured(true, true, true, false, false);
  TEST_ASSERT_TRUE(startup.enabled());
  // Later disconnect/AP recovery retains the same logical owner.
  startup.enable_when_configured(true, true, false, true, true);
  TEST_ASSERT_TRUE(startup.enabled());
  status = {};
  startup.describe(status);
  web::write_nfc(doc.to<JsonObject>(), status);
  TEST_ASSERT_EQUAL_STRING("initializing", doc["state"]);
  TEST_ASSERT_TRUE(doc["reason"].isNull());
}
void enabling_does_not_poll_hardware() {
  nfc::WorkerStartup startup;
  Fake f;
  f.tags = {known};
  nfc::ReadOnlyService service(f);
  for (unsigned i = 0; i < 100; ++i)
    startup.enable_when_configured(true, true, true, false, false);
  TEST_ASSERT_EQUAL(0, f.reads);
  TEST_ASSERT_FALSE(service.snapshot().initialized);
  tick(f, service, 3);  // Only the owner explicitly polling can start RF/read.
  TEST_ASSERT_NOT_NULL(service.snapshot().tag.get());
  TEST_ASSERT_EQUAL(1, service.snapshot().generation);
  tick(f, service, 5);
  TEST_ASSERT_EQUAL(1, service.snapshot().generation);
}
void read_storage_lifetime() {
  struct Tracked {
    unsigned* destroyed{nullptr};
    ~Tracked() { if (destroyed) ++*destroyed; }
  };
  unsigned destroyed = 0;
  auto value = nfc::make_read_storage<Tracked>();
  TEST_ASSERT_NOT_NULL(value.get());
  value->destroyed = &destroyed;
  auto held = value;
  value.reset();
  TEST_ASSERT_EQUAL(0, destroyed);
  held.reset();
  TEST_ASSERT_EQUAL(1, destroyed);
  nfc::ReadImage image(nfc::ReadOnlyService::maximum_memory_bytes);
  TEST_ASSERT_NOT_NULL(image.data());
  TEST_ASSERT_EQUAL(4096, image.size());
  std::fill_n(image.data(), image.size(), 0xA5);
  TEST_ASSERT_EQUAL_HEX8(0xA5, image.data()[4095]);
}
}  // namespace
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(blank_approved_tag_is_distinct_and_clears_on_removal);
  RUN_TEST(no_tag);
  RUN_TEST(populated_tag);
  RUN_TEST(initialization_cleanup_and_consistency_errors);
  RUN_TEST(empty_valid_tag_and_suppression);
  RUN_TEST(removal_replacement);
  RUN_TEST(multiple_and_bounce);
  RUN_TEST(invalid_format);
  RUN_TEST(geometry_error);
  RUN_TEST(truncated_error);
  RUN_TEST(transport_not_absence);
  RUN_TEST(fallback_and_changed_uid);
  RUN_TEST(deadline);
  RUN_TEST(api_and_presentation);
  RUN_TEST(provisioning_defers_without_hardware_failure);
  RUN_TEST(enabling_does_not_poll_hardware);
  RUN_TEST(read_storage_lifetime);
  return UNITY_END();
}
