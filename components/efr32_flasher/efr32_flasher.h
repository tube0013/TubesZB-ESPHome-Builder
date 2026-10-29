// efr32_flasher.h - ESPHome external component for EFR32 (Gecko) flashing via XMODEM-CRC
#pragma once

#include "esphome/components/zigbee_firmware_baud/runtime_baud.h"
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/md5/md5.h"

#ifdef USE_ESP_IDF
  #include "esp_http_client.h"
  #include "esp_crt_bundle.h"
#else
  #error "EFR32 flasher requires ESP-IDF framework."
#endif

namespace esphome {
namespace efr32_flasher {

class EFR32Flasher : public Component {
 public:
  ~EFR32Flasher();
  void set_uart(esphome::uart::UARTComponent *u){ uart_ = u; }
  void set_bl_switch(esphome::switch_::Switch *s){ bl_sw_ = s; }
  void set_rst_switch(esphome::switch_::Switch *s){ rst_sw_ = s; }
  void set_pause_switch(esphome::switch_::Switch *s){ pause_sw_ = s; }
  void set_update_url(const std::string &u){ manifest_url_ = u; }
  void set_hardware_flow_control(bool enabled) { hardware_flow_control_ = enabled; }
  void set_verbose(bool v){ verbose_ = v; }
  void set_show_progress(bool v){ show_progress_ = v; }
  void set_progress_step(uint8_t s){ progress_step_ = s ? s : 5; }
  void set_runtime_baud_rate(uint32_t baud){ runtime_baud_rate_ = baud; application_baud_.manual(baud); }
  void set_baud_preference_hash(uint32_t hash){ application_baud_.set_preference_hash(hash); }
  void set_runtime_baud_select(select::Select *value){ application_baud_.set_selector(value); }
  bool is_applying_manifest_baud() const { return application_baud_.applying_manifest(); }
  void set_bootloader_baud_rate(uint32_t baud){ bootloader_baud_rate_ = baud; }
  void set_fw_text_sensor(esphome::text_sensor::TextSensor *t){ fw_text_ = t; }
  void set_latest_text_sensor(esphome::text_sensor::TextSensor *t){ latest_text_ = t; }
  void set_ezsp_protocol_text(esphome::text_sensor::TextSensor *t){ ezsp_protocol_text_ = t; }
  void set_installed_stack_text(esphome::text_sensor::TextSensor *t){ installed_stack_text_ = t; }
  void set_xncp_version_text(esphome::text_sensor::TextSensor *t){ xncp_version_text_ = t; }
  void set_ieee_text(esphome::text_sensor::TextSensor *t){ ieee_text_ = t; }
  void set_manuf_id_text(esphome::text_sensor::TextSensor *t){ manuf_id_text_ = t; }
  void set_board_name_text(esphome::text_sensor::TextSensor *t){ board_name_text_ = t; }
  void set_mfg_string_text(esphome::text_sensor::TextSensor *t){ mfg_string_text_ = t; }
  void set_chip_text(esphome::text_sensor::TextSensor *t){ chip_text_ = t; }
  void set_variant(uint8_t v){ variant_force_ = v; } // 0=auto,1=MGM24,2=BM24,3=model-specific flat manifest
  void set_busy_sensor(esphome::binary_sensor::BinarySensor *b){ busy_sensor_ = b; }

  void start_firmware_update(){ want_update_ = true; }
  void start_check_update(){ want_check_ = true; }
  void set_ezsp_bootloader(bool value) { ezsp_bootloader_ = value; }
  void start_bootloader() { want_bootloader_ = true; }
  bool bootloader_pending() const { return want_bootloader_ || bootloader_running_; }
  bool bootloader_succeeded() const { return bootloader_ready_; }
  void restore_runtime_baud() { bootloader_ready_ = false; apply_runtime_baud_(); }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE - 3.0f; }

 private:
  // HTTP helpers
  bool http_open_(const std::string &url, esp_http_client_handle_t &client, int timeout_ms=30000);
  bool fetch_manifest_(const std::string &url, std::string &fw_url_out);

