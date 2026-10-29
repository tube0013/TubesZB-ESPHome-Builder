#include "components/efr32_flasher/ezsp_bootloader.h"
#include <cassert>
using namespace esphome::efr32_flasher::ezsp_bootloader;
struct Transport {
  int calls{0}, fail{-1}; bool reset_ok{true}; uint8_t version{13}, status{0}; bool installed{true}, wide{false};
  bool reset() { return reset_ok; }
  bool command(bool extended,uint8_t seq,uint16_t id,const std::vector<uint8_t>&payload,std::vector<uint8_t>&reply) {
    const int n=calls++; if(n==fail)return false;
    assert(seq==n);
    if(n==0){assert(!extended&&id==0&&payload==std::vector<uint8_t>{4});reply={version,2,0x43,0x74};}
    if(n==1){assert(extended&&id==0&&payload==std::vector<uint8_t>{version});reply={version,2,0x43,0x74};}
    if(n==2){assert(extended&&id==0x91&&payload.empty());reply={uint8_t(installed?0:255),uint8_t(installed?1:255),0,0,0};}
    if(n==3){assert(extended&&id==0x8F&&payload==std::vector<uint8_t>{1});reply=wide?std::vector<uint8_t>{status,0,0,0}:std::vector<uint8_t>{status};}
    return true;
  }
};
int main(){
  assert(request(false,0,0,{4})==std::vector<uint8_t>({0,0,0,4}));
  assert(request(true,3,0x8F,{1})==std::vector<uint8_t>({3,0,1,0x8F,0,1}));
  std::vector<uint8_t> out;
  assert(response({3,0x80,1,0x8F,0,0},true,3,0x8F,out)&&out==std::vector<uint8_t>{0});
  assert(!response({2,0x80,1,0x8F,0,0},true,3,0x8F,out)); // stale sequence
  assert(!response({3,0,1,0x8F,0,0},true,3,0x8F,out)); // command echoed, not a response
  assert(!response({3,0x80,1,0x58,0,0},true,3,0x8F,out)); // invalid-command reply
  assert(!response({3,0x80,0,0x8F,0,0},true,3,0x8F,out)); // wrong header format
  assert(!response({3,0x80,1,0x8F},true,3,0x8F,out));
  Transport t;assert(launch(t)==Result::LAUNCHED&&t.calls==4);
  t={};t.wide=true;assert(launch(t)==Result::LAUNCHED);
  t={};t.reset_ok=false;assert(launch(t)==Result::ASH_FAILED&&t.calls==0);
  t={};t.fail=0;assert(launch(t)==Result::VERSION_FAILED&&t.calls==1);
  t={};t.version=4;assert(launch(t)==Result::UNSUPPORTED_VERSION&&t.calls==1);
  t={};t.fail=1;assert(launch(t)==Result::VERSION_FAILED&&t.calls==2);
  t={};t.installed=false;assert(launch(t)==Result::NO_BOOTLOADER&&t.calls==3);
  t={};t.status=1;assert(launch(t)==Result::LAUNCH_FAILED);
  t={};t.fail=3;assert(launch(t)==Result::LAUNCH_FAILED);
  Menu menu;for(auto c:std::string("noise\r\nGecko Bootloader v1.12.0\r\n1. upload gbl\r\nBL >"))menu.feed(c);
  assert(menu.feed('\n'));
  Menu fake;for(auto c:std::string("EZSP ready C BL >"))assert(!fake.feed(c));
  Menu header_only;for(auto c:std::string("Gecko Bootloader v1.12.0"))assert(!header_only.feed(c));
}
