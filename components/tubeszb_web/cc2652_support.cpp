#include "tubeszb_web.h"

#ifdef USE_TUBESZB_CC2652
#include "cc2652_log.h"
#include "esphome/components/logger/logger.h"
#include <algorithm>
#include <cstring>

namespace esphome::tubeszb_web {

std::string TubesZBWeb::radio_source_key_() const {
  return std::string(zigbee_beta_->state ? "beta|" : "stable|") + board_sensor_->state;
}

void TubesZBWeb::setup_radio_() {
  auto bind = [this](text_sensor::TextSensor *sensor, std::string RadioState::*field) {
    sensor->add_on_state_callback([this, field](const std::string &value) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (field == &RadioState::board && radio_.board != value) {
        radio_.checked = false; radio_checked_key_.clear();
      }
      if (field == &RadioState::latest && radio_.operation == "check" && cc_started_)
        cc_fresh_latest_ = !value.empty();
      radio_.*field = value;
    });
  };
  bind(firmware_sensor_, &RadioState::firmware);
  bind(latest_sensor_, &RadioState::latest);
  bind(board_sensor_, &RadioState::board);
  bind(ieee_sensor_, &RadioState::ieee);
  radio_.manufacturer = "Texas Instruments";
  radio_.status = "Ready";
  zigbee_beta_->add_on_state_callback([this](bool) {
    std::lock_guard<std::mutex> lock(mutex_); radio_.checked = false; radio_checked_key_.clear();
  });
  gateway_beta_->add_on_state_callback([this](bool) {
    std::lock_guard<std::mutex> lock(mutex_); verified_source_.clear(); snapshot_.update_verified = false;
  });
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_callback(this,
      [](void *self, uint8_t level, const char *tag, const char *message, size_t length) {
        static_cast<TubesZBWeb *>(self)->radio_log_(level, tag, message, length);
      });
  }
}

void TubesZBWeb::radio_log_(uint8_t level, const char *tag, const char *message, size_t length) {
  if (strcmp(tag, "cc2652_flasher") != 0) return;
  const std::string line(message, std::min(length, size_t(512)));
  const auto event = decode_cc2652_log(line);
  using E = CC2652LogEvent;
  std::lock_guard<std::mutex> lock(mutex_);
  if (event.kind == E::CHECK_START || event.kind == E::FLASH_START || event.kind == E::PROBE_START || event.kind == E::BOOTLOADER_START) {
    radio_.operation = event.kind == E::CHECK_START ? "check" : event.kind == E::FLASH_START ? "flash" : event.kind == E::PROBE_START ? "probe" : "bootloader";
    radio_.result = 1; radio_.busy = true; radio_.progress = -1; radio_command_at_ = millis();
    radio_.status = "CC2652 operation in progress";
    cc_started_ = event.kind != E::BOOTLOADER_START;
    cc_failed_ = cc_fresh_latest_ = cc_flash_ok_ = cc_probe_ok_ = false;
    if (event.kind == E::CHECK_START) {
      radio_.checked = false; radio_checked_key_.clear(); radio_requested_key_ = radio_source_key_();
    }
  }
  if (event.kind == E::PROGRESS) radio_.progress = event.progress;
  if (event.kind == E::FLASH_OK) cc_flash_ok_ = true;
  if (event.kind == E::PROBE_OK) cc_probe_ok_ = true;
  if (event.kind == E::PROBE_FAILED || (level <= ESPHOME_LOG_LEVEL_ERROR && radio_.busy)) cc_failed_ = true;
  if (event.kind == E::BOOTLOADER_READY) {
    radio_.busy = false; radio_.result = 2;
    radio_.status = "Bootloader ready; reset the radio to resume normal operation";
  }
}

void TubesZBWeb::queue_cc2652_automatic_(Command command) {
  if (settings_->is_trial_active()) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (command_ != Command::NONE || action_pending_ || gateway_ota_state_ == 1 || checking_ || radio_.busy || radio_.result == 1) return;
  command_ = command; action_pending_ = true;
}
void TubesZBWeb::request_cc2652_check() {
  if (!settings_->internet_allowed()) return;
  queue_cc2652_automatic_(Command::RADIO_CHECK);
}
void TubesZBWeb::request_cc2652_probe() { queue_cc2652_automatic_(Command::RADIO_PROBE); }

