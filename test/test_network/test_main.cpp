#include <unity.h>

#include <string>

#include "network/http_transport.hpp"
#include "network/network_policy.hpp"

using opentag::network::ExponentialReconnectBackoff;
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reconnect_backoff_doubles_saturates_and_resets);
  RUN_TEST(test_url_parser_applies_default_and_explicit_ports);
  RUN_TEST(test_url_parser_rejects_credentials_fragments_bad_ports_and_schemes);
  return UNITY_END();
}
