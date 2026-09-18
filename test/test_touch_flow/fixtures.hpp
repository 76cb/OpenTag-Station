#pragma once
#include <fstream>
// Review fixtures are emitted from the production presentation models, not a
// second hand-maintained layout. They are still approximations of LVGL fonts.
inline void export_touch_fixtures() {
  using namespace opentag;using namespace opentag::ui;
  JsonDocument output;
  auto emit=[&](const char* name,const TagFlow& flow) {
    const auto screen=flow.screen();auto scene=output[name].to<JsonObject>();
    scene["title"]=screen.title;scene["body"]=screen.body;
    const auto body=tag_body_box(screen);auto area=scene["body_box"].to<JsonArray>();for(auto n:{body.x,body.y,body.w,body.h})area.add(n);
    auto buttons=scene["buttons"].to<JsonArray>();
    for(std::size_t i=0;i<screen.count;++i){const auto& b=screen.buttons[i];auto item=buttons.add<JsonObject>();item["text"]=b.text;item["enabled"]=b.enabled;auto box=item["box"].to<JsonArray>();for(auto n:{b.box.x,b.box.y,b.box.w,b.box.h})box.add(n);}
  };
  TagFlow f;f.uid="E004000000000001";f.material="SUNLU PLA+ 2.0 Black";
  emit("no-tag",f);f.lifecycle=services::TagLifecycle::reading;emit("reading",f);
  f.lifecycle=services::TagLifecycle::blank_compatible;emit("blank",f);
  f.lifecycle=services::TagLifecycle::linked;f.current_spool=28;emit("tag",f);
  f.lifecycle=services::TagLifecycle::unlinked;emit("unlinked",f);
  f.act(TagAction::sources);emit("sources",f);
  auto load=[&](const char* json){network::ResponseBody b(json);f.consume(b);};
  f.entity="spool";load(R"({"phase":"catalog","items":[{"id":28,"remaining_weight":712,"filament":{"name":"PLA+ 2.0 Black","vendor":{"name":"SUNLU"}}},{"id":31,"remaining_weight":936,"filament":{"name":"PolyLite PETG Black","vendor":{"name":"Polymaker"}}},{"id":36,"remaining_weight":488,"filament":{"name":"PLA Galaxy Black","vendor":{"name":"Prusament"}}}],"has_more":true})");emit("spool-picker",f);
  f.act(TagAction::row1);emit("spool-confirm",f);f.from_spool=28;f.from_name="SUNLU PLA+ Red";f.act(TagAction::use);emit("reassign",f);
  f.entity="filament";load(R"({"phase":"catalog","items":[{"id":12,"name":"PLA+ 2.0 Black","vendor":{"name":"SUNLU"},"weight":1000,"spool_weight":130,"material":"PLA+","diameter":1.75}]})");emit("filament-picker",f);f.act(TagAction::row0);f.act(TagAction::use);emit("create-spool",f);
  f.entity="community";f.query="SUNLU PLA+";f.browse();f.page=TagPage::progress;f.phase="searching";emit("community-searching",f);
  load(R"({"phase":"community_catalog","catalog_state":"not_installed","message":"Community catalog is not installed."})");emit("community-not-installed",f);
  load(R"({"phase":"community_catalog","catalog_state":"damaged","message":"Community catalog is damaged. Redownload it."})");emit("community-damaged",f);
  f.entity="community";load(R"({"phase":"community","items":[{"id":"sunlu-pla-black","manufacturer":"SUNLU","name":"PLA+ 2.0 Black","material":"PLA+","diameter":1.75,"density":1.24,"weight":1000}]})");emit("community-results",f);f.act(TagAction::row0);emit("community-result",f);
  load(R"({"phase":"import_preview","source":{"manufacturer":"SUNLU","name":"PLA+ 2.0 Black"},"proposed_filament":{"name":"PLA+ 2.0 Black"}})");emit("import-review",f);
  load(R"({"phase":"preview","uid":"E004000000000001","spool_id":31,"spool":{"filament":{"name":"PLA+ 2.0 Black","vendor":{"name":"SUNLU"}}}})");emit("write-review",f);
  load(R"({"phase":"writing","message":"Keep the tag on the reader.","completed_blocks":17,"total_blocks":23})");emit("writing",f);
  load(R"({"phase":"verifying","message":"Reading every written block back.","completed_blocks":23,"total_blocks":23})");emit("verifying",f);
  load(R"({"phase":"associating","message":"Verifying the Spoolman association."})");emit("linking",f);
  load(R"({"phase":"complete","uid":"E004000000000001","spool_id":31,"spool":{"filament":{"name":"PLA+ 2.0 Black","vendor":{"name":"SUNLU"}}}})");emit("write-success",f);
  load(R"({"phase":"unlink_pending","message":"Tag blank and verified. Local station cleanup needs attention. Storage unavailable; retry cleanup."})");emit("cleanup-pending",f);
  load(R"({"phase":"cleared","uid":"E004000000000001"})");emit("ready-to-reuse",f);
  load(R"({"phase":"write_recovery","message":"Present the same tag to resume its verified write review.","spool_id":31})");emit("recovery",f);
  f.waiting=true;emit("loading",f);f.waiting=false;
  f.fail("Spoolman is unavailable. Check the station connection and retry. Your tag has not been changed.");emit("error",f);
  f.page=TagPage::selected;f.entity="filament";f.selected["name"]="A deliberately long filament name that remains readable in its scrollable review area";f.selected["vendor"]["name"]="Long manufacturer name";emit("long-name",f);
  for(auto mode:{InputMode::text,InputMode::numeric,InputMode::url,InputMode::password}) {
    auto keys=input_keys(mode,false,true);
    auto scene=output[mode==InputMode::text?"keyboard-qwerty":mode==InputMode::numeric?"keyboard-numeric":mode==InputMode::url?"keyboard-url":"keyboard-password"].to<JsonObject>();
    scene["title"]=mode==InputMode::numeric?"Empty spool (g)":mode==InputMode::url?"Spoolman URL":mode==InputMode::password?"Wi-Fi password":"Search Community";
    scene["input"]=mode==InputMode::numeric?"130":mode==InputMode::url?"http://spoolman.example:7912":mode==InputMode::password?"********":"SUNLU PLA+ 2.0";scene["numeric"]=mode==InputMode::numeric;
    auto buttons=scene["buttons"].to<JsonArray>();
    for(std::size_t i=0;i<keys.count;++i){auto item=buttons.add<JsonObject>();const auto& k=keys.keys[i];item["text"]=std::string(k.text)=="ACTION"?(mode==InputMode::text?"SEARCH":"DONE"):k.text;auto box=item["box"].to<JsonArray>();for(auto n:{k.box.x,k.box.y,k.box.w,k.box.h})box.add(n);}
  }
  output["keyboard-long"].set(output["keyboard-url"]);output["keyboard-long"]["input"]="…very-long-spoolman-hostname.example:7912";
  std::ofstream file(".pio/touch-flow-fixtures.json");serializeJsonPretty(output,file);
}
