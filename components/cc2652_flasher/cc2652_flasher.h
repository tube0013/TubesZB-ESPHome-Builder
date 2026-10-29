#include "esphome/components/zigbee_firmware_baud/runtime_baud.h"
// cc2652_flasher.h - ESPHome external component for CC2652 flashing
#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <unordered_set>
#include <ArduinoJson.h>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/md5/md5.h"

#ifdef USE_ESP_IDF
  #include "esp_http_client.h"
  #include "esp_crt_bundle.h"
#else
  #error "This component is ESP-IDF only."
#endif

namespace esphome {
namespace cc2652_flasher {

class CC2652Flasher : public Component {
 public:
  ~CC2652Flasher();
  void set_uart(esphome::uart::UARTComponent *u){ uart_ = u; }
  void set_bsl_switch(esphome::switch_::Switch *s){ bsl_sw_ = s; }
  void set_rst_switch(esphome::switch_::Switch *s){ rst_sw_ = s; }
  void set_update_url(const std::string &u){ manifest_url_ = u; }
  void set_boot_baud(uint32_t b){ boot_baud_ = b; }
  void set_restore_baud(uint32_t b){ restore_baud_rate_ = b; }
  void set_runtime_baud_rate(uint32_t b){ restore_baud_rate_ = b; application_baud_.manual(b); }
  void set_baud_preference_hash(uint32_t hash){ application_baud_.set_preference_hash(hash); }
  void set_runtime_baud_select(select::Select *value){ application_baud_.set_selector(value); }
  bool is_applying_manifest_baud() const { return application_baud_.applying_manifest(); }
  void set_erase_mode(uint8_t m){ erase_mode_ = m; }  // 0=sector,1=bank,2=auto
  void set_verbose(bool v){ verbose_ = v; }
  void set_show_progress(bool v){ show_progress_ = v; }
  void set_progress_step(uint8_t s){ progress_step_ = s ? s : 5; }
  void set_fw_text_sensor(esphome::text_sensor::TextSensor *t){ fw_text_ = t; }
  void set_ieee_text_sensor(esphome::text_sensor::TextSensor *t){ ieee_text_ = t; }
  void set_latest_text_sensor(esphome::text_sensor::TextSensor *t){ latest_text_ = t; }
  void set_variant_text_sensor(esphome::text_sensor::TextSensor *t){ variant_text_ = t; }
  void set_check_on_boot(bool v){ check_on_boot_ = v; }
  void set_check_interval_days(uint16_t d){ if(d==0) d=7; check_interval_ms_ = (uint32_t)d * 24u * 60u * 60u * 1000u; }
  void set_variant(uint8_t v){ variant_force_ = v; }
  void set_detect_on_boot(bool v){ detect_on_boot_ = v; }
  void set_detect_on_boot_delay_ms(uint16_t d){ detect_on_boot_delay_ms_ = d; }
  void start_firmware_update(){ want_update_ = true; }
  void start_erase_only(uint8_t mode){ want_erase_ = true; erase_request_mode_ = mode; }
  void start_check_update(){ want_check_ = true; }
  void start_detect_variant(){ want_detect_ = true; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override;

 private:
  // internal helpers and state (defined in header for header-only implementation)
  static inline uint8_t sum8_(const uint8_t *d, size_t n){ uint32_t s=0; for(size_t i=0;i<n;i++) s+=d[i]; return (uint8_t)(s&0xFF); }
  static inline bool ends_with_(const std::string &s, const char *suf){ size_t n=std::strlen(suf); return s.size()>=n && s.compare(s.size()-n,n,suf)==0; }
  static inline uint8_t hex2_(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='A'&&c<='F')return c-'A'+10; if(c>='a'&&c<='f')return c-'a'+10; return 0; }
  static inline void put_u16_le_(std::vector<uint8_t>&v,uint16_t x){ v.push_back(x&0xFF); v.push_back((x>>8)&0xFF); }
  static inline void put_u32_le_(std::vector<uint8_t>&v,uint32_t x){ v.push_back(x&0xFF); v.push_back((x>>8)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>24)&0xFF); }
  static inline void put_u32_be_(std::vector<uint8_t>&v,uint32_t x){ v.push_back((x>>24)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>8)&0xFF); v.push_back(x&0xFF); }
  inline void feed_(){ esphome::App.feed_wdt(); }
  void delay_(uint32_t ms);
  void log_bytes_(const char* prefix, const uint8_t* data, size_t len);
  void flush_uart_();
  bool read_exact_(uint8_t *dst, size_t n, uint32_t timeout_ms=500);
  bool http_open_(const std::string &url, esp_http_client_handle_t &client, int timeout_ms=30000);
  bool fetch_manifest_(const std::string &url, std::string &fw_url_out);
  static inline uint8_t sum8_(const std::vector<uint8_t>&v){ return sum8_(v.data(), v.size()); }
  bool send_packet_(const std::vector<uint8_t> &payload, uint32_t ack_timeout=1500);
  bool recv_status_snoop_(uint8_t &status, uint32_t total_timeout_ms=1200, uint32_t ib_gap_ms=15);
  bool recv_payload_(std::vector<uint8_t> &payload, uint32_t total_timeout_ms=1500, uint32_t ib_gap_ms=15);
  static inline uint8_t calc_checks_(uint8_t cmd, uint32_t addr, uint32_t size);
  bool bsl_download_(uint32_t addr, uint32_t total_size);
  bool bsl_send_data_(const uint8_t *data, size_t len);
  bool bsl_mem_read32_(uint32_t addr, uint32_t &out);
  bool bsl_get_chip_id_(uint16_t &chip_id);
  bool get_status_raw_(uint8_t &st);
  bool get_status_();
  bool wait_status_success_(uint32_t total_ms, uint32_t poll_ms=200);
  bool bsl_send_simple_(uint8_t cmd, uint32_t status_wait_ms=0);
  bool sector_erase_one_(uint32_t page_addr);
  bool erase_entire_flash_by_sector_();
  bool bank_erase_();
  bool verify_erased_quick_();
  bool ensure_erased_(uint32_t start_addr, size_t len);
  bool program_from_bin_(const std::string &url);
  bool program_from_hex_(const std::string &url);
  static inline uint8_t znp_fcs_(uint8_t len, uint8_t cmd1, uint8_t cmd2, const std::vector<uint8_t> &data){ uint8_t f=len^cmd1^cmd2; for(auto b:data) f^=b; return f; }
  bool znp_send_(uint8_t cmd1, uint8_t cmd2, const std::vector<uint8_t> &data);
  bool znp_recv_(uint8_t &cmd1, uint8_t &cmd2, std::vector<uint8_t> &data, uint32_t timeout_ms=1000);
  bool znp_req_resp_(uint8_t sreq_cmd1, uint8_t sreq_cmd2, const std::vector<uint8_t> &payload, uint8_t expect_cmd1, uint8_t expect_cmd2, std::vector<uint8_t> &out, uint8_t tries=6, uint32_t wait_ms=120);
  void update_progress_(uint32_t total, uint32_t expected, uint32_t &last_pc);
  void query_znp_info_();
  void enter_bootloader_();
  void leave_bootloader_();
  void restore_baud_();
  bool bsl_autobaud_(uint32_t timeout_ms=800);
  void run_erase_(uint8_t mode);
  void run_update_();
  void run_check_update_();
  void run_detect_variant_();
  bool detect_variant_via_bsl_();

