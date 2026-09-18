#pragma once
#include <cctype>
#include <cmath>
#include "network/backend_json.hpp"
#include "network/http_transport.hpp"
#include "network/community_ca.hpp"

namespace opentag::services {
// A streaming JSON array recognizer. Retains one <=8 KiB object and eight
// results in PSRAM, never the catalog. Escape/depth handling crosses chunks.
class CommunityPage {
 public:
  static constexpr std::size_t page_size=8, object_limit=8192;
  network::BackendDocument page;
  std::string error;
  CommunityPage(std::string query, unsigned offset):query_(lower(query)),offset_(offset) {
    page["items"].to<JsonArray>(); page["offset"]=offset; page["has_more"]=false;
  }
  bool feed(const std::uint8_t* data, std::size_t size) {
    for(std::size_t i=0;i<size;++i) {
      const char c=static_cast<char>(data[i]);
      if(depth_) {
        if(!object_.append(&c,1))return fail("Community record exceeds 8 KiB bound");
        if(quoted_) { if(escaped_)escaped_=false; else if(c=='\\')escaped_=true; else if(c=='"')quoted_=false; }
        else if(c=='"')quoted_=true;
        else if(c=='{'||c=='[') { if(++depth_>12)return fail("Community nesting exceeds bound"); }
        else if(c=='}'||c==']') { if(--depth_==0) { if(!record())return false; state_=State::separator; } }
      } else if(std::isspace(static_cast<unsigned char>(c)))continue;
      else if(state_==State::start && c=='[')state_=State::first;
      else if((state_==State::first||state_==State::value)&&c=='{') {
        object_.release(); object_.append(&c,1); depth_=1;
      } else if((state_==State::first||state_==State::separator)&&c==']')state_=State::done;
      else if(state_==State::separator&&c==',')state_=State::value;
      else return fail("Malformed Community JSON array");
    }
    return true;
  }
  bool finish() {
    if(state_!=State::done||depth_)return fail("Truncated Community catalog");
    page["next_offset"]=offset_+page["items"].size();
    return !page.overflowed() && error.empty();
  }
 private:
  enum class State { start,first,value,separator,done } state_{State::start};
  network::ResponseBody object_{object_limit};
  std::string query_;
  unsigned offset_,matches_{0},records_{0};
  unsigned depth_{0}; bool quoted_{false},escaped_{false};
  bool fail(const char* why){error=why;return false;}
  static std::string lower(std::string s) {
    for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s;
  }
  bool record() {
    if(++records_>100000)return fail("Community record count exceeds bound");
    JsonDocument decoded(&network::backend_json_allocator);
    if(deserializeJson(decoded,static_cast<const char*>(object_.data()),object_.size(),DeserializationOption::NestingLimit(12)))return fail("Invalid Community record");
    object_.release();
    auto item=decoded.as<JsonObjectConst>();
    if(item.isNull())return fail("Invalid Community record schema");
    for(auto key:{"id","manufacturer","name","material"})
      if(!item[key].is<const char*>()||!*item[key].as<const char*>()||
         item[key].as<JsonString>().size()>(std::string_view(key)=="id"?180U:128U))
        return fail("Invalid Community identity");
    for(auto key:{"density","diameter"})
      if(!item[key].is<double>()||!std::isfinite(item[key].as<double>())||item[key].as<double>()<=0)
        return fail("Invalid Community physical properties");
    const auto text=lower(std::string(item["manufacturer"].as<const char*>())+" "+
        item["name"].as<const char*>()+" "+item["material"].as<const char*>());
    std::size_t begin=0;
    while(begin<query_.size()) {
      const auto end=query_.find(' ',begin); const auto term=query_.substr(begin,end-begin);
      if(!term.empty()&&text.find(term)==std::string::npos)return true;
      if(end==std::string::npos)break; begin=end+1;
    }
    if(matches_++<offset_)return true;
    if(page["items"].size()<page_size) {
      if(measureJson(item)>2048)return fail("Matching Community record exceeds import page bound");
      page["items"].as<JsonArray>().add(item);
    }
    else page["has_more"]=true;
    return !page.overflowed();
  }
};
inline core::Result<network::BackendDocument> search_community(
    network::IHttpTransport& transport,const std::string& query,unsigned offset) {
  using Result=core::Result<network::BackendDocument>;
  if(query.empty()||query.size()>64||offset>100000)
    return Result::failure({core::ErrorCategory::configuration,"Enter a Community search of 1–64 characters",false});
  auto stream=network::make_external<CommunityPage>([&]{return CommunityPage(query,offset);});
  if(!stream)return Result::failure({core::ErrorCategory::backend_unavailable,"Community search memory unavailable",true});
  network::HttpRequest request;
  request.url="https://icezaza2543.github.io/SpoolmanDB-Community/filaments.json";
  request.ca_certificate_pem=network::community_ca;
  request.maximum_stream_bytes=64U*1024U*1024U;
  request.accept_gzip=true;
  request.response_consumer=[&](const std::uint8_t* data,std::size_t size){return stream->feed(data,size);};
  const auto response=transport.perform(request);
  if(!stream->error.empty())return Result::failure({core::ErrorCategory::invalid_response,stream->error,false});
  if(!response.ok())return Result::failure(response.error());
  if(response.value().status_code!=200||response.value().content_type.find("json")==std::string::npos||!stream->finish())
    return Result::failure({core::ErrorCategory::invalid_response,"Community response incomplete or unsupported",true});
  return Result::success(std::move(stream->page));
}
}  // namespace opentag::services
