#pragma once
#include <cstdint>
namespace esphome::zigbee_firmware_baud {
inline bool valid_baud(uint32_t baud) { return baud >= 9600 && baud <= 921600; }
}
