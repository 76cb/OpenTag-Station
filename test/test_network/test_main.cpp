#include <unity.h>

#include <string>
#include <vector>

#include "core/saturating_counter.hpp"
#include "network/http_transport.hpp"
#include "network/network_policy.hpp"

using opentag::network::ExponentialReconnectBackoff;
using opentag::network::AsyncWifiScanState;
using opentag::network::NetworkConnectReceiptGate;
using opentag::network::ProvisioningPolicy;
using opentag::network::ProvisioningReason;
using opentag::network::WifiNetwork;
using opentag::network::WifiScanOutcome;
using opentag::network::normalize_scan_results;
using opentag::network::parse_http_url;

void setUp() {}
void tearDown() {}

void test_reconnect_backoff_doubles_saturates_and_resets() {
  ExponentialReconnectBackoff backoff(1000U, 5000U);
  TEST_ASSERT_EQUAL_UINT32(1000U, backoff.consume_delay());
  TEST_ASSERT_EQUAL_UINT32(2000U, backoff.consume_delay());
  TEST_ASSERT_EQUAL_UINT32(4000U, backoff.consume_delay());
  TEST_ASSERT_EQUAL_UINT32(5000U, backoff.consume_delay());
  TEST_ASSERT_EQUAL_UINT32(5000U, backoff.consume_delay());
  TEST_ASSERT_EQUAL_UINT32(5U, backoff.attempts());
  backoff.reset();
  TEST_ASSERT_EQUAL_UINT32(0U, backoff.attempts());
  TEST_ASSERT_EQUAL_UINT32(1000U, backoff.next_delay_ms());
}

void test_diagnostic_counters_saturate_instead_of_wrapping() {
  TEST_ASSERT_EQUAL_UINT32(
      UINT32_MAX,
      opentag::core::saturating_increment<std::uint32_t>(UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT8(
      UINT8_MAX,
      opentag::core::saturating_increment<std::uint8_t>(UINT8_MAX));
  TEST_ASSERT_EQUAL_UINT32(
      42U,
      opentag::core::saturating_increment<std::uint32_t>(41U));
}

void test_url_parser_applies_default_and_explicit_ports() {
  const auto http = parse_http_url("http://spoolman.local/api/v1/info");
  TEST_ASSERT_TRUE(http.ok());
  TEST_ASSERT_FALSE(http.value().secure);
  TEST_ASSERT_EQUAL_STRING("spoolman.local", http.value().host.c_str());
  TEST_ASSERT_EQUAL_UINT16(80U, http.value().port);
  TEST_ASSERT_EQUAL_STRING("/api/v1/info", http.value().path.c_str());

  const auto https = parse_http_url("https://example.com:8443?probe=1");
  TEST_ASSERT_TRUE(https.ok());
  TEST_ASSERT_TRUE(https.value().secure);
  TEST_ASSERT_EQUAL_UINT16(8443U, https.value().port);
  TEST_ASSERT_EQUAL_STRING("/?probe=1", https.value().path.c_str());
}

void test_url_parser_rejects_credentials_fragments_bad_ports_and_schemes() {
  TEST_ASSERT_FALSE(parse_http_url("ftp://example.com/file").ok());
  TEST_ASSERT_FALSE(parse_http_url("http://user:pass@example.com/").ok());
  TEST_ASSERT_FALSE(parse_http_url("http://example.com:0/").ok());
  TEST_ASSERT_FALSE(parse_http_url("http://example.com:999999999999/").ok());
  TEST_ASSERT_FALSE(parse_http_url("http://example.com/path#fragment").ok());
  TEST_ASSERT_FALSE(parse_http_url("http://bad host/path").ok());
}

void test_provisioning_policy_covers_first_boot_fallback_and_ap_grace() {
  ProvisioningPolicy policy;
  policy.initialize(false);
  TEST_ASSERT_TRUE(policy.active());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ProvisioningReason::unconfigured),
      static_cast<int>(policy.reason()));

  policy.begin_connection_attempt();
  TEST_ASSERT_TRUE(policy.active());
  policy.connected(1000U);
  TEST_ASSERT_TRUE(policy.active());
  TEST_ASSERT_TRUE(policy.grace_active());
  TEST_ASSERT_EQUAL_UINT32(30000U, policy.grace_remaining_ms(1000U));
  policy.poll(30999U);
  TEST_ASSERT_TRUE(policy.active());
  policy.poll(31000U);
  TEST_ASSERT_FALSE(policy.active());

  policy.initialize(true);
  policy.connected(32000U);
  TEST_ASSERT_FALSE(policy.active());
  policy.request_setup();
  TEST_ASSERT_TRUE(policy.active());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ProvisioningReason::requested),
      static_cast<int>(policy.reason()));

  policy.initialize(true);
  policy.connection_failed();
  policy.connection_failed();
  TEST_ASSERT_FALSE(policy.active());
  policy.connection_failed();
  TEST_ASSERT_TRUE(policy.active());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ProvisioningReason::connection_failures),
      static_cast<int>(policy.reason()));
  policy.begin_connection_attempt();
  policy.connection_failed();
  TEST_ASSERT_TRUE(policy.active());
}

