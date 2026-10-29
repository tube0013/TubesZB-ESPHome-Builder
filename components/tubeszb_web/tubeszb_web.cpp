#include "tubeszb_web.h"
#include "gbl_url.h"

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/json/json_util.h"
#ifdef USE_ZWAVE_PROXY
#include "esphome/components/zwave_proxy/zwave_proxy.h"
#endif
#include <esp_heap_caps.h>
#include <algorithm>
#include <cmath>

namespace esphome::tubeszb_web {

static const char *const TAG = "tubeszb_web";

void TubesZBWeb::setup() {
  char token[33];
  snprintf(token, sizeof(token), "%08x%08x%08x%08x", unsigned(random_uint32()), unsigned(random_uint32()),
           unsigned(random_uint32()), unsigned(random_uint32()));
  nonce_ = token;
  char mac[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  mac_ = ethernet_->get_eth_mac_address_pretty_into_buffer(mac);
  // The standard middleware applies any configured ESPHome HTTP authentication.
  base_->add_handler(this);
  ota::get_global_ota_callback()->add_global_state_listener(this);
  update_->add_on_state_callback([this]() {
    if (checking_ && !update_->status_has_error() && check_source_ == update_->get_source_url())
      verified_source_ = check_source_;
    checking_ = false; check_timeout_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.checking = false;
#ifdef USE_TUBESZB_ZIGBEE
    snapshot_.update_verified = !verified_source_.empty() && verified_source_ == update_->get_source_url();
#endif
  });
#ifdef USE_TUBESZB_ZIGBEE
  setup_radio_();
#endif
  settings_->set_mutation_guard([this]() {
    std::lock_guard<std::mutex> lock(mutex_);
    return gateway_ota_state_ != 1 && !action_pending_ && snapshot_.update_state != update::UPDATE_STATE_INSTALLING
#ifdef USE_TUBESZB_ZIGBEE
           && !radio_.busy && radio_.result != 1
#endif
           ;
  });
}

void TubesZBWeb::on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *) {
  // OTA callbacks keep HTTP progress current while the download blocks the main loop.
  std::lock_guard<std::mutex> lock(mutex_);
  switch (state) {
    case ota::OTA_STARTED:
      gateway_ota_state_ = 1; gateway_ota_progress_ = 0.0f; gateway_ota_error_ = 0; break;
    case ota::OTA_IN_PROGRESS:
      gateway_ota_state_ = 1;
      if (std::isfinite(progress)) gateway_ota_progress_ = std::clamp(progress, 0.0f, 100.0f);
      break;
    case ota::OTA_COMPLETED:
      gateway_ota_state_ = 2; gateway_ota_progress_ = 100.0f; gateway_ota_error_ = 0; break;
    case ota::OTA_ABORT:
    case ota::OTA_ERROR:
      gateway_ota_state_ = 3; gateway_ota_error_ = error; break;
  }
}

void TubesZBWeb::check_gateway_update() {
  if (!settings_->internet_allowed()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (checking_ || command_ != Command::NONE || gateway_ota_state_ == 1 || update_->state == update::UPDATE_STATE_INSTALLING || !ethernet_->is_connected())
      return;
#ifdef USE_TUBESZB_ZIGBEE
    if (radio_.busy || radio_.result == 1) return;
#endif
    if (gateway_ota_state_ == 3) { gateway_ota_state_ = 0; gateway_ota_progress_ = -1.0f; gateway_ota_error_ = 0; }
    checking_ = true;
    check_timeout_ = false;
    check_started_ = millis();
    check_source_ = update_->get_source_url();
    snapshot_.checking = true;
  }
  update_->status_clear_error();
  update_->check();
}

void TubesZBWeb::loop() {
  Command command;
  std::string value;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    command = command_;
    command_ = Command::NONE;
    value.swap(command_value_);
  }
  if (command == Command::RESTART) {
    restart_at_ = millis() + 1500;
    restart_pending_ = true;
  } else if (command == Command::CHECK) {
    check_gateway_update();
  } else if (command == Command::INSTALL) {
    if (settings_->internet_allowed()) update_->perform(false);
  }
#ifdef USE_TUBESZB_ZIGBEE
  else if (command != Command::NONE) run_radio_command_(command, value);
#endif
  if (checking_ && (update_->status_has_error() || uint32_t(millis() - check_started_) >= 30000)) {
    check_timeout_ = !update_->status_has_error();
    checking_ = false;
  }
  if (restart_pending_ && int32_t(millis() - restart_at_) >= 0)
    App.safe_reboot();
  if (command == Command::NONE && uint32_t(millis() - sampled_at_) < 1000)
    return;
  sampled_at_ = millis();
  Snapshot next;
  next.ethernet = ethernet_->is_connected();
  next.api = api::global_api_server != nullptr && api::global_api_server->is_connected();
#ifdef USE_ZWAVE_PROXY
  auto *proxy = zwave_proxy::global_zwave_proxy;
  next.zwave_connected = proxy != nullptr && proxy->get_api_connection() != nullptr;
  next.proxy = next.zwave_connected;
  next.zwave_baud = (zwave_uart_ != nullptr ? zwave_uart_ : uart_)->get_baud_rate();
  next.home_id = proxy != nullptr ? proxy->get_home_id() : 0;
#endif
#ifdef USE_TUBESZB_ZIGBEE
  sample_radio_();
  next.proxy = stream_->is_connected();
  next.update_verified = !verified_source_.empty() && verified_source_ == update_->get_source_url();
#endif
  next.uptime = static_cast<uint32_t>(millis_64() / 1000);
  next.free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  next.baud = uart_->get_baud_rate();
  next.update_state = update_->state;
  next.latest_version = update_->update_info.latest_version;
  next.update_error = update_->status_has_error() || check_timeout_;
  next.checking = checking_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(next);
    action_pending_ = restart_pending_;
  }
}

