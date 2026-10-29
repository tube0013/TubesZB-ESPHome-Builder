#pragma once
#include <cstdlib>
#include <string>

namespace esphome::tubeszb_web {
struct CC2652LogEvent {
  enum Kind { NONE, CHECK_START, FLASH_START, PROBE_START, FLASH_OK, PROBE_OK, PROBE_FAILED,
              BOOTLOADER_START, BOOTLOADER_READY, PROGRESS } kind{NONE};
  int progress{-1};
};

inline CC2652LogEvent decode_cc2652_log(const std::string &line) {
  using E = CC2652LogEvent;
  if (line.find("Starting firmware update") != std::string::npos) return {E::FLASH_START};
  if (line.find("Checking CC2652 firmware against manifest") != std::string::npos) return {E::CHECK_START};
  if (line.find("Detecting CC2652 variant via ROM bootloader") != std::string::npos) return {E::PROBE_START};
  if (line.find("Update finished OK.") != std::string::npos) return {E::FLASH_OK};
  if (line.find("Variant detection: CC2652P2") != std::string::npos || line.find("Variant detection: CC2652P7") != std::string::npos) return {E::PROBE_OK};
  if (line.find("Variant detection failed") != std::string::npos || line.find("Variant detection inconclusive") != std::string::npos) return {E::PROBE_FAILED};
  if (line.find("Manual bootloader requested.") != std::string::npos) return {E::BOOTLOADER_START};
  if (line.find("Manual bootloader ready.") != std::string::npos) return {E::BOOTLOADER_READY};
  const auto start = line.find("Progress: ");
  if (start != std::string::npos) {
    const char *digits = line.c_str() + start + 10;
    char *end = nullptr;
    const unsigned long value = std::strtoul(digits, &end, 10);
    if (end != digits && *end == '%' && value <= 100) return {E::PROGRESS, static_cast<int>(value)};
  }
  return {};
}
}  // namespace esphome::tubeszb_web
