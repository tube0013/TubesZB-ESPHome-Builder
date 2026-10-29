#pragma once
#include <cstdint>
#include <map>
namespace esphome {
inline std::map<uint32_t, uint32_t> saved;
inline bool save_ok = true, sync_ok = true;
class ESPPreferenceObject {
 public:
  uint32_t key{};
  bool load(uint32_t *value) { if (!saved.count(key)) return false; *value = saved[key]; return true; }
  bool save(const uint32_t *value) { if (!save_ok) return false; saved[key] = *value; return true; }
};
class ESPPreferences {
 public:
  template<typename T> ESPPreferenceObject make_preference(uint32_t key) { return {key}; }
  bool sync() { return sync_ok; }
};
inline ESPPreferences preferences;
inline ESPPreferences *global_preferences = &preferences;
}