  // Bootloader / XMODEM helpers
  bool enter_bootloader_();
  bool probe_bootloader_menu_(uint32_t timeout_ms);
  void run_bootloader_();
  void leave_bootloader_();
  bool await_char_(uint8_t expect, uint32_t timeout_ms);
  bool read_byte_(uint8_t &b, uint32_t timeout_ms=500);
  void flush_uart_();
  void delay_(uint32_t ms);
  void update_progress_(uint32_t total, uint32_t expected, uint32_t &last_pc);
  bool wait_for_ncp_start_(uint32_t ms);
  std::string detect_variant_key_();

  static uint16_t crc16_ccitt_(const uint8_t *data, size_t len){
    uint16_t crc = 0x0000; // XMODEM variant
    for(size_t i=0;i<len;i++){
      crc ^= (uint16_t)data[i] << 8;
      for(int j=0;j<8;j++) crc = (crc & 0x8000) ? (crc<<1) ^ 0x1021 : (crc<<1);
    }
    return crc;
  }
  bool xmodem_send_(esp_http_client_handle_t client, uint32_t content_len);

  // High level flows
  void run_update_();
  void run_check_update_();
  inline void set_busy_(bool on){ if (busy_sensor_) busy_sensor_->publish_state(on); }
  void apply_runtime_baud_();
  void apply_bootloader_baud_();
  void set_uart_baud_(uint32_t baud);

  // State
  esphome::uart::UARTComponent *uart_{nullptr};
  esphome::switch_::Switch *bl_sw_{nullptr};
  esphome::switch_::Switch *rst_sw_{nullptr};
  esphome::switch_::Switch *pause_sw_{nullptr};
  esphome::text_sensor::TextSensor *fw_text_{nullptr};
  esphome::text_sensor::TextSensor *latest_text_{nullptr};
  esphome::text_sensor::TextSensor *ezsp_protocol_text_{nullptr};
  esphome::text_sensor::TextSensor *installed_stack_text_{nullptr};
  esphome::text_sensor::TextSensor *xncp_version_text_{nullptr};
  esphome::text_sensor::TextSensor *ieee_text_{nullptr};
  esphome::text_sensor::TextSensor *manuf_id_text_{nullptr};
  esphome::text_sensor::TextSensor *board_name_text_{nullptr};
  esphome::text_sensor::TextSensor *mfg_string_text_{nullptr};
  esphome::text_sensor::TextSensor *chip_text_{nullptr};
  esphome::binary_sensor::BinarySensor *busy_sensor_{nullptr};
  std::string manifest_url_;
  bool want_update_{false};
  bool want_check_{false};
  bool ezsp_bootloader_{false}, want_bootloader_{false}, bootloader_running_{false}, bootloader_ready_{false};
  bool verbose_{false};
  bool hardware_flow_control_{true};
  bool show_progress_{true};
  uint8_t progress_step_{5};
  uint32_t runtime_baud_rate_{0};
  uint32_t bootloader_baud_rate_{115200};
  std::string manifest_version_{};
  std::string expected_md5_{};
  uint32_t expected_size_{0};
  uint8_t variant_force_{0};
  std::string variant_key_override_{};
  zigbee_firmware_baud::RuntimeBaud application_baud_;
  uint32_t manifest_baudrate_{0};
};

class UpdateFirmwareAction : public esphome::Action<> {
 public:
  void set_parent(EFR32Flasher *p){ parent_ = p; }
  void play() override { if(parent_) parent_->start_firmware_update(); }
 private:
  EFR32Flasher *parent_{nullptr};
};

class CheckUpdateAction : public esphome::Action<> {
 public:
  void set_parent(EFR32Flasher *p){ parent_ = p; }
  void play() override { if(parent_) parent_->start_check_update(); }
 private:
  EFR32Flasher *parent_{nullptr};
};

class EnterBootloaderAction : public esphome::Action<> {
 public:
  void set_parent(EFR32Flasher *parent) { parent_ = parent; }
  void play() override { if (parent_ != nullptr) parent_->start_bootloader(); }
 private:
  EFR32Flasher *parent_{nullptr};
};

// Old probing actions removed to keep component lean

} // namespace efr32_flasher
} // namespace esphome