void TubesZBWeb::dump_config() {
  ESP_LOGCONFIG(TAG, "TubesZB frontend: /; original ESPHome frontend: /esphome");
}

bool TubesZBWeb::canHandle(AsyncWebServerRequest *request) const {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  auto url = request->url_to(buffer);
  return url == "/" || url == "/esphome" || url == "/esphome/" || url == "/tubeszb/status" ||
         url == "/tubeszb/restart" || url == "/tubeszb/check-update" || url == "/tubeszb/install-update"
#ifdef USE_TUBESZB_ZIGBEE
         || url == "/tubeszb/zigbee-action"
#endif
         ;
}

void TubesZBWeb::send_json_(AsyncWebServerRequest *request, int code, const std::string &body) {
  auto *response = request->beginResponse(code, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void TubesZBWeb::error_(AsyncWebServerRequest *request, int code, const char *message) {
  ESP_LOGW(TAG, "Frontend request rejected (%d): %s", code, message);
  auto body = json::build_json([message](JsonObject root) { root["error"] = message; });
  send_json_(request, code, std::string(body.c_str(), body.size()));
}

bool TubesZBWeb::authorized_(AsyncWebServerRequest *request) {
  auto token = request->get_header("X-TubesZB-UI");
  auto origin = request->get_header("Origin");
  auto host = request->get_header("Host");
  if (!token.has_value() || *token != nonce_ ||
      (origin.has_value() && (!host.has_value() ||
       (*origin != "http://" + *host && *origin != "https://" + *host)))) {
    error_(request, 401, "Reload this device's page before making changes.");
    return false;
  }
  return true;
}

void TubesZBWeb::status_(AsyncWebServerRequest *request) {
  std::string payload;
  {
  std::lock_guard<std::mutex> lock(mutex_);
  auto body = json::build_json([this](JsonObject root) {
    root["nonce"] = nonce_;
    root["name"] = App.get_name().c_str();
    root["model"] = model_;
    root["board"] = board_;
#ifdef USE_TUBESZB_ZIGBEE
#ifdef USE_TUBESZB_CC2652
    root["radio_type"] = "cc2652";
#elif defined(USE_TUBESZB_EFR32)
    root["radio_type"] = "efr32";
#else
    root["radio_type"] = "serial";
#endif
    radio_json_(root);
#else
    root["radio_type"] = "zwave";
#endif
#ifdef USE_TUBESZB_DUAL_RADIO
    root["dual_radio"] = true;
#else
    root["dual_radio"] = false;
#endif
#ifdef USE_ZWAVE_PROXY
    auto zwave = root["zwave"].to<JsonObject>();
    zwave["connected"] = snapshot_.zwave_connected;
    zwave["home_id"] = snapshot_.home_id;
    zwave["baud_rate"] = snapshot_.zwave_baud;
#endif
    root["project_version"] = ESPHOME_PROJECT_VERSION;
    root["esphome_version"] = ESPHOME_VERSION;
    root["mac"] = mac_;
    root["ethernet_connected"] = snapshot_.ethernet;
    root["api_connected"] = snapshot_.api;
    root["proxy_connected"] = snapshot_.proxy;
    root["home_id"] = snapshot_.home_id;
    root["baud_rate"] = snapshot_.baud;
    root["uptime"] = snapshot_.uptime;
    root["free_heap"] = snapshot_.free_heap;
    root["gateway_ota_state"] = gateway_ota_state_;
    root["gateway_ota_progress"] = gateway_ota_progress_;
    root["gateway_ota_error"] = gateway_ota_error_;
    root["update_state"] = unsigned(snapshot_.update_state);
    root["latest_version"] = snapshot_.latest_version;
    root["update_error"] = snapshot_.update_error;
    root["update_verified"] = snapshot_.update_verified;
    root["checking"] = snapshot_.checking || command_ == Command::CHECK;
    root["action_pending"] = action_pending_;
  });
  payload.assign(body.c_str(), body.size());
  }
  send_json_(request, 200, payload);
}

void TubesZBWeb::handleRequest(AsyncWebServerRequest *request) {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  auto url = request->url_to(buffer);
  if (request->method() == HTTP_GET && url == "/") {
    auto *response = request->beginResponse(200, "text/html; charset=utf-8", FRONTEND_GZ, FRONTEND_GZ_SIZE);
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Frame-Options", "DENY");
    request->send(response);
    return;
  }
  if (request->method() == HTTP_GET && (url == "/esphome" || url == "/esphome/")) {
    server_->handle_index_request(request);
    return;
  }
  if (request->method() == HTTP_GET && url == "/tubeszb/status") {
    status_(request);
    return;
  }
  if (request->method() != HTTP_POST) {
    error_(request, 400, "Use GET for pages and status; POST for actions.");
    return;
  }
  if (!authorized_(request))
    return;
  const bool network_trial = settings_->is_trial_active();
  const bool internet_allowed = settings_->internet_allowed();
  std::unique_lock<std::mutex> lock(mutex_);
  auto reject = [&](int code, const char *message) { lock.unlock(); error_(request, code, message); };
  if (action_pending_ || gateway_ota_state_ == 1 || snapshot_.update_state == update::UPDATE_STATE_INSTALLING || snapshot_.checking) {
    reject(409, "Another device action is in progress.");
    return;
  }
#ifdef USE_TUBESZB_ZIGBEE
  if (radio_.busy || radio_.result == 1) {
    reject(409, "Wait for the active Zigbee operation to finish.");
    return;
  }
#endif
  if (url != "/tubeszb/restart" && network_trial) {
    reject(409, "Keep or revert the network trial before updating firmware.");
    return;
  }
  const auto radio_action = request->arg("action");
  if (!internet_allowed && (url == "/tubeszb/check-update" || url == "/tubeszb/install-update" ||
      (url == "/tubeszb/zigbee-action" && (radio_action == "check" || radio_action == "flash")))) {
    reject(409, "Use without internet is enabled. Disable it in Network to check or download firmware."); return;
  }
  if (url == "/tubeszb/restart") {
    command_ = Command::RESTART;
  } else if (url == "/tubeszb/check-update") {
    if (!snapshot_.ethernet) {
      reject(409, "Ethernet is not connected.");
      return;
    }
    command_ = Command::CHECK;
  } else if (url == "/tubeszb/install-update") {
    if (snapshot_.update_state != update::UPDATE_STATE_AVAILABLE || snapshot_.update_error || !snapshot_.update_verified) {
      reject(409, "Check for a valid firmware update before installing.");
      return;
    }
    gateway_ota_state_ = 1; gateway_ota_progress_ = -1.0f; gateway_ota_error_ = 0;
    command_ = Command::INSTALL;
#ifdef USE_TUBESZB_ZIGBEE
  } else if (url == "/tubeszb/zigbee-action") {
    const auto action = request->arg("action");
    const auto value = request->arg("value");
    if (action == "check") {
      if (check_button_ == nullptr) { reject(400, "Radio firmware checks are unavailable for this model."); return; }
      if (!radio_.url.empty()) { reject(400, "Clear the custom URL to check the selected release channel."); return; }
      command_ = Command::RADIO_CHECK;
    }
    else if (action == "probe") {
      if (probe_button_ == nullptr) { reject(400, "Radio identity probing is unavailable for this model."); return; }
      command_ = Command::RADIO_PROBE;
    }
    else if (action == "reset") command_ = Command::RADIO_RESET;
    else if (action == "bootloader") {
      if (bootloader_button_ == nullptr) { reject(400, "Manual bootloader mode is unavailable for this radio."); return; }
      command_ = Command::RADIO_BOOTLOADER;
    }
    else if (action == "pause") command_ = Command::SERIAL_PAUSE;
    else if (action == "resume") command_ = Command::SERIAL_RESUME;
    else if (action == "flash") {
      if (flash_button_ == nullptr) { reject(400, "Radio firmware flashing is unavailable for this model."); return; }
      if (!radio_.url.empty() && !is_gbl_url(radio_.url)) {
        reject(400, "Custom radio firmware must be a direct HTTP(S) .gbl file URL."); return;
      }
      if (radio_.url.empty() && !radio_.checked) {
        reject(409, "Check Zigbee firmware on the selected channel before flashing."); return;
      }
      command_ = Command::RADIO_FLASH;
    } else if (action == "baud") {
#if defined(USE_TUBESZB_EFR32) || defined(USE_TUBESZB_SERIAL_RADIO)
      if (baud_ == nullptr || !baud_->has_option(value)) { reject(400, "Unsupported baud rate."); return; }
      command_ = Command::SET_BAUD;
#else
      reject(400, "Changing the CC2652 baud rate is not supported here."); return;
#endif
    } else if (action == "url") {
#ifdef USE_TUBESZB_EFR32
      if (custom_url_ == nullptr) { reject(400, "Custom firmware URLs are not supported for this radio."); return; }
      if (!value.empty() && !is_gbl_url(value)) {
        reject(400, "Enter a direct HTTP(S) .gbl file URL, or leave it empty."); return;
      }
      command_ = Command::SET_URL;
#else
      reject(400, "Custom firmware URLs are not supported for this radio."); return;
#endif
    } else if (action == "gateway-channel" || action == "zigbee-channel") {
      if ((action == "gateway-channel" ? gateway_beta_ : zigbee_beta_) == nullptr) { reject(400, "Firmware channels are unavailable for this model."); return; }
      if (value != "stable" && value != "beta") { reject(400, "Choose Stable or Beta."); return; }
      command_ = action == "gateway-channel" ? Command::GATEWAY_CHANNEL : Command::ZIGBEE_CHANNEL;
    } else { reject(400, "Unknown Zigbee action."); return; }
    command_value_ = value;
#endif
  } else {
    reject(404, "Unknown device action.");
    return;
  }
  action_pending_ = true;
  lock.unlock();
  // The ESPHome IDF server maps unsupported codes (including 202) to HTTP 500.
  // Acknowledge that the command was queued; status reports its completion.
  send_json_(request, 200, "{\"ok\":true}");
}

}  // namespace esphome::tubeszb_web
