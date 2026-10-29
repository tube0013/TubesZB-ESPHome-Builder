#pragma once

#include "esphome/core/component.h"
#include "esphome/components/ethernet/ethernet_component.h"
#include "esphome/components/http_request/update/http_request_update.h"
#include "esphome/components/network_settings/network_settings.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/ota/ota_backend.h"
#include <mutex>
#ifdef USE_TUBESZB_ZIGBEE
#include "esphome/components/script/script.h"
#endif
#ifdef USE_TUBESZB_ZIGBEE
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#if defined(USE_TUBESZB_EFR32) || defined(USE_TUBESZB_SERIAL_RADIO)
#include "esphome/components/select/select.h"
#endif
#include "esphome/components/switch/switch.h"
#ifdef USE_TUBESZB_EFR32
#include "esphome/components/text/text.h"
#endif
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/stream_server/stream_server.h"
#endif

namespace esphome::tubeszb_web {

extern const uint8_t FRONTEND_GZ[];
extern const size_t FRONTEND_GZ_SIZE;

class TubesZBWeb : public Component, public AsyncWebHandler, public ota::OTAGlobalStateListener {
 public:
  void set_base(web_server_base::WebServerBase *base) { base_ = base; }
  void set_web_server(web_server::WebServer *server) { server_ = server; }
  void set_network_settings(network_settings::NetworkSettings *settings) { settings_ = settings; }
  void set_ethernet(ethernet::EthernetComponent *ethernet) { ethernet_ = ethernet; }
  void set_zwave_uart(uart::UARTComponent *uart) { zwave_uart_ = uart; }
  void set_uart(uart::UARTComponent *uart) { uart_ = uart; }
  void set_update(http_request::HttpRequestUpdate *update) { update_ = update; }
  void set_model(const char *model) { model_ = model; }
  void set_board(const char *board) { board_ = board; }
#ifdef USE_TUBESZB_ZIGBEE
  void set_stream_server_id(::StreamServerComponent *server) { stream_ = server; }
  void set_busy_sensor(binary_sensor::BinarySensor *sensor) { busy_ = sensor; }
#define TZB_TEXT_BINDING(name) void set_##name##_sensor(text_sensor::TextSensor *sensor) { name##_sensor_ = sensor; }
  TZB_TEXT_BINDING(firmware)
  TZB_TEXT_BINDING(latest)
  TZB_TEXT_BINDING(board)
  TZB_TEXT_BINDING(manufacturer)
  TZB_TEXT_BINDING(ieee)
  TZB_TEXT_BINDING(status)
#undef TZB_TEXT_BINDING
#define TZB_BUTTON_BINDING(name) void set_##name##_button(button::Button *value) { name##_button_ = value; }
  TZB_BUTTON_BINDING(check)
  TZB_BUTTON_BINDING(flash)
  TZB_BUTTON_BINDING(probe)
  TZB_BUTTON_BINDING(reset)
  TZB_BUTTON_BINDING(pause)
  TZB_BUTTON_BINDING(resume)
  TZB_BUTTON_BINDING(bootloader)
#undef TZB_BUTTON_BINDING
#ifdef USE_TUBESZB_CC2652
  void set_flash_script(script::Script<> *value) { cc_flash_script_ = value; }
  void set_boot_probe_script(script::Script<> *value) { cc_boot_probe_script_ = value; }
#endif
#if defined(USE_TUBESZB_EFR32) || defined(USE_TUBESZB_SERIAL_RADIO)
  void set_baud_select(select::Select *value) { baud_ = value; }
#endif
  void set_radio_model(const char *model) { radio_model_ = model; }
  void set_gateway_beta_switch(switch_::Switch *value) { gateway_beta_ = value; }
  void set_zigbee_beta_switch(switch_::Switch *value) { zigbee_beta_ = value; }
#ifdef USE_TUBESZB_EFR32
  void set_bootloader_script(script::Script<> *value) { bootloader_script_ = value; }
  void set_custom_url_text(text::Text *value) { custom_url_ = value; }
#endif
#endif
  // Register the index handler before WebServer::setup() at priority 249.
  float get_setup_priority() const override { return setup_priority::ETHERNET - 0.5f; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *component) override;
  void request_cc2652_check();
  void request_cc2652_probe();
  void check_gateway_update();  // Main-loop entry point shared by UI and YAML checks.
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  enum class Command { NONE, RESTART, CHECK, INSTALL, RADIO_CHECK, RADIO_FLASH, RADIO_PROBE, RADIO_RESET,
                       SERIAL_PAUSE, SERIAL_RESUME, SET_BAUD, SET_URL, GATEWAY_CHANNEL, ZIGBEE_CHANNEL, RADIO_BOOTLOADER };
  struct Snapshot {
    bool ethernet{false}, api{false}, proxy{false}, zwave_connected{false}, update_error{false}, checking{false}, update_verified{true};
    uint32_t home_id{0}, uptime{0}, free_heap{0}, baud{0}, zwave_baud{0};
    update::UpdateState update_state{update::UPDATE_STATE_UNKNOWN};
    std::string latest_version;
  } snapshot_;
  void send_json_(AsyncWebServerRequest *request, int code, const std::string &body);
  void error_(AsyncWebServerRequest *request, int code, const char *message);
  bool authorized_(AsyncWebServerRequest *request);
  void status_(AsyncWebServerRequest *request);
#ifdef USE_TUBESZB_ZIGBEE
  void setup_radio_();
  void run_radio_command_(Command command, const std::string &value);
  void sample_radio_();
  void radio_json_(JsonObject root);
  void radio_log_(uint8_t level, const char *tag, const char *message, size_t length);
  struct RadioState {
    bool busy{false}, connected{false}, paused{false}, gateway_beta{false}, zigbee_beta{false};
    int progress{-1};
    uint8_t result{0};  // 0=no result, 1=running, 2=completed, 3=failed; never infer a flash success from a disconnect.
    std::string operation, firmware, latest, board, manufacturer, ieee, status, url, selected_baud;
    bool checked{false};
  } radio_;
  ::StreamServerComponent *stream_{nullptr};
  binary_sensor::BinarySensor *busy_{nullptr};
  text_sensor::TextSensor *firmware_sensor_{nullptr}, *latest_sensor_{nullptr}, *board_sensor_{nullptr},
                          *manufacturer_sensor_{nullptr}, *ieee_sensor_{nullptr}, *status_sensor_{nullptr};
  button::Button *check_button_{nullptr}, *flash_button_{nullptr}, *probe_button_{nullptr}, *reset_button_{nullptr},
                 *pause_button_{nullptr}, *resume_button_{nullptr}, *bootloader_button_{nullptr};
  switch_::Switch *gateway_beta_{nullptr}, *zigbee_beta_{nullptr};
  #ifdef USE_TUBESZB_EFR32
  script::Script<> *bootloader_script_{nullptr};
  text::Text *custom_url_{nullptr};
  #endif
#if defined(USE_TUBESZB_EFR32) || defined(USE_TUBESZB_SERIAL_RADIO)
  select::Select *baud_{nullptr};
#endif
  const char *radio_model_{"MGM21"};
  uint32_t radio_command_at_{0};
  std::string radio_checked_key_;
  std::string radio_requested_key_;
  std::string radio_source_key_() const;
#ifdef USE_TUBESZB_CC2652
  script::Script<> *cc_flash_script_{nullptr}, *cc_boot_probe_script_{nullptr};
  bool cc_started_{false}, cc_failed_{false}, cc_fresh_latest_{false}, cc_flash_ok_{false}, cc_probe_ok_{false};
  void queue_cc2652_automatic_(Command command);
#endif
#endif
  web_server_base::WebServerBase *base_{nullptr};
  web_server::WebServer *server_{nullptr};
  network_settings::NetworkSettings *settings_{nullptr};
  ethernet::EthernetComponent *ethernet_{nullptr};
  uart::UARTComponent *uart_{nullptr}, *zwave_uart_{nullptr};
  http_request::HttpRequestUpdate *update_{nullptr};
  const char *model_{"TubesZB Z-Wave"};
  const char *board_{"ESP32"};
  std::mutex mutex_;
  std::string nonce_, mac_;
  std::string check_source_, verified_source_;
  Command command_{Command::NONE};
  std::string command_value_;
  bool action_pending_{false};  // Protected by mutex_, including the main-loop handoff.
  uint8_t gateway_ota_state_{0}, gateway_ota_error_{0};  // 0=idle, 1=installing, 2=verified, 3=failed/aborted
  float gateway_ota_progress_{-1.0f};
  bool checking_{false}, check_timeout_{false}, restart_pending_{false};
  uint32_t check_started_{0}, sampled_at_{0}, restart_at_{0};
};

}  // namespace esphome::tubeszb_web
