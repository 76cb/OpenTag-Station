#include "config/product_features.hpp"
#include <unity.h>
#include "ui/tag_flow.hpp"
#include "ui/touch_input.hpp"
#include "fixtures.hpp"
#include "network/gzip_stream.hpp"
#include "network/bounded_response_body.hpp"
#include "gzip_fixture.hpp"
#include "community_gzip_fixture.hpp"
using namespace opentag;
using network::StreamDisposition;
using namespace opentag::ui;
namespace {
bool has(const TagScreen& s,TagAction a) {for(std::size_t i=0;i<s.count;++i)if(s.buttons[i].action==a&&s.buttons[i].enabled)return true;return false;}
void receive(TagFlow& f,const char* json) {network::ResponseBody b(json);TEST_ASSERT_TRUE(f.consume(b));}
JsonDocument command(const std::string& text) {JsonDocument c;TEST_ASSERT_FALSE(deserializeJson(c,text));return c;}
void lifecycle() {
  using services::TagLifecycle;using services::tag_lifecycle;
  TEST_ASSERT_TRUE(tag_lifecycle({})==TagLifecycle::no_tag);
  TEST_ASSERT_TRUE(tag_lifecycle({true,true,false,false,true})==TagLifecycle::blank_compatible);
  TEST_ASSERT_TRUE(tag_lifecycle({true,false,true,true})==TagLifecycle::linked);
  TEST_ASSERT_TRUE(tag_lifecycle({true,false,true,false})==TagLifecycle::unlinked);
  TEST_ASSERT_TRUE(tag_lifecycle({false,false,false,false,false,"unlink_pending"})==TagLifecycle::cleanup_pending);
  TEST_ASSERT_TRUE(tag_lifecycle({false,false,false,false,false,"write_recovery"})==TagLifecycle::write_pending);
  TEST_ASSERT_TRUE(tag_lifecycle({true,true,false,false,false,"cleared",true})==TagLifecycle::ready);
}
void sensible_actions() {
  TagFlow f;TEST_ASSERT_FALSE(has(f.screen(),TagAction::write));TEST_ASSERT_FALSE(has(f.screen(),TagAction::clear));
  f.lifecycle=services::TagLifecycle::blank_compatible;auto s=f.screen();
  TEST_ASSERT_TRUE(has(s,TagAction::sources));TEST_ASSERT_FALSE(has(s,TagAction::update));TEST_ASSERT_FALSE(has(s,TagAction::clear));
  TEST_ASSERT_EQUAL(std::string::npos,s.body.find("Unsupported"));
  f.lifecycle=services::TagLifecycle::linked;s=f.screen();TEST_ASSERT_TRUE(has(s,TagAction::update));TEST_ASSERT_TRUE(has(s,TagAction::sources));TEST_ASSERT_TRUE(has(s,TagAction::clear));
  f.lifecycle=services::TagLifecycle::unlinked;s=f.screen();TEST_ASSERT_FALSE(has(s,TagAction::update));TEST_ASSERT_TRUE(has(s,TagAction::sources));TEST_ASSERT_TRUE(has(s,TagAction::clear));
}
void bounded_paging_and_selection() {
  TagFlow f;auto c=command(f.act(TagAction::spools));TEST_ASSERT_EQUAL_STRING("catalog",c["action"]);TEST_ASSERT_EQUAL(0,c["offset"].as<int>());
  receive(f,R"({"phase":"catalog","items":[{"id":21},{"id":22},{"id":23},{"id":24},{"id":25},{"id":26},{"id":27},{"id":28}],"has_more":true})");
  TEST_ASSERT_TRUE(f.act(TagAction::next).empty());TEST_ASSERT_EQUAL(3,f.row);
  f.act(TagAction::next);TEST_ASSERT_EQUAL(6,f.row);c=command(f.act(TagAction::next));TEST_ASSERT_EQUAL(8,c["offset"].as<int>());
  // rc.21: tapping a spool row sends the tag preview directly; no
  // "Use this spool?" stop. BACK from the review returns to the list.
  f.offset=0;f.row=0;c=command(f.act(TagAction::row1));TEST_ASSERT_EQUAL(22,f.selected["id"].as<int>());
  TEST_ASSERT_EQUAL_STRING("preview",c["action"]);TEST_ASSERT_EQUAL(22,c["spool_id"].as<int>());TEST_ASSERT_EQUAL_STRING("blank",c["mode"]);
  TEST_ASSERT_TRUE(f.page!=TagPage::selected);
  receive(f,R"({"phase":"preview","spool_id":22})");TEST_ASSERT_TRUE(f.page==TagPage::review);
  f.act(TagAction::back);TEST_ASSERT_TRUE(f.page==TagPage::catalog);TEST_ASSERT_EQUAL(0,f.row);
}
// rc.21: the move warning is part of the single review page (no separate
// "Move this tag?" + CONTINUE step), and BACK returns to the list.
void reassign_review() {
  TagFlow f;f.lifecycle=services::TagLifecycle::linked;f.current_spool=28;f.act(TagAction::sources);
  command(f.act(TagAction::spools));receive(f,R"({"phase":"catalog","items":[{"id":31,"filament":{"name":"PETG"}}]})");
  auto c=command(f.act(TagAction::row0));TEST_ASSERT_EQUAL_STRING("preview",c["action"]);TEST_ASSERT_EQUAL_STRING("rewrite",c["mode"]);TEST_ASSERT_EQUAL(31,c["spool_id"].as<int>());
  receive(f,R"({"phase":"preview","spool_id":31,"previous_spool_id":28,"repurpose":true,"spool":{"filament":{"name":"PETG"}}})");
  auto s=f.screen();TEST_ASSERT_EQUAL_STRING("Move this tag?",s.title.c_str());
  TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("From spool #28 to #31. Spool #28 stays in your inventory."));
  TEST_ASSERT_TRUE(has(s,TagAction::write));
  c=command(f.act(TagAction::write));TEST_ASSERT_EQUAL(28,c["previous_spool_id"].as<int>());
  f.waiting=false;f.page=TagPage::review;f.act(TagAction::back);TEST_ASSERT_TRUE(f.page==TagPage::catalog);
  // The UI-known link alone still warns when the writer reports no owner.
  receive(f,R"({"phase":"preview","spool_id":31})");s=f.screen();
  TEST_ASSERT_EQUAL_STRING("Move this tag?",s.title.c_str());TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("From spool #28 to #31"));
  // Rewriting the same spool is not a move.
  receive(f,R"({"phase":"preview","spool_id":28})");TEST_ASSERT_EQUAL_STRING("Ready to write",f.screen().title.c_str());
}
// Safety gap closed: a blank-flow review (no UI-known link) must still warn
// when the writer reports another spool owns the UID or the tag holds another
// spool's data.
void review_warns_from_writer_preview() {
  TagFlow f;receive(f,R"({"phase":"preview","spool_id":31,"previous_spool_id":12,"repurpose":true})");
  auto s=f.screen();TEST_ASSERT_EQUAL(0,f.from_spool);TEST_ASSERT_EQUAL_STRING("Move this tag?",s.title.c_str());
  TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("From spool #12 to #31. Spool #12 stays in your inventory."));
  TagFlow g;receive(g,R"({"phase":"preview","spool_id":31,"previous_spool_id":0,"repurpose":true})");
  s=g.screen();TEST_ASSERT_EQUAL_STRING("Move this tag?",s.title.c_str());
  TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("This tag holds another spool's data. Writing replaces it."));
  TEST_ASSERT_TRUE(has(s,TagAction::write));
  TagFlow h;receive(h,R"({"phase":"preview","spool_id":31,"previous_spool_id":0,"repurpose":false})");
  s=h.screen();TEST_ASSERT_EQUAL_STRING("Ready to write",s.title.c_str());
  // One reminder only, on the physical progress screens.
  TEST_ASSERT_EQUAL(std::string::npos,s.body.find("Keep the tag"));
  TEST_ASSERT_EQUAL_STRING("WRITE TAG",s.buttons[1].text.c_str());
}
// The main forward action is drawn in the primary colour; erase stays red and
// is never primary; BACK and DONE stay neutral.
void action_buttons_are_coloured_by_role() {
  TagFlow h;receive(h,R"({"phase":"preview","spool_id":31,"previous_spool_id":0,"repurpose":false})");
  auto s=h.screen();TEST_ASSERT_EQUAL_STRING("BACK",s.buttons[0].text.c_str());TEST_ASSERT_FALSE(s.buttons[0].primary);
  TEST_ASSERT_EQUAL_STRING("WRITE TAG",s.buttons[1].text.c_str());TEST_ASSERT_TRUE(s.buttons[1].primary);TEST_ASSERT_FALSE(s.buttons[1].destructive);
  TagFlow c;receive(c,R"({"phase":"clear_preview"})");s=c.screen();
  bool erase=false;for(std::size_t i=0;i<s.count;++i)if(s.buttons[i].text=="ERASE TAG"){erase=true;TEST_ASSERT_TRUE(s.buttons[i].destructive);TEST_ASSERT_FALSE(s.buttons[i].primary);}
  TEST_ASSERT_TRUE(erase);
  TagFlow b;b.lifecycle=services::TagLifecycle::blank_compatible;s=b.screen();
  TEST_ASSERT_TRUE(s.count>0);TEST_ASSERT_TRUE(s.buttons[0].primary);
  for(std::size_t i=1;i<s.count;++i)TEST_ASSERT_FALSE(s.buttons[i].primary);
}
// Created spool -> its tag review with no "REVIEW TAG WRITE" stop, sent
// exactly once, and never for a spool another client created.
void created_spool_previews_once() {
  TagFlow f;f.act(TagAction::filaments);receive(f,R"({"phase":"catalog","items":[{"id":12,"weight":1000,"spool_weight":130,"name":"PLA"}]})");
  f.act(TagAction::row0);f.act(TagAction::use);command(f.act(TagAction::create));f.begin_wait(5,0);
  receive(f,R"({"phase":"spool_selected","operation_id":5,"spool":{"id":77,"filament":{"name":"PLA"}}})");
  TEST_ASSERT_TRUE(f.page==TagPage::created);TEST_ASSERT_FALSE(f.waiting);
  auto c=command(f.take_auto_preview());TEST_ASSERT_EQUAL_STRING("preview",c["action"]);TEST_ASSERT_EQUAL(77,c["spool_id"].as<int>());
  TEST_ASSERT_TRUE(f.take_auto_preview().empty());
  f.begin_wait(6,0);receive(f,R"({"phase":"preview","operation_id":6,"spool_id":77})");TEST_ASSERT_TRUE(f.page==TagPage::review);
  TEST_ASSERT_TRUE(f.take_auto_preview().empty());
  // BACK from that review: the created spool stays on offer, nothing resent.
  f.act(TagAction::back);TEST_ASSERT_TRUE(f.page==TagPage::created);TEST_ASSERT_TRUE(f.take_auto_preview().empty());
  TEST_ASSERT_TRUE(has(f.screen(),TagAction::use));
  // A spool created from the browser (not this screen's request).
  TagFlow g;receive(g,R"({"phase":"spool_selected","spool":{"id":78}})");
  TEST_ASSERT_TRUE(g.page==TagPage::created);TEST_ASSERT_TRUE(g.take_auto_preview().empty());
}
void reading_and_two_tags() {
  TagFlow f;f.lifecycle=services::TagLifecycle::reading;auto s=f.screen();
  TEST_ASSERT_EQUAL_STRING("Reading tag",s.title.c_str());TEST_ASSERT_EQUAL_STRING("Hold the spool still.",s.body.c_str());
  TEST_ASSERT_TRUE(s.indicator==TagIndicator::spinner);
  const auto body=tag_body_box(s);TEST_ASSERT_TRUE(body.y+body.h<=layout::tag_spinner.y);
  f.multiple_tags=true;s=f.screen();
  TEST_ASSERT_EQUAL_STRING("Two tags on the reader. Remove one.",s.body.c_str());TEST_ASSERT_TRUE(s.indicator==TagIndicator::none);
  // The writer's reading phase with block progress: a determinate bar.
  TagFlow g;g.begin_wait(3,0);receive(g,R"({"phase":"reading","operation_id":3,"completed_blocks":12,"total_blocks":80})");
  s=g.screen();TEST_ASSERT_EQUAL_STRING("Reading tag",s.title.c_str());TEST_ASSERT_NOT_EQUAL(std::string::npos,s.body.find("12 / 80 blocks"));
  TEST_ASSERT_TRUE(s.indicator==TagIndicator::bar);TEST_ASSERT_EQUAL(12,s.done);TEST_ASSERT_EQUAL(80,s.total);TEST_ASSERT_EQUAL(0,s.count);
  TEST_ASSERT_TRUE(tag_body_box(s).y+tag_body_box(s).h<=layout::tag_bar.y);
  receive(g,R"({"phase":"reading","operation_id":3})");TEST_ASSERT_TRUE(g.screen().indicator==TagIndicator::spinner);
  receive(g,R"({"phase":"writing","operation_id":3,"completed_blocks":30,"total_blocks":20})");
  s=g.screen();TEST_ASSERT_TRUE(s.indicator==TagIndicator::bar);TEST_ASSERT_EQUAL(20,s.done);
  // Queued preview reads as reading, not "Please wait".
  TagFlow r;r.lifecycle=services::TagLifecycle::linked;r.current_spool=5;command(r.act(TagAction::update));r.begin_wait(2,0);
  s=r.screen();TEST_ASSERT_EQUAL_STRING("Reading tag",s.title.c_str());TEST_ASSERT_TRUE(s.indicator==TagIndicator::spinner);
}
void loading_spool_says_what_it_does() {
  TagFlow f;f.begin_wait(4,0);receive(f,R"({"phase":"loading_spool","operation_id":4})");
  const auto s=f.screen();TEST_ASSERT_EQUAL_STRING("Checking Spoolman",s.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Getting spool details from Spoolman...",s.body.c_str());
  TEST_ASSERT_EQUAL(std::string::npos,s.body.find("Reading"));TEST_ASSERT_TRUE(s.indicator==TagIndicator::spinner);
}
void spool_colour_ring() {
  TEST_ASSERT_EQUAL_HEX32(0xFF0000,parse_spool_rgb("FF0000"));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00,parse_spool_rgb("#00ff0080"));
  TEST_ASSERT_EQUAL_HEX32(neutral_spool_rgb,parse_spool_rgb("FFF"));
  TEST_ASSERT_EQUAL_HEX32(neutral_spool_rgb,parse_spool_rgb("GG0000"));
  TEST_ASSERT_EQUAL_HEX32(neutral_spool_rgb,parse_spool_rgb(nullptr));
  TEST_ASSERT_EQUAL_HEX32(neutral_spool_rgb,parse_spool_rgb("FF00001234"));
  TagFlow f;f.uid="E1";receive(f,R"({"phase":"complete","uid":"E1","spool_id":31,"spool":{"filament":{"name":"PLA","color_hex":"1E90FF"}}})");
  auto s=f.screen();TEST_ASSERT_TRUE(s.art);TEST_ASSERT_EQUAL_HEX32(0x1E90FF,s.art_rgb);
  // Text and ring never overlap; the ring clears the first button.
  const auto body=tag_body_box(s);TEST_ASSERT_TRUE(body.x+body.w<=layout::tag_art.x);
  for(std::size_t i=0;i<s.count;++i){const auto& b=s.buttons[i].box;const auto& a=layout::tag_art;TEST_ASSERT_TRUE(a.x+a.w<=b.x||b.x+b.w<=a.x||a.y+a.h<=b.y||b.y+b.h<=a.y);}
  receive(f,R"({"phase":"preview","spool_id":31,"proposed":{"primary_color":[0,128,255,255]}})");
  s=f.screen();TEST_ASSERT_TRUE(s.art);TEST_ASSERT_EQUAL_HEX32(0x0080FF,s.art_rgb);
  receive(f,R"({"phase":"clear_preview"})");TEST_ASSERT_FALSE(f.screen().art);
}
void exact_confirmation() {
  TagFlow f;f.uid="E004000000000001";receive(f,R"({"phase":"preview","uid":"E004000000000001","generation":"18446744073709551600","spool_id":31,"previous_spool_id":28,"current_checksum":"12345678","target_checksum":"AABBCCDD"})");
  auto c=command(f.act(TagAction::write));TEST_ASSERT_EQUAL_STRING("write",c["action"]);TEST_ASSERT_EQUAL_STRING("18446744073709551600",c["generation"]);TEST_ASSERT_EQUAL_STRING("AABBCCDD",c["target_checksum"]);TEST_ASSERT_EQUAL(28,c["previous_spool_id"].as<int>());TEST_ASSERT_EQUAL(31,c["spool_id"].as<int>());
}
void clear_retry_to_immediate_assign() {
  TagFlow f;receive(f,R"({"phase":"unlink_pending","message":"Local station cleanup: storage unavailable"})");
  TEST_ASSERT_NOT_EQUAL(std::string::npos,f.screen().body.find("storage unavailable"));
  auto c=command(f.act(TagAction::retry));TEST_ASSERT_EQUAL_STRING("retry_unlink",c["action"]);
  receive(f,R"({"phase":"cleared"})");TEST_ASSERT_TRUE(f.page==TagPage::reuse);TEST_ASSERT_TRUE(has(f.screen(),TagAction::spools));
  c=command(f.act(TagAction::spools));TEST_ASSERT_EQUAL_STRING("catalog",c["action"]);
}
void filament_and_community_create() {
  TagFlow f;f.act(TagAction::filaments);receive(f,R"({"phase":"catalog","items":[{"id":12,"weight":1000,"spool_weight":130,"name":"PLA"}]})");f.act(TagAction::row0);f.act(TagAction::use);
  TEST_ASSERT_TRUE(f.page==TagPage::create);TEST_ASSERT_EQUAL_FLOAT(130,f.tare);
  auto c=command(f.act(TagAction::create));TEST_ASSERT_EQUAL(12,c["spool"]["filament_id"].as<int>());
  if(!opentag::config::community_enabled){f.waiting=false;f.page=TagPage::sources;TEST_ASSERT_FALSE(has(f.screen(),TagAction::community));TEST_ASSERT_TRUE(f.act(TagAction::community).empty());TEST_ASSERT_TRUE(f.act(TagAction::community_update).empty());return;}
  c=command(f.act(TagAction::community));TEST_ASSERT_EQUAL_STRING("community_status",c["action"]);
  receive(f,R"({"phase":"community_catalog","catalog_state":"ready","catalog_version":"2026-09-18"})");
  f.query="SUNLU PLA";c=command(f.browse());TEST_ASSERT_EQUAL_STRING("community_search",c["action"]);
  TEST_ASSERT_EQUAL_STRING("SUNLU PLA",c["search"]);TEST_ASSERT_EQUAL(0,c["offset"].as<unsigned>());
  TEST_ASSERT_FALSE(c.containsKey("entity"));
  receive(f,R"({"phase":"community","items":[{"id":"public-filament","name":"PLA"}]})");f.act(TagAction::row0);c=command(f.act(TagAction::use));TEST_ASSERT_EQUAL_STRING("community_select",c["action"]);
  receive(f,R"({"phase":"import_preview","import_token":"TOKEN"})");c=command(f.act(TagAction::import));TEST_ASSERT_EQUAL_STRING("TOKEN",c["import_token"]);
  receive(f,R"({"phase":"imported","filament":{"id":42,"weight":1000}})");TEST_ASSERT_FALSE(f.tare_known);f.tare=180;f.tare_known=true;f.waiting=false;c=command(f.act(TagAction::create));TEST_ASSERT_EQUAL(42,c["spool"]["filament_id"].as<int>());
}
void stale_operation_rejected() {
  TagFlow f;f.operation=42;f.waiting=true;network::ResponseBody b(R"({"phase":"preview","operation_id":41})");TEST_ASSERT_FALSE(f.consume(b));TEST_ASSERT_TRUE(f.waiting);
  receive(f,R"({"phase":"preview","operation_id":42})");TEST_ASSERT_FALSE(f.waiting);
}
void keyboard_geometry() {
  for(auto mode:{InputMode::text,InputMode::numeric,InputMode::url,InputMode::password})for(bool symbols:{false,true})for(bool decimal:{false,true}) {
    auto keys=input_keys(mode,symbols,decimal);TEST_ASSERT_LESS_OR_EQUAL(36,keys.count);
    for(std::size_t i=0;i<keys.count;++i) {
      const auto& a=keys.keys[i].box;TEST_ASSERT_TRUE(a.w>=44&&a.h>=44&&a.x>=4&&a.y>=4&&a.x+a.w<=476&&a.y+a.h<=316);
      if(std::string(keys.keys[i].text)=="SPACE")TEST_ASSERT_GREATER_THAN(100,a.w);
      for(std::size_t j=0;j<i;++j){const auto& b=keys.keys[j].box;TEST_ASSERT_TRUE(a.x+a.w<=b.x||b.x+b.w<=a.x||a.y+a.h<=b.y||b.y+b.h<=a.y);}
    }
  }
}
void keyboard_value_and_validation() {
  TouchInput in;InputSpec s;s.initial="SUNLU";s.maximum_length=7;in.open(s);in.press("123");in.press("+");in.press("ABC");in.press("SPACE");in.press("Q");TEST_ASSERT_EQUAL_STRING("SUNLU+ ",in.value.c_str());in.press("CANCEL");TEST_ASSERT_EQUAL_STRING("SUNLU",in.spec.initial.c_str());
  in.value="Grün";in.press("DEL");in.press("DEL");TEST_ASSERT_EQUAL_STRING("Gr",in.value.c_str());
  s.mode=InputMode::numeric;s.initial="130";s.maximum_length=9;s.maximum=100000;in.open(s);TEST_ASSERT_TRUE(in.valid());in.value="nan";TEST_ASSERT_FALSE(in.valid());in.value="100001";TEST_ASSERT_FALSE(in.valid());in.value="1.5";in.spec.decimal=false;TEST_ASSERT_FALSE(in.valid());
}
void gzip_chunked_integrity_and_limits() {
  for(unsigned chunk:{1U,7U,256U,1024U}) {
    std::size_t received=0;
    auto stream=network::make_external<network::GzipStream>([&]{return network::GzipStream([&](const std::uint8_t*,std::size_t n){received+=n;return StreamDisposition::next;},gzip_expanded);});
    for(std::size_t i=0;i<sizeof(gzip_fixture);i+=chunk)TEST_ASSERT_TRUE(stream->feed(gzip_fixture+i,std::min<std::size_t>(chunk,sizeof(gzip_fixture)-i))==StreamDisposition::next);
    TEST_ASSERT_TRUE(stream->finish());TEST_ASSERT_EQUAL(gzip_expanded,received);
  }
  auto bounded=network::make_external<network::GzipStream>([]{return network::GzipStream([](const std::uint8_t*,std::size_t){return StreamDisposition::next;},100);});
  TEST_ASSERT_TRUE(bounded->feed(gzip_fixture,sizeof(gzip_fixture))==StreamDisposition::error);
  auto corrupt=std::vector<std::uint8_t>(std::begin(gzip_fixture),std::end(gzip_fixture));corrupt[corrupt.size()-8]^=1;
  auto stream=network::make_external<network::GzipStream>([]{return network::GzipStream([](const std::uint8_t*,std::size_t){return StreamDisposition::next;},gzip_expanded);});
  TEST_ASSERT_TRUE(stream->feed(corrupt.data(),corrupt.size())==StreamDisposition::next);TEST_ASSERT_FALSE(stream->finish());
}
void gzip_explicit_completion_and_error() {
  for(auto result:{StreamDisposition::complete,StreamDisposition::error}) {
    unsigned calls=0;
    auto stream=network::make_external<network::GzipStream>([&]{return network::GzipStream([&](const std::uint8_t*,std::size_t){++calls;return result;},gzip_expanded);});
    // Deliberately omit trailer: only explicit COMPLETE may accept this.
    TEST_ASSERT_TRUE(stream->feed(gzip_fixture,sizeof(gzip_fixture)-8)==result);
    TEST_ASSERT_EQUAL(1,calls);TEST_ASSERT_EQUAL(result==StreamDisposition::complete,stream->finish());
  }
}
void community_catalog_status_download_and_retry() {
  if(!opentag::config::community_enabled)return;
  network::OperationBudget normal,catalog;normal.begin(100);catalog.begin_catalog_update(100);
  TEST_ASSERT_TRUE(normal.expired(20100));TEST_ASSERT_FALSE(catalog.expired(20100));
  TEST_ASSERT_TRUE(catalog.expired(120100));
  TagFlow f;auto c=command(f.act(TagAction::community));TEST_ASSERT_EQUAL_STRING("community_status",c["action"]);
  receive(f,R"({"phase":"community_catalog","catalog_state":"not_installed"})");
  TEST_ASSERT_NOT_EQUAL(std::string::npos,f.screen().body.find("Not installed"));
  c=command(f.act(TagAction::community_update));TEST_ASSERT_EQUAL_STRING("community_update",c["action"]);
  receive(f,R"({"phase":"catalog_downloading","completed_blocks":42})");
  TEST_ASSERT_NOT_EQUAL(std::string::npos,f.screen().body.find("42%"));
  receive(f,R"({"phase":"failed","message":"Community catalog update failed. Your existing catalog is still available."})");
  TEST_ASSERT_TRUE(has(f.screen(),TagAction::retry));
  c=command(f.act(TagAction::retry));TEST_ASSERT_EQUAL_STRING("community_update",c["action"]);
}

