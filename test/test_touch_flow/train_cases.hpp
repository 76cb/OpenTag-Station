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

}  // namespace train
