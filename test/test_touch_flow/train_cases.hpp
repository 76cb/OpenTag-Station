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

// UI-5: lifting a tag off and setting a different one down restarts the flow
// exactly as swapping them between two reads does. The same tag coming back,
// or the reader merely being empty, does not.
inline void different_tag_after_removal_restarts_the_flow() {
  const auto started=[](TagFlow& f) {
    f.observe_tag("E004000000000001",nullptr);f.lifecycle=services::TagLifecycle::linked;f.current_spool=5;
    f.act(TagAction::sources);TEST_ASSERT_TRUE(f.page==TagPage::sources);TEST_ASSERT_EQUAL(5,f.from_spool);
  };
  TagFlow swapped;started(swapped);swapped.observe_tag("E004000000000002",nullptr);
  TEST_ASSERT_TRUE(swapped.page==TagPage::tag);TEST_ASSERT_EQUAL(0,swapped.from_spool);
  TagFlow f;started(f);
  f.observe_tag("",nullptr);TEST_ASSERT_TRUE(f.page==TagPage::sources);TEST_ASSERT_EQUAL(5,f.from_spool);
  f.observe_tag("E004000000000001",nullptr);TEST_ASSERT_TRUE(f.page==TagPage::sources);TEST_ASSERT_EQUAL(5,f.from_spool);
  f.observe_tag("",nullptr);f.observe_tag("E004000000000002",nullptr);
  TEST_ASSERT_TRUE(f.page==swapped.page);TEST_ASSERT_EQUAL(swapped.from_spool,f.from_spool);
  TEST_ASSERT_EQUAL_STRING(swapped.phase.c_str(),f.phase.c_str());TEST_ASSERT_TRUE(f.selected.isNull());
  TEST_ASSERT_EQUAL_STRING("E004000000000002",f.uid.c_str());
  // A request in flight is still never interrupted by the reader.
  TagFlow busy;started(busy);busy.begin_wait(3,0);busy.observe_tag("",nullptr);busy.observe_tag("E004000000000002",nullptr);
  TEST_ASSERT_TRUE(busy.page==TagPage::progress);TEST_ASSERT_EQUAL(5,busy.from_spool);
}

// UI-3: work started from the browser must not strand the touchscreen. An
// edit there does not move the touch flow at all, and any other progress the
// flow is not itself waiting for offers a way out - except while the tag is
// being changed.
inline void progress_from_another_client_is_never_a_dead_end() {
  TagFlow f;f.observe_tag("E004000000000001",nullptr);f.lifecycle=services::TagLifecycle::linked;f.current_spool=5;
  f.act(TagAction::sources);
  feed(f,R"({"phase":"editing","message":"Saving canonical Spoolman data; a fresh tag preview is required"})");
  TEST_ASSERT_TRUE(f.page==TagPage::sources);TEST_ASSERT_EQUAL_STRING("",f.phase.c_str());
  feed(f,R"({"phase":"updated","message":"Saved and verified in Spoolman. Generate a new tag preview."})");
  TEST_ASSERT_TRUE(f.page==TagPage::sources);TEST_ASSERT_EQUAL_STRING("",f.phase.c_str());
  TEST_ASSERT_TRUE(shows(f.screen(),TagAction::spools));
  for(const char* snapshot:{R"({"phase":"loading_spool"})",R"({"phase":"reading"})",R"({"phase":"a_phase_added_later"})"}) {
    TagFlow g;feed(g,snapshot);
    TEST_ASSERT_TRUE(g.page==TagPage::progress);TEST_ASSERT_FALSE(g.waiting);
    TEST_ASSERT_TRUE_MESSAGE(shows(g.screen(),TagAction::home),snapshot);
    TEST_ASSERT_FALSE(mentions(g.screen(),"longer than usual"));
  }
  for(const char* phase:{"validating","writing","clearing","verifying","decoding"}) {
    TagFlow g;g.page=TagPage::progress;g.phase=phase;
    TEST_ASSERT_EQUAL_MESSAGE(0,g.screen().count,phase);
  }
  // The flow's own request still shows no way out until it stalls.
  TagFlow own;own.begin_wait(4,0);feed(own,R"({"phase":"loading_spool","operation_id":4})");
  TEST_ASSERT_TRUE(own.waiting);TEST_ASSERT_EQUAL(0,own.screen().count);
}

// UI-9: a PREV that never got its page must not make a later, unrelated list
// open on its last rows, and a mistyped weight must not throw away the
// create form.
inline void failed_paging_and_create_mistakes_leave_no_trace() {
  const char* eight=R"({"phase":"catalog","items":[{"id":1},{"id":2},{"id":3},{"id":4},{"id":5},{"id":6},{"id":7},{"id":8}],"has_more":true})";
  const auto on_second_page=[&](TagFlow& f) {
    f.act(TagAction::spools);feed(f,eight);
    f.act(TagAction::next);f.act(TagAction::next);TEST_ASSERT_FALSE(f.act(TagAction::next).empty());
    feed(f,R"({"phase":"catalog","items":[{"id":9}],"has_more":false})");TEST_ASSERT_EQUAL(8,f.offset);
    TEST_ASSERT_FALSE(f.act(TagAction::previous).empty());
  };
  // PREV still lands on the last rows of the previous page when it arrives.
  TagFlow ok;on_second_page(ok);feed(ok,eight);TEST_ASSERT_EQUAL(6,ok.row);
  // PREV fails; the next list is a new browse.
  TagFlow f;on_second_page(f);f.fail("Spoolman is unavailable");
  f.act(TagAction::back);f.act(TagAction::sources);f.act(TagAction::filaments);feed(f,eight);
  TEST_ASSERT_EQUAL(0,f.row);
  // PREV is abandoned after a stall; the next list is a search.
  TagFlow g;on_second_page(g);g.begin_wait(5,0);g.abandon_wait();
  TEST_ASSERT_FALSE(g.search("PLA").empty());feed(g,eight);
  TEST_ASSERT_EQUAL(0,g.row);

  TagFlow c;c.act(TagAction::filaments);
  feed(c,R"({"phase":"catalog","items":[{"id":12,"weight":1000,"spool_weight":130,"name":"PLA"}]})");
  c.act(TagAction::row0);c.act(TagAction::use);TEST_ASSERT_TRUE(c.page==TagPage::create);
  c.remaining=1200;c.tare=180;  // typed by the user; remaining exceeds initial
  TEST_ASSERT_TRUE(c.act(TagAction::create).empty());
  TEST_ASSERT_TRUE(c.page==TagPage::create);
  TEST_ASSERT_TRUE(mentions(c.screen(),"Check the spool weights"));
  TEST_ASSERT_EQUAL_DOUBLE(1200,c.remaining);TEST_ASSERT_EQUAL_DOUBLE(180,c.tare);
  TEST_ASSERT_TRUE(shows(c.screen(),TagAction::remaining));
  // Corrected: the request goes out and the hint is gone.
  c.remaining=800;
  JsonDocument command;deserializeJson(command,c.act(TagAction::create));
  TEST_ASSERT_EQUAL_STRING("create_spool",command["action"]|"");TEST_ASSERT_EQUAL_DOUBLE(180,command["spool"]["spool_weight"].as<double>());
  TEST_ASSERT_TRUE(c.create_hint.empty());
}

}  // namespace train