void test_provisioning_grace_deadline_is_millis_wrap_safe() {
  ProvisioningPolicy policy;
  policy.initialize(false);
  policy.connected(UINT32_MAX - 10000U);
  policy.poll(19998U);
  TEST_ASSERT_TRUE(policy.active());
  policy.poll(19999U);
  TEST_ASSERT_FALSE(policy.active());
}

void test_scan_results_dedupe_strongest_sort_and_bound() {
  const std::vector<WifiNetwork> observed = {
      {"weak", -80, true},
      {"", -1, false},
      {"same", -70, false},
      {"strong", -20, true},
      {"same", -40, true},
  };
  const auto normalized = normalize_scan_results(observed, 3U);
  TEST_ASSERT_EQUAL_UINT(3U, normalized.size());
  TEST_ASSERT_EQUAL_STRING("strong", normalized[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("same", normalized[1].ssid.c_str());
  TEST_ASSERT_EQUAL_INT32(-40, normalized[1].rssi_dbm);
  TEST_ASSERT_TRUE(normalized[1].secured);
  TEST_ASSERT_EQUAL_STRING("weak", normalized[2].ssid.c_str());
}

void test_async_scan_tracks_start_completion_and_coalesced_request() {
  AsyncWifiScanState scan;
  scan.request();
  TEST_ASSERT_TRUE(scan.start_due());
  scan.request();
  auto transition = scan.accept_start_result(-1);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(WifiScanOutcome::running),
      static_cast<int>(transition.outcome));
  TEST_ASSERT_TRUE(scan.running());

  // A request arriving after the first request is consumed but before the
  // asynchronous start completes must survive as the one coalesced follow-up.
  scan.request();
  TEST_ASSERT_FALSE(scan.start_due());
  transition = scan.accept_poll_result(-1);
  TEST_ASSERT_TRUE(scan.running());
  transition = scan.accept_poll_result(7);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(WifiScanOutcome::complete),
      static_cast<int>(transition.outcome));
  TEST_ASSERT_EQUAL_UINT32(1U, scan.generation());
  TEST_ASSERT_TRUE(scan.start_due());
}

void test_async_scan_failure_preserves_actual_result_code() {
  AsyncWifiScanState scan;
  scan.request();
  TEST_ASSERT_TRUE(scan.start_due());
  const auto transition = scan.accept_start_result(-2);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(WifiScanOutcome::failed),
      static_cast<int>(transition.outcome));
  TEST_ASSERT_EQUAL_INT16(-2, transition.result_code);
  TEST_ASSERT_FALSE(scan.running());
  TEST_ASSERT_TRUE(scan.last_failure_code().has_value());
  TEST_ASSERT_EQUAL_INT16(-2, *scan.last_failure_code());
}

void test_connect_receipt_gate_blocks_reconfigure_until_exact_ack() {
  NetworkConnectReceiptGate gate;
  TEST_ASSERT_TRUE(gate.expect(41U));
  TEST_ASSERT_FALSE(gate.expect(42U));
  TEST_ASSERT_FALSE(gate.delivered(41U));
  TEST_ASSERT_FALSE(gate.acknowledge(42U));
  TEST_ASSERT_FALSE(gate.delivered(41U));
  TEST_ASSERT_TRUE(gate.acknowledge(41U));
  TEST_ASSERT_TRUE(gate.delivered(41U));
  TEST_ASSERT_FALSE(gate.expect(42U));
  TEST_ASSERT_TRUE(gate.delivered(41U));
  gate.clear(41U);
  TEST_ASSERT_FALSE(gate.delivered(41U));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reconnect_backoff_doubles_saturates_and_resets);
  RUN_TEST(test_url_parser_applies_default_and_explicit_ports);
  RUN_TEST(test_url_parser_rejects_credentials_fragments_bad_ports_and_schemes);
  RUN_TEST(test_diagnostic_counters_saturate_instead_of_wrapping);
  RUN_TEST(test_provisioning_policy_covers_first_boot_fallback_and_ap_grace);
  RUN_TEST(test_provisioning_grace_deadline_is_millis_wrap_safe);
  RUN_TEST(test_scan_results_dedupe_strongest_sort_and_bound);
  RUN_TEST(test_async_scan_tracks_start_completion_and_coalesced_request);
  RUN_TEST(test_async_scan_failure_preserves_actual_result_code);
  RUN_TEST(test_connect_receipt_gate_blocks_reconfigure_until_exact_ack);
  return UNITY_END();
}
