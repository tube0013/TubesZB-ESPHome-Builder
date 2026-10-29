#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::efr32_flasher::ezsp_bootloader {
inline std::vector<uint8_t> request(bool extended, uint8_t seq, uint16_t id,
                                    const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> out{seq, 0x00};
  if (extended) { out.push_back(0x01); out.push_back(id & 0xFF); out.push_back(id >> 8); }
  else out.push_back(id & 0xFF);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}
inline bool response(const std::vector<uint8_t> &in, bool extended, uint8_t seq, uint16_t id,
                     std::vector<uint8_t> &payload) {
  const size_t header = extended ? 5 : 3;
  if (in.size() < header || in[0] != seq || (in[1] & 0x80) == 0) return false;
  if (extended && (in[2] & 0x03) != 0x01) return false;
  const uint16_t got = extended ? uint16_t(in[3]) | (uint16_t(in[4]) << 8) : in[2];
  if (got != id) return false;
  payload.assign(in.begin() + header, in.end());
  return true;
}
enum class Result { LAUNCHED, ASH_FAILED, VERSION_FAILED, UNSUPPORTED_VERSION, NO_BOOTLOADER, LAUNCH_FAILED };
inline const char *describe(Result result) {
  switch (result) {
    case Result::LAUNCHED: return "EZSP launch acknowledged";
    case Result::ASH_FAILED: return "ASH reset did not synchronize";
    case Result::VERSION_FAILED: return "EZSP version negotiation failed";
    case Result::UNSUPPORTED_VERSION: return "EZSP protocol older than v8 is unsupported for software entry";
    case Result::NO_BOOTLOADER: return "standalone bootloader is absent or cannot be queried";
    default: return "EZSP launch was rejected or did not reply";
  }
}
// Transport owns ASH CRC/sequence/ACK handling; this sequence never erases or writes flash.
// A matching Gecko menu must additionally be observed at the bootloader baud.
template<class Transport> Result launch(Transport &transport) {
  if (!transport.reset()) return Result::ASH_FAILED;
  std::vector<uint8_t> reply;
  if (!transport.command(false, 0, 0x0000, {4}, reply) || reply.size() < 4)
    return Result::VERSION_FAILED;
  const uint8_t version = reply[0];
  if (version < 8) return Result::UNSUPPORTED_VERSION;
  if (!transport.command(true, 1, 0x0000, {version}, reply) || reply.size() < 4 || reply[0] != version)
    return Result::VERSION_FAILED;
  if (!transport.command(true, 2, 0x0091, {}, reply) || reply.size() < 5 ||
      (reply[0] == 0xFF && reply[1] == 0xFF)) return Result::NO_BOOTLOADER;
  // NORMAL_MODE is 1 (and is enabled=true on newer EZSP versions).
  if (!transport.command(true, 3, 0x008F, {1}, reply) || (reply.size() != 1 && reply.size() != 4) ||
      !std::all_of(reply.begin(), reply.end(), [](uint8_t b) { return b == 0; })) return Result::LAUNCH_FAILED;
  return Result::LAUNCHED;
}
class Menu {
 public:
  bool feed(uint8_t byte) {
    if (byte == '\r' || byte == '\n' || (byte >= 0x20 && byte <= 0x7E)) {
      text_.push_back(char(byte));
      if (text_.size() > 512) text_.erase(0, text_.size() - 512);
    }
    return text_.find("Gecko Bootloader") != std::string::npos &&
           (text_.find("BL >") != std::string::npos || text_.find("1. upload") != std::string::npos);
  }
 private:
  std::string text_;
};
}  // namespace esphome::efr32_flasher::ezsp_bootloader
