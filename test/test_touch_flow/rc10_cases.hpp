#pragma once
// rc.10 regression cases for the WT32 touch model. Included by test_main.cpp.
#include "ui/assignment_dialog.hpp"
namespace rc10 {
using namespace opentag;
using namespace opentag::ui;

inline void assignment_dialog_only_affirmative_labels_confirm() {
  namespace d = assignment_dialog;
  TEST_ASSERT_TRUE(d::is_confirmation("Assign"));
  TEST_ASSERT_TRUE(d::is_confirmation("Replace"));
  TEST_ASSERT_TRUE(d::is_confirmation(d::override_label));
  TEST_ASSERT_TRUE(d::is_confirmation(d::override_replace_label));
  TEST_ASSERT_FALSE(d::is_confirmation("Cancel"));
  TEST_ASSERT_FALSE(d::is_confirmation("Back"));  // rc.9 regression: Back assigned
  TEST_ASSERT_FALSE(d::is_confirmation(""));
  TEST_ASSERT_FALSE(d::is_confirmation(nullptr));
  TEST_ASSERT_FALSE(d::is_confirmation("assign"));
}

inline bool shows(const TagScreen& s,TagAction a){for(std::size_t i=0;i<s.count;++i)if(s.buttons[i].action==a)return true;return false;}
inline const TagButton* find_button(const TagScreen& s,TagAction a){for(std::size_t i=0;i<s.count;++i)if(s.buttons[i].action==a)return &s.buttons[i];return nullptr;}
inline void feed(TagFlow& f,const char* json){network::ResponseBody b(json);TEST_ASSERT_TRUE(f.consume(b));}

inline void progress_never_shows_blocks_for_spoolman_work() {
  TagFlow f;f.act(TagAction::spools);  // queues a catalog search
  f.waiting=true;f.page=TagPage::progress;f.phase="queued";
  auto s=f.screen();
  TEST_ASSERT_EQUAL(std::string::npos,s.body.find("blocks"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("Searching Spoolman"));
  feed(f,R"({"phase":"associating","message":"x"})");
  s=f.screen();TEST_ASSERT_EQUAL(std::string::npos,s.body.find("blocks"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("already written"));
  feed(f,R"({"phase":"writing","completed_blocks":4,"total_blocks":20})");
  s=f.screen();TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("4 / 20 blocks"));
  // No escape is ever offered while the tag itself is changing.
  f.stalled=true;TEST_ASSERT_FALSE(shows(f.screen(),TagAction::home));
  feed(f,R"({"phase":"loading_spool"})");f.stalled=true;TEST_ASSERT_TRUE(shows(f.screen(),TagAction::home));
}
inline void filament_review_formats_diameter() {
  TagFlow f;f.entity="filament";f.page=TagPage::selected;
  f.selected["name"]="PLA";f.selected["material"]="PLA";f.selected["diameter"]=1.75;
  const auto s=f.screen();
  TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("1.75 mm"));
  TEST_ASSERT_EQUAL(std::string::npos,s.body.find("1.750000"));
}
inline void unknown_empty_spool_weight_must_be_entered() {
  TagFlow f;f.act(TagAction::filaments);
  feed(f,R"({"phase":"catalog","items":[{"id":12,"weight":1000,"name":"PLA"}]})");
  f.act(TagAction::row0);f.act(TagAction::use);
  TEST_ASSERT_TRUE(f.page==TagPage::create);TEST_ASSERT_FALSE(f.tare_known);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,f.screen().buttons[2].text.find("NOT SET"));
  TEST_ASSERT_TRUE(f.act(TagAction::create).empty());TEST_ASSERT_TRUE(f.page==TagPage::error);
  // Vendor empty weight is used when the filament has none.
  TagFlow g;g.act(TagAction::filaments);
  feed(g,R"({"phase":"catalog","items":[{"id":12,"weight":1000,"name":"PLA","vendor":{"name":"Acme","empty_spool_weight":215}}]})");
  g.act(TagAction::row0);g.act(TagAction::use);
  TEST_ASSERT_TRUE(g.tare_known);TEST_ASSERT_EQUAL_FLOAT(215,g.tare);
}
inline void vendor_search_is_kept_while_paging() {
  TagFlow f;f.act(TagAction::filaments);f.query="Polymaker";
  feed(f,R"({"phase":"catalog","search_field":"vendor","items":[{"id":1},{"id":2},{"id":3},{"id":4},{"id":5},{"id":6},{"id":7},{"id":8}],"has_more":true})");
  f.act(TagAction::next);f.act(TagAction::next);
  JsonDocument c;deserializeJson(c,f.act(TagAction::next));
  TEST_ASSERT_EQUAL(8,c["offset"].as<int>());TEST_ASSERT_EQUAL_STRING("vendor",c["search_field"]|"");
  // Paging back lands on the last rows of the previous page.
  feed(f,R"({"phase":"catalog","search_field":"vendor","items":[{"id":9}],"has_more":false})");
  deserializeJson(c,f.act(TagAction::previous));TEST_ASSERT_EQUAL(0,c["offset"].as<int>());
  feed(f,R"({"phase":"catalog","search_field":"vendor","items":[{"id":1},{"id":2},{"id":3},{"id":4},{"id":5},{"id":6},{"id":7},{"id":8}],"has_more":true})");
  TEST_ASSERT_EQUAL(6,f.row);
}
inline void destructive_and_duplicate_actions() {
  TagFlow f;f.lifecycle=services::TagLifecycle::unlinked;auto s=f.screen();
  std::size_t links=0;for(std::size_t i=0;i<s.count;++i)if(s.buttons[i].action==TagAction::sources)++links;
  TEST_ASSERT_EQUAL(1,links);
  TEST_ASSERT_TRUE(find_button(s,TagAction::clear)->destructive);
  f.page=TagPage::review;f.phase="clear_preview";s=f.screen();
  TEST_ASSERT_TRUE(find_button(s,TagAction::write)->destructive);
  f.phase="preview";f.view["semantic_no_change"]=true;s=f.screen();
  TEST_ASSERT_FALSE(find_button(s,TagAction::write)->enabled);
  TEST_ASSERT_TRUE(f.act(TagAction::write).empty());
}
inline void recovery_can_be_skipped_explicitly() {
  TagFlow f;feed(f,R"({"phase":"association_pending","message":"The tag is written. Saving the link in Spoolman failed: offline"})");
  auto s=f.screen();
  TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("offline"));
  TEST_ASSERT_TRUE(find_button(s,TagAction::skip)->destructive);
  TEST_ASSERT_TRUE(f.act(TagAction::skip).empty());TEST_ASSERT_TRUE(f.page==TagPage::skip_review);
  f.act(TagAction::back);TEST_ASSERT_TRUE(f.page==TagPage::tag);
  f.act(TagAction::skip);
  JsonDocument c;deserializeJson(c,f.act(TagAction::write));
  TEST_ASSERT_EQUAL_STRING("discard_recovery",c["action"]|"");
  f.waiting=false;f.operation=0;
  feed(f,R"({"phase":"recovery_discarded","message":"Stopped."})");
  TEST_ASSERT_TRUE(f.page==TagPage::error);TEST_ASSERT_EQUAL_STRING("Recovery skipped",f.screen().title.c_str());
  // Skip is only meaningful for pending recovery.
  TagFlow g;g.lifecycle=services::TagLifecycle::linked;TEST_ASSERT_TRUE(g.act(TagAction::skip).empty());TEST_ASSERT_TRUE(g.page==TagPage::tag);
}

}  // namespace rc10
