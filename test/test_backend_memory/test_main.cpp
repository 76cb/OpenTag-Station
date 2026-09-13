#include <unity.h>
#include <cstdlib>
#include <string>
#include "network/backend_json.hpp"
#include "network/http_transport.hpp"
#include "network/tcp_connect.hpp"
#include "network/bounded_dns.hpp"
using namespace opentag::network;

namespace {
void* fail_allocate(void*, std::size_t) { return nullptr; }
void test_body_bounds_and_move_ownership() {
  ResponseBody body(8);
  TEST_ASSERT_TRUE(body.append("1234", 4));
  TEST_ASSERT_TRUE(body.append("5678", 4));
  TEST_ASSERT_FALSE(body.append("9", 1));
  TEST_ASSERT_TRUE(body.overflowed());
  ResponseBody moved(std::move(body));
  TEST_ASSERT_EQUAL_UINT(0, body.size());
  TEST_ASSERT_EQUAL_UINT(8, moved.size());
  TEST_ASSERT_EQUAL_STRING("12345678", moved.data());
}
void test_psram_failure_is_fallible() {
  ResponseBody body(65536, fail_allocate);
  TEST_ASSERT_FALSE(body.append("hello", 5));
  TEST_ASSERT_TRUE(body.failed());
  TEST_ASSERT_EQUAL_UINT(0, body.size());
  BackendJsonAllocator allocator(fail_allocate);
  JsonDocument document(&allocator);
  TEST_ASSERT_TRUE(deserializeJson(document, "{\"name\":\"value\"}") == DeserializationError::NoMemory);
  TEST_ASSERT_EQUAL_UINT(0, allocator.used());
}
void test_internal_workspace_admission() {
  TEST_ASSERT_FALSE(backend_admitted({17000, 10000}));
  TEST_ASSERT_FALSE(backend_admitted({24000, 4596}));
  TEST_ASSERT_TRUE(backend_admitted({24000, 9700}));
  TEST_ASSERT_FALSE(backend_admitted({24000, 9700}, true));
}
void test_connection_failures_have_distinct_actionable_messages() {
  TEST_ASSERT_NOT_NULL(std::strstr(connection_failure_message(ECONNREFUSED), "refused"));
  TEST_ASSERT_NOT_NULL(std::strstr(connection_failure_message(ENETUNREACH), "unreachable"));
  TEST_ASSERT_NOT_NULL(std::strstr(connection_failure_message(EHOSTUNREACH), "unreachable"));
  TEST_ASSERT_NOT_NULL(std::strstr(connection_failure_message(ETIMEDOUT), "timed out"));
  TEST_ASSERT_NOT_NULL(std::strstr(connection_failure_message(ENOMEM), "memory unavailable"));
  TEST_ASSERT_NOT_NULL(std::strstr(connection_failure_message(ENOBUFS), "memory unavailable"));
  TEST_ASSERT_NULL(connection_failure_message(0));
}
void test_real_tcp_connect_preserves_refused_error_and_deadline() {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, listener);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  TEST_ASSERT_EQUAL(0, ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)));
  socklen_t length = sizeof(address);
  TEST_ASSERT_EQUAL(0, ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length));
  // A bound but non-listening port is reserved by this fixture and refuses TCP.
  const auto refused = connect_tcp_ipv4(address.sin_addr.s_addr, ntohs(address.sin_port), 1000);
  TEST_ASSERT_EQUAL(-1, refused.socket);
  TEST_ASSERT_EQUAL(ECONNREFUSED, refused.error);
  TEST_ASSERT_EQUAL(0, ::listen(listener, 1));
  const auto connected = connect_tcp_ipv4(address.sin_addr.s_addr, ntohs(address.sin_port), 1000);
  TEST_ASSERT_GREATER_OR_EQUAL(0, connected.socket);
  TEST_ASSERT_EQUAL(0, connected.error);
  ::close(connected.socket);
  ::close(listener);
  const auto expired = connect_tcp_ipv4(address.sin_addr.s_addr, ntohs(address.sin_port), 0);
  TEST_ASSERT_EQUAL(-1, expired.socket);
  TEST_ASSERT_EQUAL(ETIMEDOUT, expired.error);
}
void test_late_dns_completion_cannot_overwrite_new_lookup() {
  DnsLookupState state;
  TEST_ASSERT_TRUE(state.begin("slow.example"));
  // The waiting operation times out while the resolver retains the callback.
  TEST_ASSERT_FALSE(state.begin("new.example"));
  TEST_ASSERT_EQUAL_STRING("slow.example", state.host);
  state.complete(42);
  TEST_ASSERT_TRUE(state.done());
  TEST_ASSERT_TRUE(state.begin("new.example"));
  TEST_ASSERT_FALSE(state.done());
  TEST_ASSERT_EQUAL_UINT(0, state.address);
  state.complete(84);
  TEST_ASSERT_TRUE(state.done());
  TEST_ASSERT_EQUAL_UINT(84, state.address);
}
void test_parser_is_bounded_and_releases_every_cycle() {
  for (int cycle = 0; cycle < 200; ++cycle) {
    {
      ResponseBody body(std::string("{\"spools\":[{\"id\":42}],\"ignored\":\"") + std::string(16000, 'a') + "\"}");
      auto parsed = parse_backend_json(body, "test");
      TEST_ASSERT_TRUE(parsed.ok());
      TEST_ASSERT_EQUAL_UINT(0, body.size());
      TEST_ASSERT_EQUAL(42, parsed.value()["spools"][0]["id"].as<int>());
      TEST_ASSERT_GREATER_THAN(0, backend_json_allocator.used());
    }
    TEST_ASSERT_EQUAL_UINT(0, backend_json_allocator.used());
  }
}
void test_malformed_truncated_and_deep_json_release_storage() {
  for (auto* text : {"{broken}", "{\"id\":", "[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]"}) {
    ResponseBody body(text);
    { auto parsed = parse_backend_json(body, "test"); TEST_ASSERT_FALSE(parsed.ok()); }
    TEST_ASSERT_EQUAL_UINT(0, body.size());
    TEST_ASSERT_EQUAL_UINT(0, backend_json_allocator.used());
  }
}
void test_parser_aggregate_limit_preserves_existing_allocations() {
  BackendJsonAllocator allocator;
  void* allocation = allocator.allocate(BackendJsonAllocator::maximum_bytes);
  TEST_ASSERT_NOT_NULL(allocation);
  TEST_ASSERT_NULL(allocator.allocate(1));
  TEST_ASSERT_NULL(allocator.reallocate(allocation, BackendJsonAllocator::maximum_bytes + 1));
  allocator.deallocate(allocation);
  TEST_ASSERT_EQUAL_UINT(0, allocator.used());
}
}
void setUp() {}
void tearDown() {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_body_bounds_and_move_ownership);
  RUN_TEST(test_psram_failure_is_fallible);
  RUN_TEST(test_internal_workspace_admission);
  RUN_TEST(test_connection_failures_have_distinct_actionable_messages);
  RUN_TEST(test_real_tcp_connect_preserves_refused_error_and_deadline);
  RUN_TEST(test_late_dns_completion_cannot_overwrite_new_lookup);
  RUN_TEST(test_parser_is_bounded_and_releases_every_cycle);
  RUN_TEST(test_malformed_truncated_and_deep_json_release_storage);
  RUN_TEST(test_parser_aggregate_limit_preserves_existing_allocations);
  return UNITY_END();
}
