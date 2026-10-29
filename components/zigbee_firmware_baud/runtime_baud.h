#pragma once
#include "baud_rate.h"
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"
#include "esphome/components/select/select.h"
#include <string>
#include <cstdlib>

namespace esphome::zigbee_firmware_baud {
class RuntimeBaud {
 public:
  void set_preference_hash(uint32_t hash) { preference_hash_ = hash; }
  void set_selector(select::Select *selector) { selector_ = selector; }
  void setup(uint32_t initial) {
    preference_ = global_preferences->make_preference<uint32_t>(preference_hash_);
    current_ = initial;
    if (selector_ != nullptr && !selector_->current_option().empty()) {
      const uint32_t selected = std::strtoul(selector_->current_option().c_str(), nullptr, 10);
      if (valid_baud(selected)) current_ = selected;
    }
    uint32_t saved = 0;
    if (preference_.load(&saved) && valid_baud(saved)) current_ = saved;
    ready_ = true;
    // The numeric saved baud survives selector option reordering across builds.
    if (selector_ != nullptr && supports(current_) && selector_->current_option() != std::to_string(current_)) {
      applying_manifest_ = true;
      selector_->make_call().set_option(std::to_string(current_)).perform();
      applying_manifest_ = false;
      if (!global_preferences->sync()) ESP_LOGE("zigbee_firmware_baud", "Could not synchronize saved baud selector");
    }
  }
  uint32_t current() const { return current_; }
  bool applying_manifest() const { return applying_manifest_; }
  bool supports(uint32_t baud) const {
    return valid_baud(baud) && (selector_ == nullptr || selector_->has_option(std::to_string(baud)));
  }
  void manual(uint32_t baud) {
    if (!ready_ || !valid_baud(baud) || baud == current_) return;
    current_ = baud;
    if (!persist_()) ESP_LOGE("zigbee_firmware_baud", "Could not save application baud %u", unsigned(baud));
  }
  // Called only after a verified transfer and after retuning the actual UART.
  // Bound template selectors must skip their UART set_action while this flag is
  // true; their state and saved option still update via the normal select call.
  bool commit(uint32_t baud) {
    if (!ready_ || !supports(baud)) return false;
    current_ = baud;
    applying_manifest_ = true;
    if (selector_ != nullptr) selector_->make_call().set_option(std::to_string(baud)).perform();
    applying_manifest_ = false;
    return persist_();
  }
 private:
  bool persist_() { return preference_.save(&current_) && global_preferences->sync(); }
  ESPPreferenceObject preference_;
  select::Select *selector_{nullptr};
  uint32_t preference_hash_{0}, current_{0};
  bool ready_{false}, applying_manifest_{false};
};
}  // namespace esphome::zigbee_firmware_baud