void TubesZBWeb::run_radio_command_(Command command, const std::string &value) {
  if ((command == Command::RADIO_CHECK || command == Command::RADIO_FLASH) && !settings_->internet_allowed()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    radio_.result = 1; radio_.busy = true; radio_.progress = -1;
    radio_command_at_ = millis(); cc_started_ = cc_failed_ = cc_fresh_latest_ = cc_flash_ok_ = cc_probe_ok_ = false;
    radio_.operation = command == Command::RADIO_FLASH ? "flash" : command == Command::RADIO_CHECK ? "check" :
                       command == Command::RADIO_PROBE ? "probe" : command == Command::RADIO_BOOTLOADER ? "bootloader" : "settings";
    radio_.status = "CC2652 action queued";
    if (command == Command::RADIO_CHECK) { radio_.checked = false; radio_checked_key_.clear(); }
  }
  // Both frontends invoke the same native button and shared pause/flash/resume script.
  switch (command) {
    case Command::RADIO_CHECK: check_button_->press(); break;
    case Command::RADIO_FLASH: flash_button_->press(); break;
    case Command::RADIO_PROBE: probe_button_->press(); break;
    case Command::RADIO_RESET: reset_button_->press(); break;
    case Command::RADIO_BOOTLOADER: bootloader_button_->press(); break;
    case Command::SERIAL_PAUSE: pause_button_->press(); break;
    case Command::SERIAL_RESUME: resume_button_->press(); break;
    case Command::GATEWAY_CHANNEL: gateway_beta_->control(value == "beta"); break;
    case Command::ZIGBEE_CHANNEL: zigbee_beta_->control(value == "beta"); break;
    default: break;
  }
}

void TubesZBWeb::sample_radio_() {
  std::lock_guard<std::mutex> lock(mutex_);
  radio_.connected = stream_->is_connected(); radio_.paused = stream_->is_paused();
  radio_.firmware = firmware_sensor_->state; radio_.latest = latest_sensor_->state;
  radio_.board = board_sensor_->state; radio_.ieee = ieee_sensor_->state;
  radio_.selected_baud = std::to_string(uart_->get_baud_rate());
  radio_.gateway_beta = gateway_beta_->state; radio_.zigbee_beta = zigbee_beta_->state;
  if (cc_started_) {
    // These native flasher methods run synchronously on the main loop. Reaching
    // this sample means they returned. Completion still requires their real result.
    cc_started_ = false; radio_.busy = false;
    bool ok = radio_.operation == "flash" ? cc_flash_ok_ : radio_.operation == "probe" ? cc_probe_ok_ : !cc_failed_ && cc_fresh_latest_;
    radio_.result = ok ? 2 : 3;
    radio_.status = ok ? "CC2652 operation complete" : "CC2652 operation failed; open device logs";
    if (ok && radio_.operation == "flash") radio_.progress = 100;
    if (radio_.operation == "check" && ok && radio_requested_key_ == radio_source_key_()) radio_checked_key_ = radio_requested_key_;
  } else if (radio_.result == 1 && radio_.operation == "settings" && uint32_t(millis()-radio_command_at_) > 1000) {
    radio_.busy = false; radio_.result = 2; radio_.status = "CC2652 settings applied";
  } else if (radio_.result == 1 && uint32_t(millis()-radio_command_at_) > 35000) {
    radio_.busy = false; radio_.result = 3; radio_.status = "CC2652 action did not complete; open device logs";
  }
  // Keep maintenance controls locked through the shared script's readback and
  // bridge restoration, including failures and legacy ESPHome button presses.
  radio_.busy = radio_.result == 1 || cc_flash_script_->is_running() || cc_boot_probe_script_->is_running();
  radio_.paused = stream_->is_paused();
  radio_.checked = !radio_checked_key_.empty() && radio_checked_key_ == radio_source_key_();
}

void TubesZBWeb::radio_json_(JsonObject root) {
  auto data = root["zigbee"].to<JsonObject>();
  data["busy"] = radio_.busy; data["connected"] = radio_.connected; data["paused"] = radio_.paused;
  data["firmware"] = radio_.firmware; data["latest"] = radio_.latest;
  data["board_name"] = radio_.board; data["manufacturer"] = radio_.manufacturer;
  data["ieee"] = radio_.ieee; data["info_status"] = radio_.status;
  data["custom_url"] = ""; data["baud"] = radio_.selected_baud;
  data["baud_options"].to<JsonArray>();
  data["gateway_beta"] = radio_.gateway_beta; data["zigbee_beta"] = radio_.zigbee_beta;
  data["checked"] = radio_.checked; data["operation"] = radio_.operation;
  data["result"] = radio_.result; data["progress"] = radio_.progress; data["port"] = 6638;
}
}  // namespace esphome::tubeszb_web
#endif
