#include "components/efr32_flasher/ezsp_bootloader_uart.h"
#include <cassert>
using namespace esphome;
using namespace esphome::efr32_flasher;
namespace ash=esphome::efr32::ash;
struct NCP {
  uart::UARTComponent host,peer;
  ash::LinkState state{&peer};
  bool respond_reset{true}, installed{true}, reject{false}, corrupt{false}, duplicate{false}, nak{false}, retried{false}, flow{false}, oversized{false};
  int launches{0};std::vector<uint8_t> last;
  NCP(){
    peer.on_write=[this](uint8_t byte){ host.rx.push_back(byte); };
    host.on_write=[this](uint8_t byte){
      peer.rx.push_back(byte);
      if(byte!=ash::FLAG)return;
      ash::Frame frame;
      if(!ash::recv_frame(state,frame,1))return;
      if(frame.control==0xC0){
        state.reset_counters();
        if(respond_reset)ash::send_flagged_frame(&peer,{0xC1,2,0x0B});
        return;
      }
      if(frame.type!=ash::FrameType::DATA)return;
      const auto &p=frame.payload;
      assert(!p.empty());
      if(nak&&p[0]==1&&!retried){
        retried=true;ash::send_nak(state,(frame.control>>4)&7);return;
      }
      if(nak&&p[0]==1)assert((frame.control&8)!=0);
      if(duplicate&&p[0]==1)for(auto b:last)host.rx.push_back(b);
      std::vector<uint8_t> reply;
      if(p[0]==0){assert(p==std::vector<uint8_t>({0,0,0,4}));reply={0,0x80,0,13,2,0x43,0x74};}
      if(p[0]==1){assert(p==std::vector<uint8_t>({1,0,1,0,0,13}));reply={1,0x80,1,0,0,13,2,0x43,0x74};}
      if(p[0]==2){assert(p==std::vector<uint8_t>({2,0,1,0x91,0}));reply={2,0x80,1,0x91,0,uint8_t(installed?0:255),uint8_t(installed?1:255),0,0,0};}
      if(p[0]==3){assert(p==std::vector<uint8_t>({3,0,1,0x8F,0,1}));launches++;reply={3,0x80,1,0x8F,0,uint8_t(reject?1:0)};}
      std::vector<uint8_t> wire;
      peer.on_write=[&wire](uint8_t b){wire.push_back(b);};
      ash::send_data_frame(state,reply,state.next_ack);
      peer.on_write=[this](uint8_t b){host.rx.push_back(b);};
      last=wire;
      if(oversized&&p[0]==0){ for(int i=0;i<300;i++)host.rx.push_back(0x55);host.rx.push_back(ash::FLAG); }
      if(flow){
        // Raw XON/XOFF can interrupt ASH bytes without joining the CRC body.
        host.rx.push_back(ash::XOFF);
        for(auto b:wire){host.rx.push_back(b);host.rx.push_back(ash::XON);host.rx.push_back(ash::XOFF);}
        host.rx.push_back(ash::XON);return;
      }
      if(corrupt&&p[0]==0)wire[wire.size()-2]^=1;
      for(auto b:wire)host.rx.push_back(b);
    };
  }
};
int main(){
  using ezsp_bootloader::launch;using ezsp_bootloader::Result;
  {NCP n;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::LAUNCHED&&n.launches==1);}
  {NCP n;n.respond_reset=false;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::ASH_FAILED&&n.launches==0);}
  {NCP n;n.installed=false;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::NO_BOOTLOADER&&n.launches==0);}
  {NCP n;n.reject=true;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::LAUNCH_FAILED&&n.launches==1);}
  {NCP n;n.corrupt=true;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::VERSION_FAILED&&n.launches==0);}
  {NCP n;n.duplicate=true;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::LAUNCHED&&n.launches==1);}
  {NCP n;n.flow=true;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::LAUNCHED&&n.launches==1);}
  {NCP n;n.oversized=true;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::LAUNCHED&&n.launches==1);}
  {NCP n;n.nak=true;EZSPBootloaderUART t(&n.host);assert(launch(t)==Result::LAUNCHED&&n.retried&&n.launches==1);}
}
