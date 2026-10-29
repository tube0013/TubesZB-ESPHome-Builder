#pragma once
#include <atomic>

namespace esphome::network_settings {
// Fail closed until NVS has been read, before Ethernet setup and boot checks.
class InternetPolicy {
 public:
  bool allowed() const { return ready_.load() && !offline_.load(); }
  bool offline() const { return offline_.load(); }
  void restore(bool offline) { offline_.store(offline); ready_.store(true); }
  template<typename Save> bool set(bool offline, Save save) {
    if (!ready_.load()) return false;
    if (offline_.load() == offline) return true;
    if (!save(offline)) return false;
    offline_.store(offline);
    return true;
  }
 protected:
  std::atomic<bool> ready_{false}, offline_{true};
};
}  // namespace esphome::network_settings
