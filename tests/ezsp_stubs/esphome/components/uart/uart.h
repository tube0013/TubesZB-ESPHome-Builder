#pragma once
#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
namespace esphome::uart {
class UARTComponent {
 public:
  std::deque<uint8_t> rx;
  std::function<void(uint8_t)> on_write;
  int available(){ return rx.size(); }
  bool read_byte(uint8_t *b){ if(rx.empty())return false;*b=rx.front();rx.pop_front();return true; }
  void write_byte(uint8_t b){ if(on_write)on_write(b); }
  void write_array(const uint8_t *p,size_t size){ for(size_t i=0;i<size;i++)write_byte(p[i]); }
};
}