  esphome::uart::UARTComponent *uart_{nullptr};
  esphome::switch_::Switch *bsl_sw_{nullptr};
  esphome::switch_::Switch *rst_sw_{nullptr};
  esphome::text_sensor::TextSensor *fw_text_{nullptr};
  esphome::text_sensor::TextSensor *ieee_text_{nullptr};
  esphome::text_sensor::TextSensor *latest_text_{nullptr};
  esphome::text_sensor::TextSensor *variant_text_{nullptr};
  std::string manifest_url_;
  uint32_t boot_baud_{0};
  uint32_t restore_baud_rate_{0};
  bool want_update_{false};
  bool want_erase_{false};
  bool want_check_{false};
  bool want_detect_{false};
  std::unordered_set<uint32_t> erased_pages_;
  uint8_t erase_mode_{0};
  uint8_t last_status_{0xFF};
  uint8_t erase_request_mode_{2};
  std::string expected_md5_{};
  uint32_t expected_size_{0};
  std::string manifest_version_{};
  bool bank_erased_all_{false};
  bool verbose_{false};
  bool show_progress_{true};
  uint8_t progress_step_{5};
  bool check_on_boot_{true};
  uint32_t check_interval_ms_{604800000u};
  uint8_t variant_force_{0};
  uint8_t variant_detected_{0};
  uint8_t znp_product_{0xFF};
  bool detect_on_boot_{true};
  uint16_t detect_on_boot_delay_ms_{0};
  zigbee_firmware_baud::RuntimeBaud application_baud_;
  uint32_t manifest_baudrate_{0};
};

class UpdateFirmwareAction : public esphome::Action<> {
 public:
  void set_parent(CC2652Flasher *p){ parent_ = p; }
  void play() override { if(parent_) parent_->start_firmware_update(); }
 private:
  CC2652Flasher *parent_{nullptr};
};

class EraseFlashAction : public esphome::Action<> {
 public:
  void set_parent(CC2652Flasher *p){ parent_ = p; }
  void set_mode(uint8_t m){ mode_ = m; }
  void play() override { if(parent_) parent_->start_erase_only(mode_); }
 private:
  CC2652Flasher *parent_{nullptr};
  uint8_t mode_{2};
};

class CheckUpdateAction : public esphome::Action<> {
 public:
  void set_parent(CC2652Flasher *p){ parent_ = p; }
  void play() override { if(parent_) parent_->start_check_update(); }
 private:
  CC2652Flasher *parent_{nullptr};
};

class DetectVariantAction : public esphome::Action<> {
 public:
  void set_parent(CC2652Flasher *p){ parent_ = p; }
  void play() override { if(parent_) parent_->start_detect_variant(); }
 private:
  CC2652Flasher *parent_{nullptr};
};

} // namespace cc2652_flasher
} // namespace esphome
