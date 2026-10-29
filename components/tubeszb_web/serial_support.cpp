#include "tubeszb_web.h"
#ifdef USE_TUBESZB_SERIAL_RADIO
namespace esphome::tubeszb_web {
std::string TubesZBWeb::radio_source_key_() const { return ""; }
void TubesZBWeb::setup_radio_() {}
void TubesZBWeb::run_radio_command_(Command command, const std::string &value) {
  switch (command) {
    case Command::RADIO_RESET: reset_button_->press(); break;
    case Command::SERIAL_PAUSE: pause_button_->press(); break;
    case Command::SERIAL_RESUME: resume_button_->press(); break;
    case Command::SET_BAUD: baud_->make_call().set_option(value).perform(); break;
    default: return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  radio_.operation = "settings"; radio_.result = 2;
}
void TubesZBWeb::sample_radio_() {
  std::lock_guard<std::mutex> lock(mutex_);
  radio_.connected = stream_->is_connected(); radio_.paused = stream_->is_paused();
  radio_.board = radio_model_; radio_.manufacturer = "Silicon Labs";
  radio_.status = radio_.paused ? "Zigbee serial bridge paused" : "Zigbee serial bridge ready";
  radio_.selected_baud = baud_ != nullptr ? baud_->current_option() : std::to_string(uart_->get_baud_rate());
}
void TubesZBWeb::radio_json_(JsonObject root) {
  auto data = root["zigbee"].to<JsonObject>();
  data["connected"] = radio_.connected; data["paused"] = radio_.paused;
  data["busy"] = false; data["result"] = radio_.result; data["operation"] = radio_.operation;
  data["progress"] = -1; data["board_name"] = radio_.board; data["manufacturer"] = radio_.manufacturer;
  data["info_status"] = radio_.status; data["baud"] = radio_.selected_baud;
  data["firmware"] = ""; data["ieee"] = ""; data["latest"] = "";
  data["custom_url"] = ""; data["checked"] = false; data["gateway_beta"] = false; data["zigbee_beta"] = false;
  auto options = data["baud_options"].to<JsonArray>();
  if (baud_ != nullptr)
    for (const auto &option : baud_->traits.get_options()) options.add(option);
  data["port"] = 6638;
}
}  // namespace esphome::tubeszb_web
#endif
