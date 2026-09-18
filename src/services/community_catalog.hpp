#pragma once
#include <cctype>
#include <cmath>
#include "network/backend_json.hpp"
#include "network/http_transport.hpp"
#include "network/community_ca.hpp"

namespace opentag::services {
// Sample while TLS, inflater and parser are simultaneously alive. Boundary
// logs alone miss the internal-memory trough; these are scan-local minima.
class CommunityRuntime {
 public:
  void sample() {
#ifdef ARDUINO
    const auto heap=network::backend_heap();
    internal_=std::min(internal_,heap.free_internal);
    largest_=std::min(largest_,heap.largest_internal);
    psram_=std::min(psram_,static_cast<std::size_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
#endif
  }
  ~CommunityRuntime() {
#ifdef ARDUINO
    // The watermark persists after unwinding; scan it once, not every 4 KiB.
    stack_=uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("COMMUNITY sampled_internal_min=%u sampled_largest_min=%u sampled_psram_min=%u backend_stack_min=%u\n",
      static_cast<unsigned>(internal_),static_cast<unsigned>(largest_),
      static_cast<unsigned>(psram_),static_cast<unsigned>(stack_));
#endif
  }
 private:
  std::size_t internal_{SIZE_MAX},largest_{SIZE_MAX},psram_{SIZE_MAX},stack_{SIZE_MAX};
};
// A streaming JSON array recognizer. Retains one <=8 KiB object and eight
// results in PSRAM, never the catalog. Escape/depth handling crosses chunks.
class CommunityPage {
 public:
  static constexpr std::size_t page_size=8, object_limit=8192;
  network::BackendDocument page;
  std::string error;
  using Cooperation = std::function<void()>;
  static constexpr std::size_t cooperation_bytes=4096;
  bool page_complete() const {return complete_;}
  unsigned records_processed() const {return records_;}
  static void cooperate() {
#ifdef ARDUINO
    // A tick of real blocking lets IDLE0 run; taskYIELD alone cannot do that.
    vTaskDelay(1);
#endif
  }
  CommunityPage(std::string query, unsigned offset, Cooperation cooperation=cooperate):query_(lower(query)),offset_(offset),cooperation_(std::move(cooperation)) {
    page["items"].to<JsonArray>(); page["offset"]=offset; page["has_more"]=false;
  }
  network::StreamDisposition feed(const std::uint8_t* data, std::size_t size) {
    using network::StreamDisposition;
    if(!error.empty())return StreamDisposition::error;
    if(complete_)return StreamDisposition::complete;
    for(std::size_t i=0;i<size;++i) {
      if(++since_cooperation_==cooperation_bytes) {
        since_cooperation_=0; if(cooperation_)cooperation_();
      }
      const char c=static_cast<char>(data[i]);
      if(depth_) {
        if(!object_.append(&c,1))return stream_fail("Community record exceeds 8 KiB bound");
        if(quoted_) { if(escaped_)escaped_=false; else if(c=='\\')escaped_=true; else if(c=='"')quoted_=false; }
        else if(c=='"')quoted_=true;
        else if(c=='{'||c=='[') { if(++depth_>12)return stream_fail("Community nesting exceeds bound"); }
        else if(c=='}'||c==']') { if(--depth_==0) { if(!record())return StreamDisposition::error; if(complete_)return StreamDisposition::complete; state_=State::separator; } }
      } else if(std::isspace(static_cast<unsigned char>(c)))continue;
      else if(state_==State::start && c=='[')state_=State::first;
      else if((state_==State::first||state_==State::value)&&c=='{') {
        object_.clear(); if(!object_.append(&c,1))return stream_fail("Community record memory unavailable"); depth_=1;
      } else if((state_==State::first||state_==State::separator)&&c==']')state_=State::done;
      else if(state_==State::separator&&c==',')state_=State::value;
      else return stream_fail("Malformed Community JSON array");
    }
    return StreamDisposition::next;
  }
  bool finish() {
    if(!complete_&&(state_!=State::done||depth_))return fail("Truncated Community catalog");
    page["next_offset"]=offset_+page["items"].size();
    return !page.overflowed() && error.empty();
  }
 private:
  enum class State { start,first,value,separator,done } state_{State::start};
  network::ResponseBody object_{object_limit};
  std::string query_;
  unsigned offset_,matches_{0},records_{0};
  Cooperation cooperation_;
  std::size_t since_cooperation_{0};
  bool complete_{false};
  network::StreamDisposition stream_fail(const char* why) {fail(why);return network::StreamDisposition::error;}
  unsigned depth_{0}; bool quoted_{false},escaped_{false};
  bool fail(const char* why){error=why;return false;}
  static std::string lower(std::string s) {
    for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s;
  }
  bool record() {
    if(++records_>100000)return fail("Community record count exceeds bound");
    JsonDocument decoded(&network::backend_json_allocator);
    if(deserializeJson(decoded,static_cast<const char*>(object_.data()),object_.size(),DeserializationOption::NestingLimit(12)))return fail("Invalid Community record");
    object_.clear();
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
    else {page["has_more"]=true;page["next_offset"]=offset_+page_size;complete_=true;}
    return page.overflowed()?fail("Community page exceeds memory bound"):true;
  }
};
inline core::Result<network::BackendDocument> search_community(
    network::IHttpTransport& transport,const std::string& query,unsigned offset) {
  using Result=core::Result<network::BackendDocument>;
  if(query.empty()||query.size()>64||offset>100000)
    return Result::failure({core::ErrorCategory::configuration,"Enter a Community search of 1–64 characters",false});
#ifdef ARDUINO
  const auto started=millis();
#else
  const std::uint32_t started=0;
#endif
  network::BackendPhaseGuard released{"community_released",started};
  network::backend_memory_phase("community_begin",0,0,started);
  CommunityRuntime runtime;runtime.sample();
  auto stream=network::make_external<CommunityPage>([&]{return CommunityPage(query,offset,[&]{
    runtime.sample();CommunityPage::cooperate();
  });});
  if(!stream)return Result::failure({core::ErrorCategory::backend_unavailable,"Community search memory unavailable",true});
  network::HttpRequest request;
  request.url="https://icezaza2543.github.io/SpoolmanDB-Community/filaments.json";
  request.ca_certificate_pem=network::community_ca;
  request.maximum_stream_bytes=64U*1024U*1024U;
  request.accept_gzip=true;
  request.community_search=true;
  request.response_consumer=[&](const std::uint8_t* data,std::size_t size){runtime.sample();return stream->feed(data,size);};
  const auto response=transport.perform(request);
  if(!stream->error.empty())return Result::failure({core::ErrorCategory::invalid_response,stream->error,false});
  if(!response.ok())return Result::failure(response.error());
  if(response.value().status_code!=200||response.value().content_type.find("json")==std::string::npos||!stream->finish())
    return Result::failure({core::ErrorCategory::invalid_response,"Community response incomplete or unsupported",true});
  return Result::success(std::move(stream->page));
}
}  // namespace opentag::services