void production_sink_closes_on_disposition() {
  for(bool gzip:{false,true})for(auto disposition:{StreamDisposition::complete,StreamDisposition::error}) {
    network::HttpRequest request;request.maximum_stream_bytes=community_expanded;
    unsigned calls=0,closed=0;
    request.response_consumer=[&](const std::uint8_t*,std::size_t){++calls;return disposition;};
    network::BoundedResponseBody sink(request,gzip,[&]{++closed;});
    TEST_ASSERT_EQUAL(0,sink.write(community_gzip,sizeof(community_gzip)-8));
    TEST_ASSERT_EQUAL(1,closed);TEST_ASSERT_EQUAL(1,calls);
    TEST_ASSERT_EQUAL(disposition==StreamDisposition::complete,sink.early_complete());
    TEST_ASSERT_EQUAL(disposition==StreamDisposition::error,sink.overflowed());
    TEST_ASSERT_EQUAL(0,sink.write(community_gzip,1));TEST_ASSERT_EQUAL(1,calls);TEST_ASSERT_EQUAL(1,closed);
  }
}
void production_sink_full_gzip_integrity() {
  for(bool corrupt:{false,true}) {
    network::HttpRequest request;request.maximum_stream_bytes=community_expanded;
    request.response_consumer=[](const std::uint8_t*,std::size_t){return StreamDisposition::next;};
    auto data=std::vector<std::uint8_t>(std::begin(community_gzip),std::end(community_gzip));
    if(corrupt)data[data.size()-8]^=1;
    unsigned closed=0;network::BoundedResponseBody sink(request,true,[&]{++closed;});
    TEST_ASSERT_EQUAL(data.size(),sink.write(data.data(),data.size()));
    TEST_ASSERT_EQUAL(!corrupt,sink.complete());TEST_ASSERT_FALSE(sink.early_complete());
    TEST_ASSERT_EQUAL(0,closed);
  }
}

}
#include "rc10_cases.hpp"
#include "train_cases.hpp"
#include "rc23_cases.hpp"
void setUp() {} void tearDown() {}
int main() {UNITY_BEGIN();RUN_TEST(lifecycle);RUN_TEST(sensible_actions);RUN_TEST(bounded_paging_and_selection);RUN_TEST(reassign_review);RUN_TEST(review_warns_from_writer_preview);RUN_TEST(action_buttons_are_coloured_by_role);RUN_TEST(created_spool_previews_once);RUN_TEST(reading_and_two_tags);RUN_TEST(loading_spool_says_what_it_does);RUN_TEST(spool_colour_ring);RUN_TEST(exact_confirmation);RUN_TEST(clear_retry_to_immediate_assign);RUN_TEST(filament_and_community_create);RUN_TEST(stale_operation_rejected);RUN_TEST(keyboard_geometry);RUN_TEST(keyboard_value_and_validation);RUN_TEST(gzip_chunked_integrity_and_limits);RUN_TEST(gzip_explicit_completion_and_error);RUN_TEST(community_catalog_status_download_and_retry);RUN_TEST(production_sink_closes_on_disposition);RUN_TEST(production_sink_full_gzip_integrity);RUN_TEST(rc10::assignment_dialog_only_affirmative_labels_confirm);RUN_TEST(rc10::progress_never_shows_blocks_for_spoolman_work);RUN_TEST(rc10::filament_review_formats_diameter);RUN_TEST(rc10::unknown_empty_spool_weight_must_be_entered);RUN_TEST(rc10::vendor_search_is_kept_while_paging);RUN_TEST(rc10::destructive_and_duplicate_actions);RUN_TEST(rc10::recovery_can_be_skipped_explicitly);RUN_TEST(rc10::metadata_candidates_can_be_chosen_on_the_touchscreen);
  RUN_TEST(train::nfc_error_does_not_replace_recovery_instructions);
  RUN_TEST(train::recovery_prompt_survives_leaving_and_failed_retries);
  RUN_TEST(train::different_tag_after_removal_restarts_the_flow);
  RUN_TEST(train::progress_from_another_client_is_never_a_dead_end);
  RUN_TEST(train::recovery_ended_elsewhere_and_foreign_writes_are_respected);
  RUN_TEST(train::failed_paging_and_create_mistakes_leave_no_trace);
  RUN_TEST(train::stall_timer_restarts_with_each_phase);
  RUN_TEST(rc23::weigh_result_belongs_to_the_spool_on_the_station);
  RUN_TEST(rc23::weigh_is_refused_while_saving);
  RUN_TEST(rc23::review_names_fields_that_are_not_written);
  RUN_TEST(rc23::tag_actions_use_plain_names);
  RUN_TEST(rc23::unlinked_tag_waits_for_spoolman);
  RUN_TEST(rc23::writer_errors_are_shown_in_plain_words);
  if(!opentag::config::community_enabled)export_touch_fixtures();return UNITY_END();}
