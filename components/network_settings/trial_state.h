#pragma once

#include "network_config.h"

namespace esphome::network_settings {

enum class Phase : uint32_t { CONFIRMED = 0, STAGED = 1, TESTING = 2 };

struct SavedState {
  uint32_t magic{0x545A4E31};  // TZN1; changing the storage layout requires a new magic/key.
  Phase phase{Phase::CONFIRMED};
  NetworkConfig confirmed{};
  NetworkConfig candidate{};
};
static_assert(sizeof(SavedState) == 56, "Changing the saved record layout requires a new NVS key/version");

inline bool valid_saved_state(const SavedState &state) {
  return state.magic == 0x545A4E31 && uint32_t(state.phase) <= uint32_t(Phase::TESTING) &&
         validate_config(state.confirmed) == nullptr && validate_config(state.candidate) == nullptr;
}

// Persistence is injected so power loss, failed writes, and boot transitions can
// be tested without an ESP32. Never update the live record before flash commits.
class TrialState {
 public:
  static constexpr uint32_t TIMEOUT_MS = 120000;
  SavedState saved{};
  NetworkConfig active{};
  bool testing{false};
  uint32_t started_at{0};

  template<typename Save> bool boot(uint32_t now, Save save) {
    active = saved.confirmed;
    testing = false;
    if (saved.phase == Phase::CONFIRMED)
      return true;
    SavedState next = saved;
    next.phase = saved.phase == Phase::STAGED ? Phase::TESTING : Phase::CONFIRMED;
    if (next.phase == Phase::CONFIRMED)
      next.candidate = {};
    if (!save(next))
      return false;
    saved = next;
    if (saved.phase == Phase::TESTING) {
      active = saved.candidate;
      started_at = now;
      testing = true;
    }
    return true;
  }

  template<typename Save> bool stage(const NetworkConfig &config, Save save) {
    if (testing || saved.phase != Phase::CONFIRMED || validate_config(config) != nullptr)
      return false;
    SavedState next = saved;
    next.candidate = config;
    next.phase = Phase::STAGED;
    if (!save(next))
      return false;
    saved = next;
    return true;
  }

  bool expired(uint32_t now) const { return testing && uint32_t(now - started_at) >= TIMEOUT_MS; }
  uint32_t remaining_seconds(uint32_t now) const {
    if (!testing || expired(now))
      return 0;
    return (TIMEOUT_MS - uint32_t(now - started_at) + 999) / 1000;
  }

  template<typename Save> bool finish(bool keep, uint32_t now, Save save) {
    if (!testing || (keep && expired(now)))
      return false;
    SavedState next = saved;
    if (keep)
      next.confirmed = active;
    next.candidate = {};
    next.phase = Phase::CONFIRMED;
    if (!save(next))
      return false;
    saved = next;
    testing = false;
    return true;
  }
};

}  // namespace esphome::network_settings
