#include "config/product_features.hpp"
#pragma once
#include <array>
#include <cstdio>
#include <algorithm>
#include <string>
#include "network/backend_json.hpp"
#include "services/tag_lifecycle.hpp"
#include "ui/product_layout.hpp"

namespace opentag::ui {
enum class TagPage { tag, sources, catalog, selected, create, created, move,
                     review, progress, import_review, ready, reuse, error };
enum class TagAction { none, home, back, sources, spools, filaments, community,
  row0,row1,row2,previous,next,search,use,write,update,clear,retry,import,community_update,
  create,initial,remaining,tare,weigh,printer,name };
struct TagButton { layout::Box box{}; std::string text; TagAction action{TagAction::none}; bool enabled{true}; };
struct TagScreen {
  std::string title,body;
  std::array<TagButton,8> buttons{}; std::size_t count{0};
  void button(layout::Box box,std::string text,TagAction action,bool enabled=true) {
    buttons[count++]={box,std::move(text),action,enabled};
  }
};
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
  int current_spool{0},from_spool{0};
  std::string from_name;
  std::string import_name;
  unsigned offset{0},row{0};
  double initial{1000},remaining{1000},tare{0};
  bool waiting{false},rewrite{false},community_request{false},catalog_update_request{false};
  std::uint64_t operation{0};
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
    return encode(command);
  }
  void fail(std::string reason) {waiting=false;message=std::move(reason);page=TagPage::error;}
  bool consume(const network::ResponseBody& body) {
    JsonDocument incoming(&allocator);
    if(body.size()>24576||deserializeJson(incoming,body.data(),body.size(),DeserializationOption::NestingLimit(12))) {
      fail("Station response unavailable; try again");return false;
    }
    if(operation && incoming["operation_id"].as<std::uint64_t>()!=operation)return false;
    const std::string result_uid=incoming["uid"]|"";
    const std::string result_phase=incoming["phase"]|"";
    if(!operation&&!result_uid.empty()&&result_uid!=uid&&
       (result_phase=="complete"||result_phase=="cleared"||result_phase=="preview"||result_phase=="clear_preview"))return false;
    view.set(incoming); phase=view["phase"]|""; message=view["message"]|"";
    if(phase=="catalog"||phase=="community") {
      if(!view["items"].is<JsonArray>()||view["items"].size()>8){fail("Inventory page exceeds limit");return false;}
      catalog_page.set(view);row=0;page=TagPage::catalog;
    } else if(phase=="community_catalog") {
      catalog_state=view["catalog_state"]|"not_installed";catalog_version=view["catalog_version"]|"";page=TagPage::catalog;
    } else if(phase=="preview"||phase=="clear_preview")page=TagPage::review;
    else if(phase=="import_preview")page=TagPage::import_review;
    else if(phase=="imported") {selected.set(view["filament"]);weights();page=TagPage::create;}
    else if(phase=="spool_selected") {selected.set(view["spool"]);page=TagPage::created;}
    else if(phase=="complete")page=TagPage::ready;
    else if(phase=="cleared") {page=TagPage::reuse;current_spool=from_spool=0;rewrite=false;}
    else if(phase=="unlink_pending"||phase=="association_pending"||phase=="clear_recovery"||phase=="write_recovery")page=TagPage::tag;
    else if(phase=="failed")page=TagPage::error;
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
        offset=row=0;query.clear();page=TagPage::catalog;
        if(entity=="community"){catalog_page.clear();return request("community_status");}
        return browse();
      case TagAction::previous:
        if(row>=3){row-=3;return {};}
        if(offset>=8){offset-=8;return browse();}return {};
      case TagAction::next:
        if(row+3<catalog_page["items"].size()){row+=3;return {};}
        if(catalog_page["has_more"]|false){offset+=8;return browse();}return {};
      case TagAction::row0: case TagAction::row1: case TagAction::row2: {
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
        if(page==TagPage::selected&&entity=="filament") {weights();page=TagPage::create;return {};}
        if((page==TagPage::selected||page==TagPage::created)&&from_spool>0&&selected["id"].as<int>()!=from_spool) {move_return=page;page=TagPage::move;return {};}
        review_return=page;
        command["action"]="preview";command["spool_id"]=selected["id"];
        command["mode"]=rewrite?"rewrite":"blank";break;
      case TagAction::update:
        if(current_spool<=0)return {};
        review_return=TagPage::tag;
        command["action"]="preview";command["mode"]="rewrite";command["spool_id"]=current_spool;break;
      case TagAction::write:
        if(page!=TagPage::review)return {};
        command["action"]=phase=="clear_preview"?"clear":"write";
        for(auto key:{"uid","generation","spool_id","previous_spool_id","current_checksum","target_checksum"})
          if(!view[key].isNull())command[key]=view[key];
        break;
      case TagAction::clear:review_return=TagPage::tag;command["action"]="clear_preview";break;
      case TagAction::retry:
        if(page==TagPage::error&&catalog_update_request)return request("community_update");
        if(page==TagPage::error&&community_request)return browse();
        if(phase=="write_recovery"){command["action"]="preview";command["mode"]="rewrite";command["spool_id"]=view["spool_id"];break;}
        command["action"]=phase=="association_pending"?"retry_association":phase=="clear_recovery"?"clear_preview":"retry_unlink";break;
      case TagAction::import:command["action"]="import";command["import_token"]=view["import_token"];break;
      case TagAction::community_update:return request("community_update");
      case TagAction::create:
        if(initial<=0||remaining<0||remaining>initial||tare<0||initial>100000||tare>100000){fail("Check spool weights before creating");return {};}
        command["action"]="create_spool";command["spool"]["filament_id"]=selected["id"];
        command["spool"]["initial_weight"]=initial;command["spool"]["remaining_weight"]=remaining;command["spool"]["spool_weight"]=tare;break;
      case TagAction::back:
        if(page==TagPage::selected||page==TagPage::create)page=TagPage::catalog;
        else if(page==TagPage::move)page=move_return;
        else if(page==TagPage::catalog)page=TagPage::sources;
        else if(page==TagPage::review)page=review_return;
        else if(page==TagPage::import_review)page=TagPage::selected;
        else page=TagPage::tag;
        return {};
      default:return {};
    }
    return encode(command);
  }
  TagScreen screen() const;
 private:
  void weights() {initial=selected["weight"]|1000.;if(initial<=0)initial=1000;remaining=initial;tare=selected["spool_weight"]|0.;}
  std::string encode(JsonDocument& command) {
    if(command.overflowed()||measureJson(command)>4096){fail("Command exceeds station limits");return {};}
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
inline TagScreen TagFlow::screen() const {
  TagScreen s; s.title="Manage tag";
  const auto action=[&](int n,const char* label,TagAction a){s.button({16,static_cast<std::int16_t>(112+n*50),448,44},label,a);};
  const auto back=[&]{s.button({8,266,140,46},"BACK",TagAction::back);};
  const auto primary=[&](const char* text,TagAction a){s.button({156,266,316,46},text,a);};
  switch(page) {
    case TagPage::tag:
      if(phase=="unlink_pending"||phase=="association_pending"||phase=="clear_recovery"||phase=="write_recovery") {
        s.title=phase=="write_recovery"?"Resume tag write":phase=="clear_recovery"?"Resume tag clear":phase=="association_pending"?"Finish linking":"Finish cleanup";
        s.body=message;action(2,phase=="write_recovery"?"RESUME WRITE":phase=="clear_recovery"?"RESUME CLEAR":phase=="association_pending"?"RETRY LINK":"RETRY CLEANUP",TagAction::retry);
      } else if(lifecycle==services::TagLifecycle::blank_compatible) {
        s.body="BLANK TAG\nReady to use\n"+uid;action(0,"ASSIGN TAG",TagAction::sources);
      } else if(lifecycle==services::TagLifecycle::linked) {
        s.body=material+"\nTag valid - Spool #"+std::to_string(current_spool);
        action(0,"UPDATE TAG",TagAction::update);action(1,"REASSIGN",TagAction::sources);action(2,"CLEAR / REUSE",TagAction::clear);
      } else if(lifecycle==services::TagLifecycle::unlinked) {
        s.body=material+"\nValid OpenPrintTag - not linked";
        action(0,"LINK TO A SPOOL",TagAction::sources);action(1,"REASSIGN",TagAction::sources);action(2,"CLEAR / REUSE",TagAction::clear);
      } else s.body=lifecycle==services::TagLifecycle::no_tag?"PLACE A TAG\nSet an NFC tag on the reader.":lifecycle==services::TagLifecycle::unsupported?"Tag needs attention\n"+message:"Reading tag…\nKeep the tag on the reader.";
      s.button({8,266,464,46},"DONE",TagAction::home);break;
    case TagPage::sources:
      s.title=from_spool?"Reassign tag":"Assign tag";s.body="How do you want to choose the filament?";
      action(0,"MY SPOOLS",TagAction::spools);action(1,"MY FILAMENTS",TagAction::filaments);if(config::community_enabled)action(2,"COMMUNITY",TagAction::community);back();break;
    case TagPage::catalog:
      s.title=entity=="spool"?"My Spools":entity=="filament"?"My Filaments":"Community";
      if(entity=="community"&&catalog_state!="ready") {s.title="Community Catalog";s.body=catalog_state=="damaged"?"Catalog damaged\nRedownload to restore local search.":"Not installed\nDownload once for offline search.";action(1,catalog_state=="damaged"?"REDOWNLOAD CATALOG":"DOWNLOAD CATALOG",TagAction::community_update);back();break;}
      s.button({326,4,146,44},"SEARCH",TagAction::search);
      if(catalog_page["items"].size()==0)s.body=query.empty()?(entity=="community"?"Community Catalog\n"+catalog_version+"\nReady\nSearch for a filament":"Search for a filament"):"No matches. Try another search.";
      for(unsigned i=0;i<3&&row+i<catalog_page["items"].size();++i) {
        auto item=catalog_page["items"][row+i];std::string text=filament_name(item);
        if(entity=="spool")text="#"+std::to_string(item["id"].as<int>())+" "+text+"\n"+std::to_string(static_cast<int>(item["remaining_weight"]|0.))+" g remaining";
        else text+="\n"+std::string(item["material"]|"")+"  "+std::to_string(static_cast<int>(item["weight"]|0.))+" g";
        s.button({8,static_cast<std::int16_t>(58+i*62),464,56},text,static_cast<TagAction>(static_cast<int>(TagAction::row0)+i));
      }
      s.button({8,266,140,46},"PREVIOUS PAGE",TagAction::previous,row>0||offset>0);
      s.button({156,266,168,46},"SOURCES",TagAction::back);
      s.button({332,266,140,46},"NEXT PAGE",TagAction::next,row+3<catalog_page["items"].size()||(catalog_page["has_more"]|false));break;
    case TagPage::selected:
      s.title=entity=="spool"?"Use this spool?":"Use this filament?";s.body=filament_name(selected);
      if(entity=="spool")s.body+="\nSpool #"+std::to_string(selected["id"].as<int>())+" - "+std::to_string(static_cast<int>(selected["remaining_weight"]|0.))+" g remaining";
      else s.body+="\n"+std::string(selected["material"]|"")+" - "+std::to_string(selected["diameter"]|1.75)+" mm";
      if(entity=="community") {
        s.button({16,212,448,44},"EDIT DISPLAY NAME",TagAction::name);
        if(!import_name.empty())s.body+="\nSpoolman name: "+import_name;
      }
      back();primary(entity=="community"?"REVIEW IMPORT":entity=="filament"?"CREATE A SPOOL":"USE SPOOL",TagAction::use);break;
    case TagPage::create:
      s.title="Create physical spool";s.body=filament_name(selected);
      s.button({16,112,448,44},"Initial: "+weight_text(initial)+" g  -  EDIT",TagAction::initial);
      s.button({16,162,448,44},"Remaining: "+weight_text(remaining)+" g  -  EDIT",TagAction::remaining);
      s.button({16,212,448,44},"Empty spool: "+weight_text(tare)+" g  -  EDIT",TagAction::tare);
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
    case TagPage::review:
      s.title=phase=="clear_preview"?"Reuse this NFC tag?":"Ready to write";
      s.body=phase=="clear_preview"?"Remove old filament information and links.\nThe permanent NFC identifier stays unchanged.":filament_name(view["spool"])+"\nSpool #"+std::to_string(view["spool_id"].as<int>())+"\nKeep the tag on the reader.";
      back();primary(phase=="clear_preview"?"CONFIRM CLEAR":"WRITE TAG",TagAction::write);break;
    case TagPage::progress: {
      if(phase=="catalog_downloading") {s.title="Downloading Community catalog";s.body=std::to_string(view["completed_blocks"]|0)+"%\nThe station remains available.";break;}
      if(community_request&&(phase=="searching"||phase=="queued")) {s.title="Searching Community…";s.body=query+"\nSearching catalog. Please wait…";break;}
      const int spool_id=view["spool_id"].is<int>()
          ? view["spool_id"].as<int>()
          : selected["id"].as<int>();
      const unsigned done=view["completed_blocks"]|0;
      const unsigned total=view["total_blocks"]|0;
      s.title=phase=="reading"?"Reading tag":
              phase=="loading_spool"?"Loading Spoolman":
              phase=="writing"?"Writing tag":
              phase=="verifying"?"Verifying tag":
              phase=="associating"?"Linking Spoolman":
              phase=="validating"?"Checking tag":
              "Preparing tag";
      s.body=message.empty()?"Working on this tag…":message;
      if(spool_id>0)s.body+="\n\nSpool #"+std::to_string(spool_id);
      if(total>0)s.body+="\nProgress  "+std::to_string(done)+" / "+std::to_string(total)+" blocks";
      else if(phase=="reading"||phase=="loading_spool"||phase=="queued")
        s.body+="\nReading identity and safety state…";
      const char* hold=phase=="associating"?"TAG VERIFIED - FINISHING LINK":"KEEP TAG ON READER";
      s.button({16,210,448,46},hold,TagAction::none,false);
      break;
    }
    case TagPage::reuse:
      s.title="TAG READY TO REUSE";s.body="The old filament information is removed.\nWhat should this tag become?";
      action(0,"CHOOSE EXISTING SPOOL",TagAction::spools);action(1,"CREATE NEW SPOOL",TagAction::sources);
      s.button({8,266,464,46},"DONE",TagAction::home);break;
    case TagPage::ready:
      s.title="TAG READY";s.body=filament_name(view["spool"])+"\nSpool #"+std::to_string(view["spool_id"].as<int>())+" - verified";
      action(0,"WEIGH",TagAction::weigh);action(1,"ASSIGN TO PRINTER",TagAction::printer);
      // Wait for the internally refreshed physical/workflow identity before
      // navigating to pages that operate on the active spool.
      s.buttons[0].enabled=s.buttons[1].enabled=current_spool>0&&current_spool==view["spool_id"].as<int>();
      if(!s.buttons[0].enabled)s.body+="\nRefreshing the active spool…";
      s.button({8,266,464,46},"DONE",TagAction::home);break;
    case TagPage::error:
      s.title="Needs attention";s.body=message;back();
      if(community_request||catalog_update_request)primary("RETRY",TagAction::retry);break;
  }
  if(waiting&&page!=TagPage::progress) {s.title="Please wait";s.body="Working on your request…";s.count=0;}
  return s;
}
}  // namespace opentag::ui
