#include "config/product_features.hpp"
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "network/backend_json.hpp"
#include "services/tag_lifecycle.hpp"
#include "ui/product_layout.hpp"

namespace opentag::ui {
enum class TagPage { tag, sources, catalog, selected, create, created, move,
                     review, progress, import_review, ready, reuse, error, skip_review, choose };
enum class TagAction { none, home, back, sources, spools, filaments, community,
  row0,row1,row2,previous,next,search,use,write,update,clear,retry,import,community_update,
  create,initial,remaining,tare,weigh,printer,name,skip,choose };
// Destructive buttons (clear, skip recovery) are drawn in a distinct colour.
struct TagButton { layout::Box box{}; std::string text; TagAction action{TagAction::none}; bool enabled{true}; bool destructive{false}; };
struct TagScreen {
  std::string title,body;
  std::array<TagButton,8> buttons{}; std::size_t count{0};
  void button(layout::Box box,std::string text,TagAction action,bool enabled=true,bool destructive=false) {
    buttons[count++]={box,std::move(text),action,enabled,destructive};
  }
};
// Phases in which the physical tag is being changed. The UI must never offer
// a way out during these, and must never show them for Spoolman-only work.
inline bool physical_phase(const std::string& phase) {
  return phase=="validating"||phase=="writing"||phase=="clearing"||phase=="verifying"||phase=="decoding";
}
inline bool recovery_phase(const std::string& phase) {
  return phase=="unlink_pending"||phase=="association_pending"||phase=="clear_recovery"||phase=="write_recovery";
}
inline std::string grams_text(JsonVariantConst value) {
  if(value.isNull())return "weight unknown";
  char buffer[24];std::snprintf(buffer,sizeof(buffer),"%.0f g",value.as<double>());return buffer;
}
inline layout::Box tag_body_box(const TagScreen& screen) {
  std::int16_t bottom=266;
  for(std::size_t i=0;i<screen.count;++i)
    if(screen.buttons[i].box.y>48)bottom=std::min(bottom,screen.buttons[i].box.y);
  return {16,48,448,static_cast<std::int16_t>(bottom-56)};
}
inline std::string weight_text(double value) {
  char buffer[32];std::snprintf(buffer,sizeof(buffer),"%.3f",value);
  std::string text=buffer;while(!text.empty()&&text.back()=='0')text.pop_back();
  if(!text.empty()&&text.back()=='.')text.pop_back();return text;
}
class TagFlow {
 public:
  TagFlow() {}
  TagPage page{TagPage::tag};
  TagPage review_return{TagPage::tag},move_return{TagPage::selected};
  services::TagLifecycle lifecycle{services::TagLifecycle::no_tag};
  std::string uid,material,query,entity{"spool"},message,phase,catalog_state,catalog_version;
  // Field Spoolman actually matched ("name", or "vendor" after a fallback);
  // echoed when paging so later pages use the same search.
  std::string search_field{"name"};
  // Plain-language description of the queued request for the wait screen.
  std::string working;
  // Why the reader rejected the tag now on it; shown for an unsupported tag.
  std::string tag_error;
  // The tag most recently on the reader; survives the reader being empty.
  std::string last_uid;
  // The unfinished write, clear or Spoolman link the writer last reported.
  // Kept until the writer reports it finished, cleared or skipped, so the
  // prompt returns after DONE, a failed retry or BACK from its review.
  struct Recovery { std::string phase,message; int spool_id{0}; } recovery;
  // Set by the UI when a non-physical request has shown no progress for a
  // long time; offers a way back without abandoning physical work.
  bool stalled{false};
  bool land_on_last_rows{false};
  // Spools that may belong to this tag (vendor/material or product match).
  // The user picks one; the UI submits it as an explicit confirmation.
  std::vector<std::pair<int,std::string>> candidates;
  int confirm_spool_id{0};
  // Shown on the create page when a required value is missing.
  std::string create_hint;
  int current_spool{0},from_spool{0};
  std::string from_name;
  std::string import_name;
  unsigned offset{0},row{0};
  double initial{1000},remaining{1000},tare{0};
  // False until the empty-spool weight comes from Spoolman or the user. A
  // silent 0 g default overstated remaining filament on every later weigh.
  bool tare_known{false};
  bool waiting{false},rewrite{false},community_request{false},catalog_update_request{false};
  std::uint64_t operation{0};
  // When the current touch request was queued, for the "taking longer than
  // usual" escape on Spoolman-only work.
  std::uint32_t wait_started_ms{0};
  // Phase the stall timer is running for; a new phase restarts it.
  std::string stall_phase;
  network::BackendJsonAllocator allocator;
  JsonDocument view{&allocator},selected{&allocator},catalog_page{&allocator};

