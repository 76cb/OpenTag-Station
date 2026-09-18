#pragma once
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include "ui/product_layout.hpp"

namespace opentag::ui {
enum class InputMode { text, numeric, url, password };
struct InputSpec {
  InputMode mode{InputMode::text};
  std::string title, initial, unit, action{"DONE"};
  std::size_t maximum_length{64};
  bool decimal{true}, required{false};
  double minimum{0}, maximum{100000};
};
struct InputKey { layout::Box box{}; const char* text{""}; };
// Fixed geometry shared by native layout tests and the LVGL component.
struct InputKeys {
  std::array<InputKey, 36> keys{};
  std::size_t count{0};
  void add(layout::Box box, const char* text) { keys[count++] = {box, text}; }
};
inline InputKeys input_keys(InputMode mode, bool symbols, bool decimal) {
  InputKeys out;
  if (mode == InputMode::numeric) {
    static const char* keys[]{"1","2","3","4","5","6","7","8","9",".","0","DEL"};
    for (int i=0; i<12; ++i) {
      if (i==9 && !decimal) continue;
      out.add({static_cast<std::int16_t>(64+(i%3)*118),
               static_cast<std::int16_t>(66+(i/3)*49),112,44}, keys[i]);
    }
    out.add({8,266,226,46},"CANCEL"); out.add({242,266,230,46},"ACTION");
    return out;
  }
  out.add({382,4,90,44},"CANCEL");
  out.add({282,4,94,44},symbols?"MORE":"SHIFT");
  static const char* letters[]{"Q","W","E","R","T","Y","U","I","O","P",
      "A","S","D","F","G","H","J","K","L"};
  static const char* numbers[]{"1","2","3","4","5","6","7","8","9","0",
      "+","-","_",".","/",":","#","@","!"};
  for(int i=0;i<19;++i) out.add({static_cast<std::int16_t>((i<10?6:29)+(i<10?i:i-10)*47),
      static_cast<std::int16_t>(i<10?104:154),44,44}, symbols?numbers[i]:letters[i]);
  static const char* lower[]{"Z","X","C","V","B","N","M","DEL"};
  static const char* punctuation[]{"$","%","&","*","(",")","=","DEL"};
  for(int i=0;i<8;++i) out.add({static_cast<std::int16_t>(8+i*47),204,
      static_cast<std::int16_t>(i==7?135:44),44},symbols?punctuation[i]:lower[i]);
  out.add({8,254,68,48},symbols?"ABC":"123");
  if(mode==InputMode::url) {
    out.add({82,254,44,48},"/"); out.add({132,254,44,48},".");
    out.add({182,254,44,48},":"); out.add({232,254,44,48},"-");
    out.add({282,254,190,48},"ACTION");
  } else {
    out.add({82,254,228,48},"SPACE"); out.add({316,254,156,48},"ACTION");
  }
  return out;
}
class TouchInput {
 public:
  InputSpec spec;
  std::string value;
  bool symbols{false}, shifted{false}, more{false};
  void open(InputSpec input) {
    spec=std::move(input); value=spec.initial; symbols=shifted=more=false;
  }
  bool valid() const {
    if(value.size()>spec.maximum_length || (spec.required&&value.empty()))return false;
    if(spec.mode!=InputMode::numeric)return true;
    if(value.empty())return false;
    if(!spec.decimal&&value.find('.')!=std::string::npos)return false;
    char* end=nullptr; const double n=std::strtod(value.c_str(),&end);
    return end && *end=='\0' && std::isfinite(n) && n>=spec.minimum && n<=spec.maximum;
  }
  void press(std::string_view key) {
    if(key=="DEL") {
      if(!value.empty()) {
        auto last=value.size()-1;
        while(last>0 && (static_cast<unsigned char>(value[last])&0xc0)==0x80)--last;
        value.erase(last);
      }
      return;
    }
    if(key=="SHIFT") { shifted=!shifted; return; }
    if(key=="123"||key=="ABC") { symbols=!symbols; more=false; return; }
    if(key=="MORE") { more=!more; return; }
    if(key=="CANCEL"||key=="ACTION")return;
    std::string add=key=="SPACE"?" ":std::string(key);
    if(!shifted && !symbols && add.size()==1 && add[0]>='A'&&add[0]<='Z')add[0]+='a'-'A';
    if(spec.mode==InputMode::numeric && add=="." && (!spec.decimal||value.find('.')!=std::string::npos))return;
    if(value.size()+add.size()<=spec.maximum_length)value+=add;
  }
};
}  // namespace opentag::ui
