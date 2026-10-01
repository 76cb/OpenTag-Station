#pragma once
// Deep-refactor train regression cases for the WT32 touch model: the wait,
// leave and tag-observation rules UiService delegates to TagFlow. Included by
// test_main.cpp after rc10_cases.hpp.
namespace train {
using namespace opentag;
using namespace opentag::ui;
using rc10::feed;
using rc10::shows;
inline bool mentions(const TagScreen& s,const char* text){return s.body.find(text)!=std::string::npos;}

// UI-4: a codec error for the tag on the reader must not overwrite the
// instructions for finishing an interrupted write or clear.
inline void nfc_error_does_not_replace_recovery_instructions() {
  const std::string error="Tag data could not be decoded";
  TagFlow f;feed(f,R"({"phase":"write_recovery","message":"Place the same tag on the reader to finish writing it.","spool_id":31})");
  f.observe_tag("E004000000000001",&error);f.lifecycle=services::TagLifecycle::write_pending;
  TEST_ASSERT_TRUE(mentions(f.screen(),"Place the same tag"));
  TEST_ASSERT_FALSE(mentions(f.screen(),"decoded"));
  // The unsupported-tag screen still explains why, and only while it applies.
  TagFlow g;g.observe_tag("E004000000000001",&error);g.lifecycle=services::TagLifecycle::unsupported;
  TEST_ASSERT_TRUE(mentions(g.screen(),"decoded"));
  g.observe_tag("E004000000000001",nullptr);
  TEST_ASSERT_FALSE(mentions(g.screen(),"decoded"));
}

// UI-1: an unfinished write or clear stays on offer until the writer reports
// it resolved. DONE, a retry that fails without a new snapshot, a stalled
// retry and BACK from the retry's review all return to the same prompt.
inline void recovery_prompt_survives_leaving_and_failed_retries() {
  TagFlow f;f.observe_tag("E004000000000001",nullptr);
  feed(f,R"({"phase":"write_recovery","message":"Place the same tag on the reader to finish writing it.","spool_id":31})");
  const auto offered=[&]{const auto s=f.screen();return f.page==TagPage::tag&&shows(s,TagAction::retry)&&shows(s,TagAction::skip)&&mentions(s,"Place the same tag");};
  const auto retry=[&](std::uint64_t operation) {
    JsonDocument c;deserializeJson(c,f.act(TagAction::retry));
    TEST_ASSERT_EQUAL_STRING("preview",c["action"]|"");TEST_ASSERT_EQUAL(31,c["spool_id"].as<int>());
    f.begin_wait(operation,1000);
  };
  TEST_ASSERT_TRUE(offered());
  // DONE, then Manage tag again: the unchanged snapshot is not consumed twice.
  f.leave_page();TEST_ASSERT_TRUE(offered());
  // The retry is accepted but fails before the writer publishes anything.
  retry(7);f.operation_failed("Writer recovery unavailable; see status before continuing");
  TEST_ASSERT_TRUE(f.page==TagPage::error);TEST_ASSERT_TRUE(mentions(f.screen(),"Writer recovery unavailable"));
  f.act(TagAction::back);TEST_ASSERT_TRUE(offered());
  // The station refuses the retry (busy): nothing was queued.
  TEST_ASSERT_FALSE(f.act(TagAction::retry).empty());f.fail("Station is busy. Please try again.");
  f.act(TagAction::back);TEST_ASSERT_TRUE(offered());
  // The retry stalls and the user takes BACK TO HOME.
  retry(8);f.abandon_wait();f.leave_page();TEST_ASSERT_TRUE(offered());
  // The writer publishes a failure for the retry.
  retry(9);feed(f,R"({"phase":"failed","operation_id":9,"message":"A different tag is on the reader."})");
  TEST_ASSERT_TRUE(f.page==TagPage::error);TEST_ASSERT_TRUE(mentions(f.screen(),"different tag"));
  f.act(TagAction::back);TEST_ASSERT_TRUE(offered());
  // The retry reaches its review and the user backs out of it.
  retry(10);feed(f,R"({"phase":"preview","operation_id":10,"uid":"E004000000000001","spool_id":31})");
  TEST_ASSERT_TRUE(f.page==TagPage::review);f.act(TagAction::back);TEST_ASSERT_TRUE(offered());
  // Only the writer reporting the work finished removes the prompt.
  feed(f,R"({"phase":"complete","operation_id":11,"uid":"E004000000000001","spool_id":31})");
  f.leave_page();TEST_ASSERT_FALSE(shows(f.screen(),TagAction::retry));TEST_ASSERT_FALSE(shows(f.screen(),TagAction::skip));
}

}  // namespace train