  std::string request(const char* action) {
    JsonDocument command(&allocator); command["action"]=action;
    return encode(command);
  }
  std::string browse() {
    JsonDocument command(&allocator);
    if(entity=="community")
      command["action"]="community_search";
    else {
      command["action"]="catalog";
      command["entity"]=entity;
    }
    command["search"]=query;command["offset"]=offset;
    if(entity!="community"&&!query.empty()&&offset>0&&search_field=="vendor")command["search_field"]="vendor";
    return encode(command);
  }
  void fail(std::string reason) {waiting=false;land_on_last_rows=false;message=std::move(reason);page=TagPage::error;}
  // Wait, leave and tag-observation transitions. UiService supplies the
  // receipts, reader state and clock; the rules live here so they are
  // host-tested.
  std::string search(std::string text) {query=std::move(text);offset=row=0;land_on_last_rows=false;return browse();}
  void begin_wait(std::uint64_t operation_id,std::uint32_t now_ms) {
    waiting=true;operation=operation_id;page=TagPage::progress;phase=stall_phase="queued";message=working;stalled=false;wait_started_ms=now_ms;
  }
  // BACK TO HOME from a stalled wait: stop following the request.
  void abandon_wait() {waiting=false;operation=0;stalled=false;}
  // Leaving the tag workspace for the home, weigh or printer page.
  void leave_page() {page=TagPage::tag;phase.clear();stalled=false;resume_recovery();}
  // The reader's current tag: a different tag restarts the flow, whether it
  // replaced the previous one between two reads or after an empty reader.
  void observe_tag(const std::string& incoming_uid,const std::string* error_message) {
    // Never while a tag is being changed or linked, including work another
    // client started: that progress page must stay until it ends.
    const bool changing=physical_phase(phase)||phase=="associating"||phase=="unlinking";
    if(!waiting&&!changing&&!incoming_uid.empty()&&!last_uid.empty()&&incoming_uid!=last_uid&&!recovery_phase(phase)) {
      page=TagPage::tag;phase.clear();selected.clear();from_spool=0;resume_recovery();
    }
    uid=incoming_uid;if(!incoming_uid.empty())last_uid=incoming_uid;
    // Kept apart from message: that may hold recovery instructions.
    if(error_message)tag_error=*error_message;else tag_error.clear();
  }
  // The queued request failed without publishing a writer snapshot.
  void operation_failed(std::string reason) {operation=0;fail(std::move(reason));}
  // A Spoolman-only request (never a physical write) that shows no result
  // for 45 s offers a way back instead of an endless wait screen. Each step
  // gets its own 45 s: linking after a long physical write is not "late".
  void update_stall(std::uint32_t now_ms) {
    if(phase!=stall_phase){stall_phase=phase;wait_started_ms=now_ms;}
    stalled=waiting&&!physical_phase(phase)&&static_cast<std::uint32_t>(now_ms-wait_started_ms)>=45000U;
  }
  bool consume(const network::ResponseBody& body) {
    JsonDocument incoming(&allocator);
    if(body.size()>24576||deserializeJson(incoming,body.data(),body.size(),DeserializationOption::NestingLimit(12))) {
      fail("Station response unavailable; try again");return false;
    }
    if(operation && incoming["operation_id"].as<std::uint64_t>()!=operation)return false;
    const std::string result_uid=incoming["uid"]|"";
    const std::string result_phase=incoming["phase"]|"";
    // The recovery record is station-wide: once the writer reports it ended,
    // forget it even when that result is for a tag no longer on the reader.
    if(result_phase=="complete"||result_phase=="cleared"||result_phase=="recovery_discarded")recovery={};
    if(!operation&&!result_uid.empty()&&result_uid!=uid&&
       (result_phase=="complete"||result_phase=="cleared"||result_phase=="preview"||result_phase=="clear_preview"))return false;
    // A Spoolman edit made in the browser is not part of the touch flow.
    if(result_phase=="editing"||result_phase=="updated")return true;
    view.set(incoming); phase=view["phase"]|""; message=view["message"]|"";
    if(recovery_phase(phase))recovery={phase,message,view["spool_id"]|0};
    if(phase=="catalog"||phase=="community") {
      if(!view["items"].is<JsonArray>()||view["items"].size()>8){fail("Inventory page exceeds limit");return false;}
      catalog_page.set(view);search_field=view["search_field"]|"name";
      const auto size=view["items"].size();
      row=land_on_last_rows&&size>0?static_cast<unsigned>(((size-1)/3)*3):0;land_on_last_rows=false;page=TagPage::catalog;
    } else if(phase=="community_catalog") {
      catalog_state=view["catalog_state"]|"not_installed";catalog_version=view["catalog_version"]|"";page=TagPage::catalog;
    } else if(phase=="preview"||phase=="clear_preview")page=TagPage::review;
    else if(phase=="import_preview")page=TagPage::import_review;
    else if(phase=="imported") {selected.set(view["filament"]);weights();page=TagPage::create;}
    else if(phase=="spool_selected") {selected.set(view["spool"]);page=TagPage::created;}
    else if(phase=="complete")page=TagPage::ready;
    else if(phase=="cleared") {page=TagPage::reuse;current_spool=from_spool=0;rewrite=false;}
    else if(recovery_phase(phase))page=TagPage::tag;
    else if(phase=="failed"||phase=="recovery_discarded")page=TagPage::error;
    else if(phase!="idle") {page=TagPage::progress;return true;}
    waiting=false;operation=0;return true;
  }
  std::string act(TagAction action) {
    if(waiting)return {};
    if(!config::community_enabled && (action==TagAction::community || action==TagAction::community_update || action==TagAction::import))return {};
    JsonDocument command(&allocator);
    switch(action) {
      case TagAction::sources:
        from_spool=current_spool;from_name=material;rewrite=lifecycle==services::TagLifecycle::linked||lifecycle==services::TagLifecycle::unlinked;
        page=TagPage::sources;return {};
      case TagAction::spools: case TagAction::filaments: case TagAction::community:
        entity=action==TagAction::spools?"spool":action==TagAction::filaments?"filament":"community";
        offset=row=0;land_on_last_rows=false;query.clear();search_field="name";page=TagPage::catalog;
        if(entity=="community"){catalog_page.clear();return request("community_status");}
        return browse();
      case TagAction::previous:
        if(row>=3){row-=3;return {};}
        if(offset>=8){offset-=8;land_on_last_rows=true;return browse();}return {};
      case TagAction::next:
        if(row+3<catalog_page["items"].size()){row+=3;return {};}
        if(catalog_page["has_more"]|false){offset+=8;return browse();}return {};
      case TagAction::choose:
        if(candidates.empty())return {};
        page=TagPage::choose;return {};
      case TagAction::row0: case TagAction::row1: case TagAction::row2: {
        if(page==TagPage::choose) {
          const auto pick=static_cast<std::size_t>(action)-static_cast<std::size_t>(TagAction::row0);
          if(pick<candidates.size())confirm_spool_id=candidates[pick].first;
          page=TagPage::tag;return {};
        }
        const auto index=row+static_cast<unsigned>(action)-static_cast<unsigned>(TagAction::row0);
        if(index>=catalog_page["items"].size())return {};
        selected.set(catalog_page["items"][index]);import_name.clear();page=TagPage::selected;return {};
      }
      case TagAction::use:
        if(page==TagPage::selected&&entity=="community") {
          command["action"]="community_select";command["id"]=selected["id"];
          if(!import_name.empty())command["import_name"]=import_name;
          break;
        }
        if(page==TagPage::selected&&entity=="filament") {weights();create_hint.clear();page=TagPage::create;return {};}
        if((page==TagPage::selected||page==TagPage::created)&&from_spool>0&&selected["id"].as<int>()!=from_spool) {move_return=page;page=TagPage::move;return {};}
        review_return=page;
        command["action"]="preview";command["spool_id"]=selected["id"];
        command["mode"]=rewrite?"rewrite":"blank";break;
      case TagAction::update:
        if(current_spool<=0)return {};
        review_return=TagPage::tag;
        command["action"]="preview";command["mode"]="rewrite";command["spool_id"]=current_spool;break;
      case TagAction::write:
        if(page==TagPage::skip_review){command["action"]="discard_recovery";break;}
        if(page!=TagPage::review||(view["semantic_no_change"]|false))return {};
        command["action"]=phase=="clear_preview"?"clear":"write";
        for(auto key:{"uid","generation","spool_id","previous_spool_id","current_checksum","target_checksum"})
          if(!view[key].isNull())command[key]=view[key];
        break;
      case TagAction::clear:review_return=TagPage::tag;command["action"]="clear_preview";break;
      case TagAction::skip:
        if(!recovery_phase(phase))return {};
        review_return=TagPage::tag;page=TagPage::skip_review;return {};
      case TagAction::retry:
        if(page==TagPage::error&&catalog_update_request)return request("community_update");
        if(page==TagPage::error&&community_request)return browse();
        if(phase=="write_recovery") {
          review_return=TagPage::tag;command["action"]="preview";command["mode"]="rewrite";
          // The view may since hold the result of a failed retry.
          if(recovery_phase(view["phase"]|""))command["spool_id"]=view["spool_id"];else command["spool_id"]=recovery.spool_id;
          break;
        }
        command["action"]=phase=="association_pending"?"retry_association":phase=="clear_recovery"?"clear_preview":"retry_unlink";break;
      case TagAction::import:command["action"]="import";command["import_token"]=view["import_token"];break;
      case TagAction::community_update:return request("community_update");
      case TagAction::create:
        if(!tare_known){create_hint="Enter the empty spool weight first (weigh an empty reel of the same type, or use the maker's value).";return {};}
        // Stay on the form: the entered values are kept for correction.
        if(initial<=0||remaining<0||remaining>initial||tare<0||initial>100000||tare>100000){create_hint="Check the spool weights: remaining cannot be more than initial, and none can be above 100000 g.";return {};}
        create_hint.clear();
        command["action"]="create_spool";command["spool"]["filament_id"]=selected["id"];
        command["spool"]["initial_weight"]=initial;command["spool"]["remaining_weight"]=remaining;command["spool"]["spool_weight"]=tare;break;
      case TagAction::back:
        if(page==TagPage::selected||page==TagPage::create)page=TagPage::catalog;
        else if(page==TagPage::move)page=move_return;
        else if(page==TagPage::catalog)page=TagPage::sources;
        else if(page==TagPage::review||page==TagPage::skip_review)page=review_return;
        else if(page==TagPage::choose)page=TagPage::tag;
        else if(page==TagPage::import_review)page=TagPage::selected;
        else page=TagPage::tag;
        if(page==TagPage::tag)resume_recovery();
        return {};
      default:return {};
    }
    return encode(command);
  }
  TagScreen screen() const;
 private:
  void resume_recovery() {if(!recovery.phase.empty()){phase=recovery.phase;message=recovery.message;}}
  void weights() {
    initial=selected["weight"]|1000.;if(initial<=0)initial=1000;remaining=initial;
    // Filament empty-reel weight, else the vendor's; otherwise ask the user.
    JsonVariantConst package=selected["spool_weight"],vendor=selected["vendor"]["empty_spool_weight"];
    tare_known=package.is<double>()||vendor.is<double>();
    tare=package.is<double>()?package.as<double>():vendor.is<double>()?vendor.as<double>():0.;
  }
  static const char* describe(std::string_view action) {
    if(action=="catalog"||action=="community_search")return "Searching Spoolman...";
    if(action=="preview"||action=="clear_preview")return "Reading the tag...";
    if(action=="write")return "Writing the tag...";
    if(action=="clear")return "Clearing the tag...";
    if(action=="create_spool")return "Creating the spool in Spoolman...";
    if(action=="retry_association")return "Saving the link in Spoolman...";
    if(action=="retry_unlink")return "Removing the link in Spoolman...";
    if(action=="discard_recovery")return "Stopping recovery...";
    return "Working on your request...";
  }
  std::string encode(JsonDocument& command) {
    if(command.overflowed()||measureJson(command)>4096){fail("Command exceeds station limits");return {};}
    working=describe(std::string_view(command["action"]|""));stalled=false;
    community_request=std::string_view(command["action"]|"")=="community_search";
    catalog_update_request=std::string_view(command["action"]|"")=="community_update";
    std::string result;serializeJson(command,result);return result;
  }
};
inline std::string filament_name(JsonVariantConst item) {
  auto f=item["filament"].is<JsonObjectConst>()?item["filament"]:item;
  std::string vendor=f["manufacturer"] | (f["vendor"]["name"] | "");
  return vendor+(vendor.empty()?"":" ")+std::string(f["name"]|"Unnamed filament");
}
inline std::string diameter_text(JsonVariantConst value) {
  char buffer[16];std::snprintf(buffer,sizeof(buffer),"%.2f mm",value|1.75);return buffer;
}
inline TagScreen TagFlow::screen() const {
  TagScreen s; s.title="Manage tag";
  const auto action=[&](int n,const char* label,TagAction a){s.button({16,static_cast<std::int16_t>(112+n*50),448,44},label,a);};
  const auto back=[&]{s.button({8,266,140,46},"BACK",TagAction::back);};
  const auto primary=[&](const char* text,TagAction a,bool enabled=true,bool destructive=false){s.button({156,266,316,46},text,a,enabled,destructive);};
  switch(page) {
    case TagPage::tag:
      if(recovery_phase(phase)) {
        s.title=phase=="write_recovery"?"Finish writing this tag":phase=="clear_recovery"?"Finish clearing this tag":phase=="association_pending"?"Link not saved yet":"Unlink not finished";
        s.body=message;
        action(1,phase=="write_recovery"?"FINISH WRITING":phase=="clear_recovery"?"FINISH CLEARING":phase=="association_pending"?"TRY SAVING LINK AGAIN":"TRY UNLINKING AGAIN",TagAction::retry);
        s.button({16,212,448,44},"SKIP RECOVERY",TagAction::skip,true,true);
      } else if(lifecycle==services::TagLifecycle::blank_compatible) {
        s.body="BLANK TAG\nReady to use\n"+uid;action(0,"ASSIGN TAG",TagAction::sources);
      } else if(lifecycle==services::TagLifecycle::linked) {
        s.body=material+"\nLinked to spool #"+std::to_string(current_spool);
        action(0,"UPDATE TAG",TagAction::update);action(1,"REASSIGN",TagAction::sources);
        s.button({16,212,448,44},"CLEAR / REUSE",TagAction::clear,true,true);
      } else if(lifecycle==services::TagLifecycle::unlinked&&!candidates.empty()) {
        s.body=material+"\nThis may be one of your Spoolman spools.";
        action(0,"CHOOSE WHICH SPOOL",TagAction::choose);action(1,"LINK TO A SPOOL",TagAction::sources);
        s.button({16,212,448,44},"CLEAR / REUSE",TagAction::clear,true,true);
      } else if(lifecycle==services::TagLifecycle::unlinked) {
        s.body=material+"\nNot linked to a Spoolman spool yet";
        action(0,"LINK TO A SPOOL",TagAction::sources);
        s.button({16,162,448,44},"CLEAR / REUSE",TagAction::clear,true,true);
      } else s.body=lifecycle==services::TagLifecycle::no_tag?"PLACE A TAG\nSet an NFC tag on the reader.":lifecycle==services::TagLifecycle::unsupported?"This tag can't be used as it is\n"+tag_error+"\nUse a blank NXP ICODE SLIX2 tag.":"Reading tag...\nKeep the tag on the reader.";
      s.button({8,266,464,46},"DONE",TagAction::home);break;
    case TagPage::sources:
      s.title=from_spool?"Reassign tag":"Assign tag";s.body="How do you want to choose the filament?";
      action(0,"MY SPOOLS",TagAction::spools);action(1,"MY FILAMENTS",TagAction::filaments);if(config::community_enabled)action(2,"COMMUNITY",TagAction::community);back();break;
    case TagPage::catalog:
      s.title=entity=="spool"?"My Spools":entity=="filament"?"My Filaments":"Community";
      if(entity=="community"&&catalog_state!="ready") {s.title="Community Catalog";s.body=catalog_state=="damaged"?"Catalog damaged\nRedownload to restore local search.":"Not installed\nDownload once for offline search.";action(1,catalog_state=="damaged"?"REDOWNLOAD CATALOG":"DOWNLOAD CATALOG",TagAction::community_update);back();break;}
      s.button({326,4,146,44},"SEARCH",TagAction::search);
      if(catalog_page["items"].size()==0)s.body=offset>0?"End of the list.":query.empty()?(entity=="community"?"Community Catalog\n"+catalog_version+"\nReady\nSearch for a filament":entity=="spool"?"No spools in Spoolman yet.":"No filaments in Spoolman yet."):"No matches for \""+query+"\".\nSearch looks at names and makers.";
      for(unsigned i=0;i<3&&row+i<catalog_page["items"].size();++i) {
        auto item=catalog_page["items"][row+i];std::string text=filament_name(item);
        if(entity=="spool")text="#"+std::to_string(item["id"].as<int>())+" "+text+"\n"+(item["remaining_weight"].isNull()?std::string("Remaining weight unknown"):grams_text(item["remaining_weight"])+" remaining");
        else text+="\n"+std::string(item["material"]|"")+"  "+grams_text(item["weight"]);
        s.button({8,static_cast<std::int16_t>(58+i*62),464,56},text,static_cast<TagAction>(static_cast<int>(TagAction::row0)+i));
      }
      s.button({8,266,140,46},"PREV",TagAction::previous,row>0||offset>0);
      s.button({156,266,168,46},"BACK",TagAction::back);
      s.button({332,266,140,46},"NEXT",TagAction::next,row+3<catalog_page["items"].size()||(catalog_page["has_more"]|false));break;
    case TagPage::selected:
      s.title=entity=="spool"?"Use this spool?":"Use this filament?";s.body=filament_name(selected);
      if(entity=="spool")s.body+="\nSpool #"+std::to_string(selected["id"].as<int>())+" - "+(selected["remaining_weight"].isNull()?std::string("remaining weight unknown"):grams_text(selected["remaining_weight"])+" remaining");
      else s.body+="\n"+std::string(selected["material"]|"")+" - "+diameter_text(selected["diameter"]);
      if(entity=="community") {
        s.button({16,212,448,44},"EDIT DISPLAY NAME",TagAction::name);
        if(!import_name.empty())s.body+="\nSpoolman name: "+import_name;
      }
      back();primary(entity=="community"?"REVIEW IMPORT":entity=="filament"?"CREATE A SPOOL":"USE SPOOL",TagAction::use);break;
    case TagPage::create:
      s.title="Create physical spool";s.body=filament_name(selected);
      if(!create_hint.empty())s.body+="\n"+create_hint;
      s.button({16,112,448,44},"Initial: "+weight_text(initial)+" g  -  EDIT",TagAction::initial);
      s.button({16,162,448,44},"Remaining: "+weight_text(remaining)+" g  -  EDIT",TagAction::remaining);
      s.button({16,212,448,44},tare_known?"Empty spool: "+weight_text(tare)+" g  -  EDIT":std::string("Empty spool: NOT SET  -  EDIT"),TagAction::tare);
      back();primary("CREATE SPOOL",TagAction::create);break;
    case TagPage::created:
      s.title="Spool created";s.body=filament_name(selected)+"\nSpool #"+std::to_string(selected["id"].as<int>());
      back();primary("REVIEW TAG WRITE",TagAction::use);break;
    case TagPage::move:
      s.title="Move this tag?";s.body="FROM #"+std::to_string(from_spool)+" "+from_name+"\nTO #"+std::to_string(selected["id"].as<int>())+" "+filament_name(selected)+"\nThe previous spool stays in inventory.";
      back();primary("CONTINUE",TagAction::use);break;
    case TagPage::import_review:
      s.title="Add to Spoolman";s.body=filament_name(view["source"])+"\nSpoolman name: "+std::string(view["proposed_filament"]["name"]|"")+"\nExisting matching definitions will be reused.";
      back();primary("ADD TO SPOOLMAN",TagAction::import);break;
    case TagPage::review: {
      const bool clearing=phase=="clear_preview";const bool unchanged=!clearing&&(view["semantic_no_change"]|false);
      s.title=clearing?"Erase this tag?":unchanged?"Tag already up to date":"Ready to write";
      s.body=clearing?"Erases the filament data on the tag and removes its link in Spoolman.\nThe spool itself stays in Spoolman.":unchanged?filament_name(view["spool"])+"\nNothing needs to be written.":filament_name(view["spool"])+"\nSpool #"+std::to_string(view["spool_id"].as<int>())+"\nReplaces the filament data on the tag.\nKeep the tag on the reader until it says done.";
      back();primary(clearing?"ERASE TAG":unchanged?"NOTHING TO WRITE":"WRITE TAG",TagAction::write,!unchanged,clearing);break;
    }
    case TagPage::skip_review:
      s.title="Skip recovery?";
      s.body=phase=="association_pending"?"The tag is written, but Spoolman will not know it belongs to this spool.\nYou can link it again later from Manage tag.":phase=="unlink_pending"?"The tag is erased, but Spoolman may still list it on the old spool.":"Only do this if that tag is lost or damaged.\nThe station will not be able to write or clear it again.";
      back();primary("SKIP RECOVERY",TagAction::write,true,true);break;
    case TagPage::choose:
      s.title="Which spool is this?";
      for(std::size_t i=0;i<3&&i<candidates.size();++i)
        s.button({8,static_cast<std::int16_t>(58+i*62),464,56},"#"+std::to_string(candidates[i].first)+" "+candidates[i].second,static_cast<TagAction>(static_cast<int>(TagAction::row0)+static_cast<int>(i)));
      if(candidates.size()>3)s.body="More matches: choose in the browser.";
      back();break;
    case TagPage::progress:
      if(phase=="catalog_downloading") {s.title="Downloading Community catalog";s.body=std::to_string(view["completed_blocks"]|0)+"%\nThe station remains available.";break;}
      if(community_request&&(phase=="searching"||phase=="queued")) {s.title="Searching Community...";s.body=query+"\nSearching catalog. Please wait...";break;}
      if(physical_phase(phase)) {
        s.title=phase=="clearing"?"Clearing tag":phase=="writing"?"Writing tag":"Checking tag";
        s.body="Keep the tag on the reader.";
        if((view["total_blocks"]|0)>0)s.body+="\n"+std::to_string(view["completed_blocks"]|0)+" / "+std::to_string(view["total_blocks"]|0)+" blocks";
        break;
      }
      s.title=phase=="associating"?"Saving link in Spoolman":phase=="unlinking"?"Removing link in Spoolman":phase=="reading"?"Reading tag":phase=="loading_spool"?"Checking Spoolman":"Please wait";
      s.body=phase=="associating"?"The tag is already written.":phase=="unlinking"?"The tag is already erased.":phase=="reading"?"Keep the tag on the reader.":working.empty()?"Working on your request...":working;
      if(stalled)s.body+="\nThis is taking longer than usual.";
      // Another client's request (not waiting) must not strand this screen.
      // The writer's own link steps always end in a result and stay as is.
      if(stalled||(!waiting&&phase!="associating"&&phase!="unlinking"))s.button({8,266,464,46},"BACK TO HOME",TagAction::home);
      break;
    case TagPage::reuse:
      s.title="TAG READY TO REUSE";s.body="The old filament information is removed.\nWhat should this tag become?";
      action(0,"CHOOSE EXISTING SPOOL",TagAction::spools);action(1,"NEW SPOOL FROM A FILAMENT",TagAction::filaments);
      s.button({8,266,464,46},"DONE",TagAction::home);break;
    case TagPage::ready:
      s.title="TAG READY";s.body=filament_name(view["spool"])+"\nSpool #"+std::to_string(view["spool_id"].as<int>())+" - verified";
      action(0,"WEIGH",TagAction::weigh);action(1,"ASSIGN TO PRINTER",TagAction::printer);
      // Wait for the internally refreshed physical/workflow identity before
      // navigating to pages that operate on the active spool.
      s.buttons[0].enabled=s.buttons[1].enabled=current_spool>0&&current_spool==view["spool_id"].as<int>();
      if(!s.buttons[0].enabled)s.body+="\nRefreshing the active spool...";
      s.button({8,266,464,46},"DONE",TagAction::home);break;
    case TagPage::error:
      s.title=phase=="recovery_discarded"?"Recovery skipped":"Needs attention";s.body=message;back();
      if(community_request||catalog_update_request)primary("RETRY",TagAction::retry);break;
  }
  if(waiting&&page!=TagPage::progress) {
    s.title="Please wait";s.body=working.empty()?"Working on your request...":working;s.count=0;
    if(stalled&&!physical_phase(phase)){s.body+="\nThis is taking longer than usual.";s.button({8,266,464,46},"BACK TO HOME",TagAction::home);}
  }
  return s;
}
}  // namespace opentag::ui
