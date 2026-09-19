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
  f.offset=0;f.row=0;f.act(TagAction::row1);TEST_ASSERT_EQUAL(22,f.selected["id"].as<int>());
  c=command(f.act(TagAction::use));TEST_ASSERT_EQUAL_STRING("preview",c["action"]);TEST_ASSERT_EQUAL(22,c["spool_id"].as<int>());
}
void reassign_review() {
  TagFlow f;f.lifecycle=services::TagLifecycle::linked;f.current_spool=28;f.act(TagAction::sources);f.entity="spool";f.page=TagPage::selected;f.selected["id"]=31;
  TEST_ASSERT_TRUE(f.act(TagAction::use).empty());TEST_ASSERT_TRUE(f.page==TagPage::move);
  auto c=command(f.act(TagAction::use));TEST_ASSERT_EQUAL_STRING("preview",c["action"]);TEST_ASSERT_EQUAL_STRING("rewrite",c["mode"]);TEST_ASSERT_EQUAL(31,c["spool_id"].as<int>());
  f.page=TagPage::created;TEST_ASSERT_TRUE(f.act(TagAction::use).empty());TEST_ASSERT_TRUE(f.page==TagPage::move);
  f.act(TagAction::back);TEST_ASSERT_TRUE(f.page==TagPage::created);
  f.act(TagAction::use);c=command(f.act(TagAction::use));
  receive(f,R"({"phase":"preview"})");f.act(TagAction::back);TEST_ASSERT_TRUE(f.page==TagPage::move);
  f.act(TagAction::back);TEST_ASSERT_TRUE(f.page==TagPage::created);
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
  receive(f,R"({"phase":"imported","filament":{"id":42,"weight":1000}})");c=command(f.act(TagAction::create));TEST_ASSERT_EQUAL(42,c["spool"]["filament_id"].as<int>());
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
void setUp() {} void tearDown() {}
int main() {UNITY_BEGIN();RUN_TEST(lifecycle);RUN_TEST(sensible_actions);RUN_TEST(bounded_paging_and_selection);RUN_TEST(reassign_review);RUN_TEST(exact_confirmation);RUN_TEST(clear_retry_to_immediate_assign);RUN_TEST(filament_and_community_create);RUN_TEST(stale_operation_rejected);RUN_TEST(keyboard_geometry);RUN_TEST(keyboard_value_and_validation);RUN_TEST(gzip_chunked_integrity_and_limits);RUN_TEST(gzip_explicit_completion_and_error);RUN_TEST(community_catalog_status_download_and_retry);RUN_TEST(production_sink_closes_on_disposition);RUN_TEST(production_sink_full_gzip_integrity);if(!opentag::config::community_enabled)export_touch_fixtures();return UNITY_END();}
