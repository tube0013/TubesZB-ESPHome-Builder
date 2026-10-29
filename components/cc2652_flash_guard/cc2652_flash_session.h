#pragma once
#include <cstring>

namespace esphome::cc2652_flash_guard {
// This records native log results only. The YAML wait runs on the main loop,
// after the synchronous native flasher has returned and restored its UART baud.
struct FlashSession {
  bool active{false}, started{false}, completed{false}, success{false}, resume_after{false};

  bool begin(bool already_paused) {
    if (active) return false;
    active = true; started = completed = success = false;
    resume_after = !already_paused;
    return true;
  }
  void observe(bool error, const char *message) {
    if (!active) return;
    if (std::strstr(message, "Starting firmware update") != nullptr) started = true;
    if (!started) return;
    // An error can be retried inside the native method. Do not resume here;
    // wait_until cannot run until the whole method, including cleanup, returns.
    if (error) completed = true;
    if (std::strstr(message, "Update finished OK.") != nullptr) {
      completed = true; success = true;
    }
  }
  void end() { active = false; }
};
}  // namespace esphome::cc2652_flash_guard
