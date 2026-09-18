#pragma once
#include <array>
#include <algorithm>
#include <functional>
#include "network/stream_disposition.hpp"
#include "network/miniz/miniz_tinfl.h"

namespace opentag::network {
// One RFC1952 member, bounded header, dictionary, expanded byte count, CRC and
// ISIZE. Allocate the entire decoder in PSRAM. Never buffer the response.
class GzipStream {
 public:
  using Sink=StreamConsumer;
  GzipStream(Sink sink,std::size_t maximum):sink_(std::move(sink)),maximum_(maximum){tinfl_init(&inflater_);}
  StreamDisposition feed(const std::uint8_t* data,std::size_t size) {
    if(disposition_!=StreamDisposition::next)return disposition_;
    while(size) {
      if(state_==State::header) {
        if(header_size_==header_.size())return disposition_=StreamDisposition::error;
        header_[header_size_++]=*data++;--size;
        if(header_size_<10)continue;
        if(header_[0]!=31||header_[1]!=139||header_[2]!=8||(header_[3]&0xe2))return disposition_=StreamDisposition::error;
        std::size_t end=10;
        if(header_[3]&4){if(header_size_<12)continue;end=12+header_[10]+256U*header_[11];if(end>header_.size())return disposition_=StreamDisposition::error;if(header_size_<end)continue;}
        if(header_[3]&8){while(end<header_size_&&header_[end])++end;if(end==header_size_)continue;++end;}
        if(header_[3]&16){while(end<header_size_&&header_[end])++end;if(end==header_size_)continue;++end;}
        if(header_size_==end)state_=State::deflate;
      } else if(state_==State::deflate) {
        std::size_t input=size,output=dictionary_.size()-cursor_;
        const auto status=tinfl_decompress(&inflater_,data,&input,dictionary_.data(),dictionary_.data()+cursor_,&output,TINFL_FLAG_HAS_MORE_INPUT);
        if(!input&&!output)return disposition_=StreamDisposition::error;
        if(output>maximum_-expanded_)return disposition_=StreamDisposition::error;
        for(std::size_t i=0;i<output;++i)crc_=crc_byte(crc_,dictionary_[cursor_+i]);
        expanded_+=output;
        // Deliver valid output before inspecting a later deflate error: the
        // caller may already have everything it requested in this prefix.
        if(output) {
          disposition_=sink_(dictionary_.data()+cursor_,output);
          if(disposition_!=StreamDisposition::next)return disposition_;
        }
        if(status<0)return disposition_=StreamDisposition::error;
        cursor_=(cursor_+output)%dictionary_.size();data+=input;size-=input;
        if(status==TINFL_STATUS_DONE)state_=State::trailer;
      } else if(state_==State::trailer) {
        trailer_[trailer_size_++]=*data++;--size;
        if(trailer_size_==8)state_=State::done;
      } else return disposition_=StreamDisposition::error; // Reject concatenated members/trailing bytes.
    }
    return StreamDisposition::next;
  }
  bool finish() const {return disposition_==StreamDisposition::complete || (disposition_!=StreamDisposition::error && state_==State::done&&read32(trailer_.data())==(crc_^0xffffffffU)&&read32(trailer_.data()+4)==expanded_);}
 private:
  static std::uint32_t crc_byte(std::uint32_t crc,std::uint8_t b) {
    // Nibble table avoids 8 branches per expanded byte and stays tiny.
    static constexpr std::uint32_t table[]{0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    crc^=b;crc=(crc>>4)^table[crc&15];return (crc>>4)^table[crc&15];
  }
  static std::uint32_t read32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
  enum class State {header,deflate,trailer,done} state_{State::header};
  StreamDisposition disposition_{StreamDisposition::next};
  Sink sink_;std::size_t maximum_,cursor_{0},expanded_{0},header_size_{0},trailer_size_{0};
  std::uint32_t crc_{0xffffffffU};
  tinfl_decompressor inflater_{};
  std::array<std::uint8_t,32768> dictionary_{};
  std::array<std::uint8_t,1024> header_{};
  std::array<std::uint8_t,8> trailer_{};
};
static_assert(sizeof(GzipStream)<=48U*1024U,"Gzip workspace exceeds PSRAM budget");
}
