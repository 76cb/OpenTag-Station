#include "config/configuration_service.hpp"
#include "services/weigh_sync.hpp"
#include <unity.h>
using namespace opentag;
struct Inventory : integrations::ISpoolInventory {
  domain::Spool spool;
  int patches = 0;
  bool enabled = true, mismatch = false;
  std::function<void()> after_get;
  Inventory() {
    spool.id = 28;
    spool.used_grams = 100;
    spool.remaining_grams = 900;
  }
  core::Result<std::vector<domain::Spool>> list_spools() override {
    return core::Result<std::vector<domain::Spool>>::success({spool});
  }
  core::Result<std::vector<domain::Spool>>
  find_spools(const integrations::SpoolFilter &) override {
    return list_spools();
  }
  core::Result<domain::Spool> get_spool(domain::SpoolId) override {
    return core::Result<domain::Spool>::success(spool);
  }
  core::Result<domain::Spool>
  create_spool(const integrations::CreateSpoolRequest &) override {
    return get_spool(28);
  }
  core::Result<std::vector<std::string>> list_locations() override {
    return core::Result<std::vector<std::string>>::success({});
  }
  core::Result<std::vector<integrations::ExtraFieldDefinition>>
  list_extra_fields() override {
    return core::Result<
        std::vector<integrations::ExtraFieldDefinition>>::success({});
  }
  integrations::BackendCapabilities capabilities() const override {
    integrations::BackendCapabilities c;
    if (enabled)
      c.add(integrations::BackendCapability::update_remaining_weight);
    return c;
  }
  core::Result<domain::Spool> set_remaining_weight(
      domain::SpoolId,
      const integrations::RemainingWeightUpdate &update) override {
    if (after_get)
      after_get();
    if (spool.used_grams != update.expected_used_grams ||
        (update.before_mutation && !update.before_mutation()))
      return core::Result<domain::Spool>::failure(
          {core::ErrorCategory::conflict, "Concurrent change", false});
    ++patches;
    if (mismatch)
      return core::Result<domain::Spool>::failure(
          {core::ErrorCategory::invalid_response, "Readback mismatch", false});
    spool.used_grams = 1000 - update.remaining_grams;
    spool.remaining_grams = update.remaining_grams;
    return get_spool(28);
  }
};
struct Fixture {
  services::WeighSync sync;
  Inventory inventory;
  bool same = true;
  services::WeighSyncSnapshot initial() {
    services::WeighSyncSnapshot s;
    s.measurement_id = 1;
    s.generation = 3;
    s.spool_id = 28;
    s.expected_used = 100;
    s.canonical_remaining = 900;
    s.tare = 130;
    return s;
  }
  void capture(bool automatic = false) {
    auto s = initial();
    s.automatic = automatic;
    TEST_ASSERT_TRUE(sync.begin(s));
    sync.capture(1, {842, true});
  }
  core::Result<void> update(std::uint64_t id = 1) {
    return sync.update(id, inventory, [&](const auto &) { return same; });
  }
};
void default_off_and_explicit_capture_no_mutation() {
  TEST_ASSERT_FALSE(
      config::Configuration{}.reconciliation.auto_update_after_weigh);
  Fixture f;
  f.capture();
  TEST_ASSERT_FALSE(f.sync.snapshot().automatic);
  TEST_ASSERT_EQUAL(0, f.inventory.patches);
  TEST_ASSERT_EQUAL_STRING("ready", f.sync.snapshot().phase.c_str());
}
void manual_update_verified() {
  Fixture f;
  f.capture();
  TEST_ASSERT_TRUE(f.update().ok());
  TEST_ASSERT_EQUAL(1, f.inventory.patches);
  TEST_ASSERT_EQUAL_FLOAT(712, *f.sync.snapshot().canonical_remaining);
}
void automatic_update_once() {
  Fixture f;
  f.capture(true);
  TEST_ASSERT_TRUE(f.sync.snapshot().automatic);
  TEST_ASSERT_TRUE(f.update().ok());
  for (int i = 0; i < 8; ++i) {
    f.sync.capture(1, {820, true});
    TEST_ASSERT_FALSE(f.update().ok());
  }
  TEST_ASSERT_EQUAL(1, f.inventory.patches);
}
void new_weigh_can_update_again() {
  Fixture f;
  f.capture(true);
  TEST_ASSERT_TRUE(f.update().ok());
  auto s = f.initial();
  s.measurement_id = 2;
  s.expected_used = f.inventory.spool.used_grams;
  s.canonical_remaining = f.inventory.spool.remaining_grams;
  TEST_ASSERT_TRUE(f.sync.begin(s));
  f.sync.capture(2, {800, true});
  TEST_ASSERT_TRUE(f.update(2).ok());
  TEST_ASSERT_EQUAL(2, f.inventory.patches);
}
void unstable_timeout_never_updates() {
  for (int fault = 0; fault < 2; ++fault) {
    Fixture f;
    TEST_ASSERT_TRUE(f.sync.begin(f.initial()));
    if (fault)
      f.sync.fail(1, "Timeout");
    else
      f.sync.capture(1, {842, false});
    TEST_ASSERT_FALSE(f.update().ok());
    TEST_ASSERT_EQUAL(0, f.inventory.patches);
  }
}
void unresolved_ambiguous_missing_tare_never_updates() {
  for (int fault = 0; fault < 3; ++fault) {
    Fixture f;
    auto s = f.initial();
    if (fault < 2)
      s.spool_id = 0;
    else
      s.tare.reset();
    TEST_ASSERT_TRUE(f.sync.begin(s));
    f.sync.capture(1, {842, true});
    TEST_ASSERT_FALSE(f.update().ok());
    TEST_ASSERT_EQUAL(0, f.inventory.patches);
  }
}
void concurrent_usage_refuses_and_refreshes() {
  Fixture f;
  f.capture(true);
  f.inventory.spool.used_grams = 115;
  f.inventory.spool.remaining_grams = 885;
  TEST_ASSERT_FALSE(f.update().ok());
  TEST_ASSERT_EQUAL(0, f.inventory.patches);
  TEST_ASSERT_EQUAL_STRING("conflict", f.sync.snapshot().phase.c_str());
  TEST_ASSERT_EQUAL_FLOAT(885, *f.sync.snapshot().canonical_remaining);
}
void mismatch_not_reported_success() {
  Fixture f;
  f.capture();
  f.inventory.mismatch = true;
  TEST_ASSERT_FALSE(f.update().ok());
  TEST_ASSERT_EQUAL_STRING("failed", f.sync.snapshot().phase.c_str());
  TEST_ASSERT_FALSE(f.update().ok());
  TEST_ASSERT_EQUAL(1, f.inventory.patches);
}
void tolerance_no_patch() {
  Fixture f;
  auto s = f.initial();
  TEST_ASSERT_TRUE(f.sync.begin(s));
  f.sync.capture(1, {1028, true});
  TEST_ASSERT_TRUE(f.update().ok());
  TEST_ASSERT_EQUAL(0, f.inventory.patches);
  TEST_ASSERT_EQUAL_STRING("unchanged", f.sync.snapshot().phase.c_str());
}
void replaced_tag_refuses_before_and_during_operation() {
  for (int during = 0; during < 2; ++during) {
    Fixture f;
    f.capture();
    if (during)
      f.inventory.after_get = [&] { f.same = false; };
    else
      f.same = false;
    TEST_ASSERT_FALSE(f.update().ok());
    TEST_ASSERT_EQUAL(0, f.inventory.patches);
  }
}
void mutation_capability_required() {
  Fixture f;
  f.capture();
  f.inventory.enabled = false;
  TEST_ASSERT_FALSE(f.update().ok());
  TEST_ASSERT_EQUAL(0, f.inventory.patches);
}
void invalid_and_stale_measurements_refused() {
  Fixture f;
  TEST_ASSERT_TRUE(f.sync.begin(f.initial()));
  f.sync.capture(99, {842, true});
  TEST_ASSERT_FALSE(f.update().ok());
  f.sync.capture(1, {120, true});
  TEST_ASSERT_FALSE(f.update().ok());
  TEST_ASSERT_EQUAL(0, f.inventory.patches);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(default_off_and_explicit_capture_no_mutation);
  RUN_TEST(manual_update_verified);
  RUN_TEST(automatic_update_once);
  RUN_TEST(new_weigh_can_update_again);
  RUN_TEST(unstable_timeout_never_updates);
  RUN_TEST(unresolved_ambiguous_missing_tare_never_updates);
  RUN_TEST(concurrent_usage_refuses_and_refreshes);
  RUN_TEST(mismatch_not_reported_success);
  RUN_TEST(tolerance_no_patch);
  RUN_TEST(replaced_tag_refuses_before_and_during_operation);
  RUN_TEST(mutation_capability_required);
  RUN_TEST(invalid_and_stale_measurements_refused);
  return UNITY_END();
}
