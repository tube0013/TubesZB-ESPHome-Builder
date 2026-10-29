#include "tubeszb_web.h"

#ifdef USE_TUBESZB_EFR32
#include "esphome/components/logger/logger.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace esphome::tubeszb_web {

std::string TubesZBWeb::radio_source_key_() const {
  return std::string((zigbee_beta_ != nullptr && zigbee_beta_->state) ? "beta|" : "stable|") + custom_url_->state + "|" + board_sensor_->state;
}

void TubesZBWeb::setup_radio_() {
  busy_->add_on_state_callback([this](bool busy) {
    std::lock_guard<std::mutex> lock(mutex_);
    radio_.busy = busy;
  });
  // Sensor callbacks keep the HTTP snapshot current even while the existing
  // EFR32 flasher/probe owns the main loop for a long operation.
  auto bind = [this](text_sensor::TextSensor *sensor, std::string RadioState::*field) {
    sensor->add_on_state_callback([this, field](const std::string &value) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (field == &RadioState::board && radio_.board != value) {
        radio_.checked = false; radio_checked_key_.clear();
      }
      radio_.*field = value;
    });
  };
  bind(firmware_sensor_, &RadioState::firmware);
  bind(latest_sensor_, &RadioState::latest);
  bind(board_sensor_, &RadioState::board);
  bind(manufacturer_sensor_, &RadioState::manufacturer);
  bind(ieee_sensor_, &RadioState::ieee);
  bind(status_sensor_, &RadioState::status);
  if (zigbee_beta_ != nullptr) zigbee_beta_->add_on_state_callback([this](bool) {
    std::lock_guard<std::mutex> lock(mutex_); radio_.checked = false; radio_checked_key_.clear();
  });
  custom_url_->add_on_state_callback([this](const std::string &) {
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
  if (strcmp(tag, "efr32_flasher") != 0)
    return;
  const std::string line(message, std::min(length, size_t(512)));
  std::lock_guard<std::mutex> lock(mutex_);
  if (line.find("run_update start") != std::string::npos) {
    radio_.operation = "flash"; radio_.result = 1; radio_.progress = -1;
  }
  const auto pos = line.find("Progress: ");
  if (pos != std::string::npos) {
    char *end = nullptr;
    const unsigned long progress = strtoul(line.c_str() + pos + 10, &end, 10);
    if (end != nullptr && *end == '%' && progress <= 100)
      radio_.progress = static_cast<int>(progress);
  }
  if (line.find("EFR32 update finished OK") != std::string::npos) {
    radio_.result = 2; radio_.progress = 100;
  } else if ((level <= ESPHOME_LOG_LEVEL_ERROR && radio_.result == 1) ||
             line.find("run_update finished ok=0") != std::string::npos || line.find("EFR32 update failed") != std::string::npos) {
    radio_.result = 3;
  }
}

void TubesZBWeb::run_radio_command_(Command command, const std::string &value) {
  if ((command == Command::RADIO_CHECK || command == Command::RADIO_FLASH) && !settings_->internet_allowed()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    radio_.result = 1; radio_.progress = -1;
    radio_command_at_ = millis();
    radio_.operation = command == Command::RADIO_FLASH ? "flash" : command == Command::RADIO_CHECK ? "check" :
                       command == Command::RADIO_PROBE ? "probe" : command == Command::RADIO_BOOTLOADER ? "bootloader" : "settings";
    if (command == Command::RADIO_CHECK) { radio_.checked = false; radio_checked_key_.clear(); }
  }
  switch (command) {
    case Command::RADIO_CHECK: radio_requested_key_ = radio_source_key_(); check_button_->press(); break;
    case Command::RADIO_FLASH: flash_button_->press(); break;
    case Command::RADIO_PROBE: probe_button_->press(); break;
    case Command::RADIO_RESET: reset_button_->press(); break;
    case Command::RADIO_BOOTLOADER: bootloader_button_->press(); break;
    case Command::SERIAL_PAUSE: pause_button_->press(); break;
    case Command::SERIAL_RESUME: resume_button_->press(); break;
    case Command::SET_BAUD: baud_->make_call().set_option(value).perform(); break;
    case Command::SET_URL:
      radio_checked_key_.clear();
      custom_url_->make_call().set_value(value).perform(); break;
    case Command::GATEWAY_CHANNEL:
      verified_source_.clear();
      gateway_beta_->control(value == "beta");
      ESP_LOGI("tubeszb_web", "Gateway firmware channel: %s. Check for updates before installing.", value.c_str()); break;
    case Command::ZIGBEE_CHANNEL:
      radio_checked_key_.clear();
      zigbee_beta_->control(value == "beta");
      ESP_LOGI("tubeszb_web", "Zigbee firmware channel: %s. Check Zigbee firmware before flashing.", value.c_str()); break;
    default: break;
  }
}

void TubesZBWeb::sample_radio_() {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool manual_bootloader = bootloader_script_->is_running();
  radio_.busy = busy_->state || manual_bootloader;
  // Also reflect presses from the original ESPHome interface using this script.
  if (manual_bootloader && radio_.operation != "bootloader") {
    radio_.operation = "bootloader"; radio_.result = 1; radio_.progress = -1;
    radio_command_at_ = millis();
  }
  radio_.connected = stream_->is_connected();
  radio_.paused = stream_->is_paused();
  radio_.firmware = firmware_sensor_->state;
  radio_.latest = latest_sensor_->state;
  radio_.board = board_sensor_->state;
  radio_.manufacturer = manufacturer_sensor_->state;
  radio_.ieee = ieee_sensor_->state;
  radio_.status = status_sensor_->state;
  radio_.url = custom_url_->state;
  radio_.selected_baud = baud_->current_option().c_str();
  radio_.gateway_beta = gateway_beta_->state;
  radio_.zigbee_beta = zigbee_beta_ != nullptr && zigbee_beta_->state;
  if (radio_.result == 1 && !radio_.busy && radio_.operation != "flash" && uint32_t(millis()-radio_command_at_) > 1000) {
    radio_.result = 2;
    if (radio_.operation == "check" && radio_requested_key_ == radio_source_key_()) radio_checked_key_ = radio_requested_key_;
  }
  radio_.checked = !radio_checked_key_.empty() && radio_checked_key_ == radio_source_key_();
}

void TubesZBWeb::radio_json_(JsonObject root) {
  auto data = root["zigbee"].to<JsonObject>();
  data["busy"] = radio_.busy;
  data["connected"] = radio_.connected;
  data["paused"] = radio_.paused;
  data["firmware"] = radio_.firmware;
  data["latest"] = radio_.latest;
  data["board_name"] = radio_.board;
  data["manufacturer"] = radio_.manufacturer;
  data["ieee"] = radio_.ieee;
  data["info_status"] = radio_.status;
  data["custom_url"] = radio_.url;
  data["baud"] = radio_.selected_baud;
  auto options = data["baud_options"].to<JsonArray>();
  for (const char *option : baud_->traits.get_options()) options.add(option);
  data["gateway_beta"] = radio_.gateway_beta;
  data["zigbee_beta"] = radio_.zigbee_beta;
  data["supports_zigbee_beta"] = zigbee_beta_ != nullptr;
  data["checked"] = radio_.checked;
  data["operation"] = radio_.operation;
  data["result"] = radio_.result;
  data["progress"] = radio_.progress;
  data["port"] = 6638;
}

}  // namespace esphome::tubeszb_web
#endif
