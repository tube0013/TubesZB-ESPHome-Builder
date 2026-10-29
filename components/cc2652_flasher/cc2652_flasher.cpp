#include "esphome/components/zigbee_firmware_baud/manifest_baud.h"
#include "esphome/components/zigbee_firmware_baud/runtime_baud.h"

// ====== CC2652 Flasher (external component) ======
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

static const char *const TAG = "cc2652_flasher";

static constexpr uint8_t SBL_ACK_00  = 0x00;
static constexpr uint8_t SBL_ACK_CC  = 0xCC;
static constexpr uint8_t SBL_NACK_33 = 0x33;

enum : uint8_t {
  CMD_PING          = 0x20,
  CMD_GET_STATUS    = 0x23,
  CMD_RESET         = 0x25,
  CMD_SECTOR_ERASE  = 0x26,
  CMD_BANK_ERASE    = 0x2C,
  CMD_MEMORY_WRITE  = 0x2B,
};

enum : uint8_t {
  STAT_SUCCESS      = 0x40,
  STAT_UNKNOWN_CMD  = 0x41,
  STAT_INVALID_CMD  = 0x42,
  STAT_INVALID_ADDR = 0x43,
  STAT_FLASH_FAIL   = 0x44,
};

static constexpr uint32_t FLASH_PAGE_SIZE = 4096;  // 4 KB

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

  void setup() override {
    application_baud_.setup(restore_baud_rate_ != 0 ? restore_baud_rate_ : uart_->get_baud_rate());
    restore_baud_rate_ = application_baud_.current();
    restore_baud_();
    // Optionally detect variant very early at boot
    if (detect_on_boot_) {
      if (detect_on_boot_delay_ms_ == 0) {
        this->run_detect_variant_();
      } else {
        this->set_timeout(detect_on_boot_delay_ms_, [this](){ this->start_detect_variant(); });
      }
    }
    // Delay ZNP query slightly so the Zigbee radio finishes booting.
    this->set_timeout(700, [this](){ this->query_znp_info_(); });
    // Second attempt later in case the first was too early.
    this->set_timeout(4500, [this](){ this->query_znp_info_(); });
    if (check_on_boot_) this->set_timeout(4000, [this](){ this->start_check_update(); });
    // Weekly check by default
    this->set_interval(check_interval_ms_, [this](){ this->start_check_update(); });
  }
  void loop() override {
    if (want_detect_) {
      want_detect_ = false;
      run_detect_variant_();
      return;
    }
    if (want_check_) {
      want_check_ = false;
      run_check_update_();
      return;
    }
    if (want_erase_) {
      want_erase_ = false;
      run_erase_(erase_request_mode_);
      return;
    }
    if(!want_update_) return;
    want_update_ = false;
    run_update_();
  }
  float get_setup_priority() const override { return setup_priority::HARDWARE - 3.0f; }

 private:
  // ---------- helpers ----------
  static inline uint8_t sum8_(const uint8_t *d, size_t n){ uint32_t s=0; for(size_t i=0;i<n;i++) s+=d[i]; return (uint8_t)(s&0xFF); }
  static inline bool ends_with_(const std::string &s, const char *suf){ size_t n=std::strlen(suf); return s.size()>=n && s.compare(s.size()-n,n,suf)==0; }
  static inline uint8_t hex2_(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='A'&&c<='F')return c-'A'+10; if(c>='a'&&c<='f')return c-'a'+10; return 0; }
  static inline void put_u16_le_(std::vector<uint8_t>&v,uint16_t x){ v.push_back(x&0xFF); v.push_back((x>>8)&0xFF); }
  static inline void put_u32_le_(std::vector<uint8_t>&v,uint32_t x){ v.push_back(x&0xFF); v.push_back((x>>8)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>24)&0xFF); }
  static inline void put_u32_be_(std::vector<uint8_t>&v,uint32_t x){ v.push_back((x>>24)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>8)&0xFF); v.push_back(x&0xFF); }

  inline void feed_(){ esphome::App.feed_wdt(); }
  void delay_(uint32_t ms){
    uint32_t start = millis();
    while (millis() - start < ms) { esphome::App.feed_wdt(); esphome::delay(1); }
  }

  void log_bytes_(const char* prefix, const uint8_t* data, size_t len) {
    if (!data && len) return;
    if (len == 0) { ESP_LOGV(TAG, "%s <empty>", prefix); return; }
    char line[140];
    size_t i = 0;
    while (i < len) {
      int pos = 0;
      pos += snprintf(line+pos, sizeof(line)-pos, "%s len=%u: ", prefix, (unsigned)len);
      size_t chunk = std::min((size_t)16, len - i);
      for (size_t j = 0; j < chunk && pos < (int)sizeof(line)-4; j++)
        pos += snprintf(line+pos, sizeof(line)-pos, "%02X ", data[i+j]);
      ESP_LOGV(TAG, "%s", line);
      i += chunk;
      feed_();
    }
  }

  void flush_uart_(){
    uint8_t b;
    for(int i=0;i<64;i++){
      bool any=false;
      while(uart_->available()){ uart_->read_byte(&b); any=true; feed_(); }
      if(!any) break;
      delay_(2);
    }
  }

  bool read_exact_(uint8_t *dst, size_t n, uint32_t timeout_ms=500){
    uint32_t start = millis(); size_t got=0;
    while(got<n){
      if(uart_->available()){
        if(uart_->read_byte(dst+got)) { got++; feed_(); }
      }else{
        if(millis()-start > timeout_ms) return false;
        delay_(1);
        feed_();
      }
    }
    return true;
  }

  // ---------- HTTP ----------
  bool http_open_(const std::string &url, esp_http_client_handle_t &client, int timeout_ms=4000){
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = timeout_ms;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;   // verify with built-in bundle
    cfg.disable_auto_redirect = false;

    client = esp_http_client_init(&cfg);
    if(!client) return false;
    feed_(); // Note: WDT feed in between each http_client call might be needed
    if(esp_http_client_open(client, 0) != ESP_OK) return false;

    (void)esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if(status != 200){
      char first[256]; int n = esp_http_client_read(client, first, sizeof(first));
      ESP_LOGE(TAG,"HTTP status %d, first bytes: %.*s", status, n>0?n:0, first);
      esp_http_client_close(client); esp_http_client_cleanup(client);
      return false;
    }
    return true;
  }

 bool fetch_manifest_(const std::string &url, std::string &fw_url_out){
    manifest_baudrate_ = 0;
    esp_http_client_handle_t client;
    if(!http_open_(url, client)) return false;

    std::string body; body.reserve(1024);
    char buf[1024]; int r;
    while((r = esp_http_client_read(client, buf, sizeof(buf))) > 0){
      body.append(buf, r);
      feed_();
    }
    esp_http_client_close(client); esp_http_client_cleanup(client);

    JsonDocument doc;
    auto err = deserializeJson(doc, body);
    if(err){ ESP_LOGE(TAG,"JSON parse error: %s", err.c_str()); return false; }

    // Reset previously parsed fields
    fw_url_out.clear(); expected_md5_.clear(); expected_size_ = 0; manifest_version_.clear();

    JsonVariantConst selected_image = doc.as<JsonVariantConst>();
    // Helper to extract URL/md5/size/version from a JsonVariant
    auto extract_fields = [&](JsonVariantConst v)->bool{
      if (v.isNull()) return false;
      if (v["fw_url"].is<const char*>()) fw_url_out = v["fw_url"].as<const char*>();
      else if (v["firmware_url"].is<const char*>()) fw_url_out = v["firmware_url"].as<const char*>();
      else if (v["url"].is<const char*>()) fw_url_out = v["url"].as<const char*>();
      else return false;
      selected_image = v;
      // Optional metadata
      if (v["md5"].is<const char*>()) expected_md5_ = v["md5"].as<const char*>();
      else if (v["md5sum"].is<const char*>()) expected_md5_ = v["md5sum"].as<const char*>();
      else if (v["md5_hex"].is<const char*>()) expected_md5_ = v["md5_hex"].as<const char*>();
      if (v["size"].is<uint32_t>()) expected_size_ = v["size"].as<uint32_t>();
      else if (v["size"].is<const char*>()) { const char* s=v["size"].as<const char*>(); uint32_t tmp=0; for(const char* p=s; *p; ++p){ if(*p<'0'||*p>'9'){ tmp=0; break;} tmp=tmp*10+(*p-'0'); } if(tmp) expected_size_=tmp; }
      else if (v["length"].is<uint32_t>()) expected_size_ = v["length"].as<uint32_t>();
      else if (v["length"].is<const char*>()) { const char* s=v["length"].as<const char*>(); uint32_t tmp=0; for(const char* p=s; *p; ++p){ if(*p<'0'||*p>'9'){ tmp=0; break;} tmp=tmp*10+(*p-'0'); } if(tmp) expected_size_=tmp; }
      if (v["version"].is<const char*>()) manifest_version_ = v["version"].as<const char*>();
      else if (v["code_revision"].is<const char*>()) manifest_version_ = v["code_revision"].as<const char*>();
      else if (v["rev"].is<const char*>()) manifest_version_ = v["rev"].as<const char*>();
      return true;
    };

    // Variant-aware selection
    bool selected = false;
    uint8_t vsel = choose_variant_();

    if (doc["variants"].is<JsonArrayConst>()){
      for (JsonVariantConst it : doc["variants"].as<JsonArrayConst>()){
        // match rules: { match: { product: [..], variant: "p2"|"p7" }, url/md5/size/... }
        bool match = false;
        if (it["match"].is<JsonObjectConst>()){
          auto m = it["match"].as<JsonObjectConst>();
          if (m["product"].is<JsonArrayConst>()){
            for (JsonVariantConst pv : m["product"].as<JsonArrayConst>()){
              if (pv.is<uint32_t>()) { if ((uint8_t)pv.as<uint32_t>() == znp_product_) match = true; }
            }
          }
          if (!match && m["variant"].is<const char*>()){
            std::string vs = m["variant"].as<const char*>();
            std::transform(vs.begin(), vs.end(), vs.begin(), ::tolower);
            if ((vs == "p7" && vsel == 7) || (vs == "p2" && vsel == 2)) match = true;
          }
        }
        // also allow simple keys at top-level of variant entry
        if (!match && it["product"].is<uint32_t>()){
          if ((uint8_t)it["product"].as<uint32_t>() == znp_product_) match = true;
        }
        if (!match && it["variant"].is<const char*>()){
          std::string vs = it["variant"].as<const char*>();
          std::transform(vs.begin(), vs.end(), vs.begin(), ::tolower);
          if ((vs == "p7" && vsel == 7) || (vs == "p2" && vsel == 2)) match = true;
        }
        if (match && extract_fields(it)) { selected = true; break; }
      }
      if (!selected) {
        // Fallback: if exactly one variant entry exists, accept it
        auto arr = doc["variants"].as<JsonArrayConst>();
        if (arr.size() == 1) {
          JsonVariantConst only = *arr.begin();
          if (!extract_fields(only)) {
            ESP_LOGE(TAG, "Manifest single-variant entry missing URL.");
            return false;
          }
          selected = true;
          ESP_LOGW(TAG, "Variant unknown; using the only variant provided in manifest.");
        }
        if (!selected) {
          if (doc["version"].is<const char*>()) manifest_version_ = doc["version"].as<const char*>();
          ESP_LOGE(TAG, "Manifest variants present but no applicable key (need 'p2' or 'p7').");
          // Keep version for checks but signal failure for URL selection
          return false;
        }
      }
      // If we selected a variant but it didn't carry a version, fall back to root version
      if (selected && manifest_version_.empty()) {
        if (doc["version"].is<const char*>()) manifest_version_ = doc["version"].as<const char*>();
        else if (doc["code_revision"].is<const char*>()) manifest_version_ = doc["code_revision"].as<const char*>();
        else if (doc["rev"].is<const char*>()) manifest_version_ = doc["rev"].as<const char*>();
      }
    } else if (doc["variants"].is<JsonObjectConst>()){
      auto obj = doc["variants"].as<JsonObjectConst>();
      JsonVariantConst choice;
      if (vsel == 7 && obj["p7"].is<JsonVariantConst>()) choice = obj["p7"];
      else if (vsel == 2 && obj["p2"].is<JsonVariantConst>()) choice = obj["p2"];
      else if (obj.size() == 1) { for (auto kv : obj) { choice = kv.value(); break; } }
      if (!choice.isNull()) { if (!extract_fields(choice)) { ESP_LOGE(TAG, "Variant entry missing URL."); return false; } selected = true; }
      else {
        if (doc["version"].is<const char*>()) manifest_version_ = doc["version"].as<const char*>();
        ESP_LOGE(TAG, "Manifest variants present but no applicable key (need 'p2' or 'p7').");
        return false;
      }
      // If chosen variant didn't have a version, fall back to root version
      if (selected && manifest_version_.empty()) {
        if (doc["version"].is<const char*>()) manifest_version_ = doc["version"].as<const char*>();
        else if (doc["code_revision"].is<const char*>()) manifest_version_ = doc["code_revision"].as<const char*>();
        else if (doc["rev"].is<const char*>()) manifest_version_ = doc["rev"].as<const char*>();
      }
    }

    // Legacy multi-key support
    if (!selected && (doc["fw_url_p7"].is<const char*>() || doc["fw_url_p2"].is<const char*>())){
      if (vsel == 7 && doc["fw_url_p7"].is<const char*>()) fw_url_out = doc["fw_url_p7"].as<const char*>();
      else if (vsel == 2 && doc["fw_url_p2"].is<const char*>()) fw_url_out = doc["fw_url_p2"].as<const char*>();
      else if (vsel == 0 && doc["fw_url_p7"].is<const char*>()) fw_url_out = doc["fw_url_p7"].as<const char*>();
      else if (vsel == 0 && doc["fw_url_p2"].is<const char*>()) fw_url_out = doc["fw_url_p2"].as<const char*>();
      selected = !fw_url_out.empty();
      // Optional shared metadata at root
      if (doc["md5"].is<const char*>()) expected_md5_ = doc["md5"].as<const char*>();
      if (doc["size"].is<uint32_t>()) expected_size_ = doc["size"].as<uint32_t>();
      if (doc["version"].is<const char*>()) manifest_version_ = doc["version"].as<const char*>();
    }

    // Simple/legacy single URL
    if (!selected){
      if (doc["fw_url"].is<const char*>()) fw_url_out = doc["fw_url"].as<const char*>();
      else if (doc["firmware_url"].is<const char*>()) fw_url_out = doc["firmware_url"].as<const char*>();
      else if (doc["url"].is<const char*>()) fw_url_out = doc["url"].as<const char*>();
      else { ESP_LOGE(TAG,"Manifest missing firmware URL (fw_url/firmware_url/url)."); return false; }

      if (doc["md5"].is<const char*>()) expected_md5_ = doc["md5"].as<const char*>();
      else if (doc["md5sum"].is<const char*>()) expected_md5_ = doc["md5sum"].as<const char*>();
      else if (doc["md5_hex"].is<const char*>()) expected_md5_ = doc["md5_hex"].as<const char*>();
      if (doc["size"].is<uint32_t>()) expected_size_ = doc["size"].as<uint32_t>();
      else if (doc["size"].is<const char*>()) { const char* s=doc["size"].as<const char*>(); uint32_t tmp=0; for(const char* p=s; *p; ++p){ if(*p<'0'||*p>'9'){ tmp=0; break;} tmp=tmp*10+(*p-'0'); } if(tmp) expected_size_=tmp; }
      else if (doc["length"].is<uint32_t>()) expected_size_ = doc["length"].as<uint32_t>();
      else if (doc["length"].is<const char*>()) { const char* s=doc["length"].as<const char*>(); uint32_t tmp=0; for(const char* p=s; *p; ++p){ if(*p<'0'||*p>'9'){ tmp=0; break;} tmp=tmp*10+(*p-'0'); } if(tmp) expected_size_=tmp; }
      if (doc["version"].is<const char*>()) manifest_version_ = doc["version"].as<const char*>();
      else if (doc["code_revision"].is<const char*>()) manifest_version_ = doc["code_revision"].as<const char*>();
      else if (doc["rev"].is<const char*>()) manifest_version_ = doc["rev"].as<const char*>();
    }

    if (fw_url_out.size() >= 4) {
      std::string l = fw_url_out;
      std::transform(l.begin(), l.end(), l.begin(), ::tolower);
      if (l.rfind(".hex") == l.size()-4) {
        ESP_LOGW(TAG, "Manifest points to an Intel HEX file. Flasher will stream & parse HEX directly.");
      }
    }
    if (!zigbee_firmware_baud::read_baud(selected_image, doc.as<JsonVariantConst>(), manifest_baudrate_) ||
        (manifest_baudrate_ != 0 && !application_baud_.supports(manifest_baudrate_))) {
      ESP_LOGE(TAG, "Invalid or unsupported manifest baudrate; expected a supported integer application baud");
      return false;
    }
    if (manifest_baudrate_ != 0) ESP_LOGI(TAG, "Manifest application baudrate=%u", unsigned(manifest_baudrate_));
    if (!expected_md5_.empty()) ESP_LOGI(TAG, "Manifest MD5=%s", expected_md5_.c_str());
    if (expected_size_) ESP_LOGI(TAG, "Manifest size=%u", (unsigned)expected_size_);
    if (!manifest_version_.empty()) ESP_LOGI(TAG, "Manifest version=%s", manifest_version_.c_str());
    if (vsel == 2) ESP_LOGI(TAG, "Variant selected: CC2652P2");
    else if (vsel == 7) ESP_LOGI(TAG, "Variant selected: CC2652P7");
    else ESP_LOGI(TAG, "Variant selected: unknown/legacy");
    return true;
  }

  // ---------- Framing & status ----------
  static inline uint8_t sum8_(const std::vector<uint8_t>&v){ return sum8_(v.data(), v.size()); }

  bool send_packet_(const std::vector<uint8_t> &payload, uint32_t ack_timeout=1500){
    uint8_t size = (uint8_t)(payload.size() + 2);
    uint8_t cks  = sum8_(payload);

    if (verbose_) {
      ESP_LOGV(TAG, "TX frame: LEN=%u CKS=%02X", size, cks);
      log_bytes_("TX payload", payload.data(), payload.size());
    }

    uart_->write_byte(size); feed_();
    uart_->write_byte(cks);  feed_();
    if(!payload.empty()) { uart_->write_array(payload.data(), payload.size()); feed_(); }

    // Expect 2-byte sequence: 0x00 followed by 0xCC (ACK) or 0x33 (NACK)
    uint8_t a0=0xFF, a1=0xFF;
    uint32_t start = millis();
    while (millis() - start < ack_timeout) {
      if (a0 == 0xFF) {
        if (uart_->available() && uart_->read_byte(&a0)) { feed_(); continue; }
      } else if (a1 == 0xFF) {
        if (uart_->available() && uart_->read_byte(&a1)) { break; }
      }
      delay_(1); feed_();
    }
    if (a0 == 0xFF || a1 == 0xFF) {
      ESP_LOGE(TAG,"TX: no ACK/NACK within %u ms (got a0=0x%02X a1=0x%02X)", ack_timeout, a0, a1);
      return false;
    }
    ESP_LOGV(TAG,"TX: ACK seq a0=0x%02X a1=0x%02X", a0, a1);
    if (a0 != 0x00) {
      ESP_LOGW(TAG, "Unexpected first ACK byte 0x%02X (expect 0x00)", a0);
    }
    return (a1 == SBL_ACK_CC);
  }

  bool recv_status_snoop_(uint8_t &status, uint32_t total_timeout_ms=1200, uint32_t ib_gap_ms=15){
    std::vector<uint8_t> raw; raw.reserve(16);

    uint32_t start = millis();
    while (raw.empty()) {
      if (uart_->available()) { uint8_t b; uart_->read_byte(&b); raw.push_back(b); break; }
      if (millis() - start > total_timeout_ms) { ESP_LOGE(TAG,"RX: status timeout (no first byte)"); return false; }
      delay_(1); feed_();
    }

    uint32_t last = millis();
    while (millis() - start < total_timeout_ms) {
      if (uart_->available()) { uint8_t b; uart_->read_byte(&b); raw.push_back(b); last = millis(); feed_(); }
      else { if (millis() - last >= ib_gap_ms) break; delay_(1); feed_(); }
    }

    if (!raw.empty() && verbose_) log_bytes_("RX raw (snoop)", raw.data(), raw.size());

    auto buf = raw;
    while (!buf.empty() && (buf[0] == SBL_ACK_CC || buf[0] == SBL_ACK_00)) buf.erase(buf.begin());

    if (buf.empty()) {
      uint32_t extra_start = millis();
      while (millis() - extra_start < 60) {
        if (uart_->available()) { uint8_t b; uart_->read_byte(&b); buf.push_back(b); feed_(); }
        else { delay_(1); feed_(); }
      }
      while (!buf.empty() && (buf[0] == SBL_ACK_CC || buf[0] == SBL_ACK_00)) buf.erase(buf.begin());
    }

    if (buf.empty()) { ESP_LOGE(TAG,"RX: only stray ACKs, no status"); return false; }

    if (buf.size()==1 && buf[0] >= STAT_SUCCESS && buf[0] <= STAT_FLASH_FAIL) { status = buf[0]; ESP_LOGV(TAG,"RX unframed status: 0x%02X", status); return true; }

    if (buf.size() >= 2) {
      uint8_t size = buf[0];
      if (size >= 2 && buf.size() >= size) {
        uint8_t cks = buf[1];
        std::vector<uint8_t> payload(buf.begin()+2, buf.begin()+size);
        uint8_t calc = sum8_(payload);
        if (verbose_) {
          ESP_LOGV(TAG,"RX frame(snoop): LEN=%u CKS=%02X (calc=%02X)", size, cks, calc);
          if (!payload.empty()) log_bytes_("RX payload", payload.data(), payload.size());
        }
        if (calc != cks) {
          // Host NACK: 0x00 0x33
          uart_->write_byte(0x00); uart_->write_byte(SBL_NACK_33);
          ESP_LOGE(TAG,"RX: checksum mismatch (snoop)");
          return false;
        }
        // Host ACK: 0x00 0xCC
        uart_->write_byte(0x00); uart_->write_byte(SBL_ACK_CC);
        if (payload.size() == 1) { status = payload[0]; return true; }
        ESP_LOGE(TAG,"RX: framed status len %u (snoop)", (unsigned)payload.size());
        return false;
      }
    }
    ESP_LOGE(TAG,"RX: couldn't interpret status (bytes=%u)", (unsigned)buf.size());
    return false;
  }

  // Receive a generic framed payload (not status), acknowledge and return payload
  bool recv_payload_(std::vector<uint8_t> &payload, uint32_t total_timeout_ms=1500, uint32_t ib_gap_ms=15){
    std::vector<uint8_t> raw; raw.reserve(32);
    uint32_t start = millis();
    while (raw.empty()) {
      if (uart_->available()) { uint8_t b; uart_->read_byte(&b); raw.push_back(b); break; }
      if (millis() - start > total_timeout_ms) { ESP_LOGE(TAG,"RX: payload timeout (no first byte)"); return false; }
      delay_(1); feed_();
    }
    uint32_t last = millis();
    while (millis() - start < total_timeout_ms) {
      if (uart_->available()) { uint8_t b; uart_->read_byte(&b); raw.push_back(b); last = millis(); feed_(); }
      else { if (millis() - last >= ib_gap_ms) break; delay_(1); feed_(); }
    }
    if (!raw.empty()) log_bytes_("RX raw", raw.data(), raw.size());
    if (raw.size() < 2) { ESP_LOGE(TAG, "RX: too short"); return false; }
    uint8_t size = raw[0];
    uint8_t cks  = raw[1];
    if (size < 2 || raw.size() < size) { ESP_LOGE(TAG, "RX: incomplete frame size=%u raw=%u", size, (unsigned)raw.size()); return false; }
    payload.assign(raw.begin()+2, raw.begin()+size);
    uint8_t calc = sum8_(payload);
    ESP_LOGV(TAG,"RX frame: LEN=%u CKS=%02X (calc=%02X)", size, cks, calc);
    if (!payload.empty()) log_bytes_("RX payload", payload.data(), payload.size());
    if (cks != calc) { uart_->write_byte(0x00); uart_->write_byte(SBL_NACK_33); ESP_LOGE(TAG, "RX: checksum mismatch"); return false; }
    uart_->write_byte(0x00); uart_->write_byte(SBL_ACK_CC);
    return true;
  }

  // ---------- Low-level commands (CC26xx style, big-endian addresses) ----------
  static inline uint8_t calc_checks_(uint8_t cmd, uint32_t addr, uint32_t size){
    std::vector<uint8_t> tmp; tmp.reserve(1+4+4);
    tmp.push_back(cmd);
    put_u32_be_(tmp, addr);
    put_u32_be_(tmp, size);
    tmp.erase(tmp.begin()); // remove cmd from sum per SBL spec
    return sum8_(tmp);
  }

  bool bsl_download_(uint32_t addr, uint32_t total_size){
    // 0x21 DOWNLOAD: length=11, checksum=sum(addr_be + size_be + cmd) & 0xFF
    if ((total_size & 3u) != 0) { ESP_LOGE(TAG, "DOWNLOAD size must be multiple of 4 (%u)", (unsigned)total_size); return false; }
    std::vector<uint8_t> p; p.reserve(1+4+4);
    p.push_back(0x21);
    put_u32_be_(p, addr);
    put_u32_be_(p, total_size);
    if(!send_packet_(p)) return false;
    // Some bootloaders send an immediate framed response; others don't.
    // Don't require a frame; just check status.
    return get_status_();
  }

  bool bsl_send_data_(const uint8_t *data, size_t len){
    // 0x24 SEND DATA: length=len+3, checksum=sum(cmd + data[]) & 0xFF
    if (len == 0) return true;
    if (len > 252) { ESP_LOGE(TAG, "SEND_DATA too large (%u)", (unsigned)len); return false; }
    auto try_once = [&](const uint8_t *d, size_t n)->bool{
      std::vector<uint8_t> p; p.reserve(1+n);
      p.push_back(0x24);
      p.insert(p.end(), d, d+n);
      if(!send_packet_(p, 3000)) return false;
      bool ok = get_status_();
      // Small pacing between chunks to avoid overrun in ROM BSL
      delay_(1);
      return ok;
    };
    if (try_once(data, len)) return true;
    ESP_LOGW(TAG, "SEND_DATA failed, retrying once (len=%u)", (unsigned)len);
    return try_once(data, len);
  }

  bool bsl_mem_read32_(uint32_t addr, uint32_t &out){
    // CC26xx: 0x2A, length=9, checksum=sum(addr_be + 0x000001) + cmd? -> calc of (addr,size=2) in cc2538-bsl
    std::vector<uint8_t> p; p.reserve(1+4+2);
    p.push_back(0x2A);
    put_u32_be_(p, addr);
    p.push_back(1); // width (4 bytes)
    p.push_back(1); // number of reads
    if(!send_packet_(p)) return false;
    std::vector<uint8_t> payload;
    if(!recv_payload_(payload)) return false;
    if (payload.size() < 4) { ESP_LOGE(TAG, "MEM_READ returned %u bytes", (unsigned)payload.size()); return false; }
    // Payload (LSB..MSB)
    out = (uint32_t)payload[0] | ((uint32_t)payload[1]<<8) | ((uint32_t)payload[2]<<16) | ((uint32_t)payload[3]<<24);
    return get_status_();
  }

  bool bsl_get_chip_id_(uint16_t &chip_id){
    std::vector<uint8_t> p; p.reserve(1); p.push_back(0x28);
    if(!send_packet_(p)) return false;
    std::vector<uint8_t> payload; if(!recv_payload_(payload)) return false;
    if(payload.size() < 4) { ESP_LOGE(TAG, "GET_CHIP_ID: short payload %u", (unsigned)payload.size()); return false; }
    // Match CCTools behavior: chip_id from first two bytes [MSB..]
    chip_id = ((uint16_t)payload[0] << 8) | (uint16_t)payload[1];
    return get_status_();
  }

  bool get_status_raw_(uint8_t &st){
    std::vector<uint8_t> p(1, CMD_GET_STATUS);
    ESP_LOGD(TAG,"Sending GET_STATUS");
    if(!send_packet_(p)) return false;
    return recv_status_snoop_(st);
  }
  bool get_status_(){
    uint8_t st=0xFF;
    if(!get_status_raw_(st)) return false;
    ESP_LOGD(TAG,"GET_STATUS -> 0x%02X", st);
    return st == STAT_SUCCESS;
  }
  bool wait_status_success_(uint32_t total_ms, uint32_t poll_ms=200){
    uint32_t start = millis();
    while (millis() - start < total_ms){
      uint8_t st=0xFF;
      if(get_status_raw_(st)){
        if(st == STAT_SUCCESS) return true;
        if(st == STAT_FLASH_FAIL || st == STAT_INVALID_ADDR || st == STAT_INVALID_CMD){
          ESP_LOGE(TAG,"Status error: 0x%02X while waiting", st);
          return false;
        }
      }
      delay_(poll_ms);
    }
    ESP_LOGE(TAG,"Timed out waiting for success status");
    return false;
  }

  bool bsl_send_simple_(uint8_t cmd, uint32_t status_wait_ms=0){
    ESP_LOGV(TAG,"Sending CMD 0x%02X", cmd);
    std::vector<uint8_t> p(1, cmd);
    if(!send_packet_(p, cmd==CMD_BANK_ERASE ? 3000 : 1500)) { ESP_LOGE(TAG,"send_packet NACK for cmd 0x%02X", cmd); return false; }
    if (status_wait_ms) return wait_status_success_(status_wait_ms);
    if (recv_status_snoop_(last_status_, 1300, 18)) { ESP_LOGV(TAG,"Immediate status for 0x%02X -> 0x%02X", cmd, last_status_); return last_status_ == STAT_SUCCESS; }
    ESP_LOGV(TAG,"No immediate status for 0x%02X, querying GET_STATUS…", cmd);
    return get_status_();
  }

  // ---------- Erase helpers ----------
  bool sector_erase_one_(uint32_t page_addr){
    {
      std::vector<uint8_t> p; p.reserve(1+4);
      p.push_back(CMD_SECTOR_ERASE);
      // CC26xx SBL uses big-endian address encoding
      put_u32_be_(p, page_addr);
      if (send_packet_(p, 1500)) {
        uint8_t st=0xFF; bool ok = get_status_raw_(st); last_status_ = st;
        if (ok && st == STAT_SUCCESS) return true;
        if (ok && st == STAT_INVALID_ADDR) { ESP_LOGW(TAG,"SECTOR_ERASE invalid address @0x%08X (likely end of flash)", page_addr); return false; }
        ESP_LOGE(TAG,"SECTOR_ERASE status error @0x%08X st=0x%02X", page_addr, st);
        return false;
      }
      ESP_LOGW(TAG,"SECTOR_ERASE no ACK (fmt A), retrying fmt B…");
      feed_();
    }
    {
      std::vector<uint8_t> p; p.reserve(1+4+2);
      p.push_back(CMD_SECTOR_ERASE);
      put_u32_be_(p, page_addr);
      put_u16_le_(p, 1);
      if (!send_packet_(p, 1500)) { ESP_LOGE(TAG,"SECTOR_ERASE no ACK (fmt B) @0x%08X", page_addr); return false; }
      uint8_t st=0xFF; bool ok = get_status_raw_(st); last_status_ = st;
      if (ok && st == STAT_SUCCESS) return true;
      if (ok && st == STAT_INVALID_ADDR) { ESP_LOGW(TAG,"SECTOR_ERASE invalid address @0x%08X (likely end of flash)", page_addr); return false; }
      ESP_LOGE(TAG,"SECTOR_ERASE status error (fmt B) @0x%08X st=0x%02X", page_addr, st);
      return false;
    }
  }

  bool erase_entire_flash_by_sector_(){
    ESP_LOGI(TAG, "Sweeping sectors from base (4KB pages) until invalid address…");
    uint32_t page = 0; uint32_t erased = 0;
    while(true){
      uint32_t addr = page * FLASH_PAGE_SIZE;
      if(sector_erase_one_(addr)){
        ++erased; ++page; if((erased % 8) == 0) ESP_LOGV(TAG, "Erased %u pages so far", (unsigned)erased);
        feed_();
      } else {
        if (last_status_ == STAT_INVALID_ADDR){
          ESP_LOGI(TAG, "Reached end of flash after %u pages", (unsigned)erased);
          return true;
        }
        ESP_LOGE(TAG, "Sector sweep aborted (status=0x%02X)", last_status_);
        return false;
      }
    }
  }

  bool bank_erase_(){
    ESP_LOGI(TAG,"Issuing BANK_ERASE (full flash)… this can take a while");
    if(!bsl_send_simple_(CMD_BANK_ERASE, /*status_wait_ms=*/60000)){
      ESP_LOGE(TAG,"BANK_ERASE failed (no ACK or status error).");
      return false;
    }
    ESP_LOGI(TAG,"BANK_ERASE complete.");
    erased_pages_.clear();
    bank_erased_all_ = true;
    return true;
  }

  bool verify_erased_quick_(){
    ESP_LOGI(TAG, "Verifying erase (quick sample)…");
    const uint32_t addrs[] = {0x00000000u, 0x00001000u, 0x00002000u, 0x00004000u, 0x00008000u, 0x00010000u, 0x00020000u, 0x00040000u};
    for (uint32_t a : addrs){
      uint32_t v=0;
      if(!bsl_mem_read32_(a, v)) { ESP_LOGW(TAG, "Readback failed @0x%08X (skipping)", a); continue; }
      if (v != 0xFFFFFFFFu){ ESP_LOGE(TAG, "Verify failed: @0x%08X = 0x%08X (not erased)", a, v); return false; }
      feed_();
    }
    ESP_LOGI(TAG, "Erase verified on sample addresses");
    return true;
  }

  bool ensure_erased_(uint32_t addr, size_t len){
    if (bank_erased_all_) return true;
    uint32_t first = addr / FLASH_PAGE_SIZE;
    uint32_t last  = (addr + len - 1) / FLASH_PAGE_SIZE;
    for(uint32_t p = first; p <= last; ++p){
      if (erased_pages_.find(p) != erased_pages_.end()) continue;
      uint32_t page_addr = p * FLASH_PAGE_SIZE;
      ESP_LOGI(TAG,"Erasing page @ 0x%08X", page_addr);
      if(!sector_erase_one_(page_addr)) return false;
      erased_pages_.insert(p);
      feed_();
    }
    return true;
  }

  // ---------- memory write (with fallback) ----------
  bool memory_write_(uint32_t addr, const uint8_t *data, size_t len){
    if(!ensure_erased_(addr, len)) return false;

    {
      std::vector<uint8_t> p; p.reserve(1+4+len);
      p.push_back(CMD_MEMORY_WRITE);
      put_u32_le_(p, addr);
      p.insert(p.end(), data, data+len);
      if(send_packet_(p)) {
        if(get_status_()) return true;
        ESP_LOGE(TAG,"MEMORY_WRITE status error @0x%08X len=%u", addr, (unsigned)len);
        return false;
      }
      ESP_LOGW(TAG,"MEMORY_WRITE no ACK for format A, retrying with length field…");
      feed_();
    }
    {
      std::vector<uint8_t> p; p.reserve(1+4+2+len);
      p.push_back(CMD_MEMORY_WRITE);
      put_u32_le_(p, addr);
      put_u16_le_(p, (uint16_t)len);
      p.insert(p.end(), data, data+len);
      if(!send_packet_(p)) { ESP_LOGE(TAG,"MEMORY_WRITE no ACK for format B"); return false; }
      return get_status_();
    }
  }

  // ---------- programming (BIN / HEX) ----------
  bool program_from_bin_(const std::string &url){
    esp_http_client_handle_t client;
    if(!http_open_(url, client)) return false;

    std::vector<uint8_t> net_buf(1024);
    std::vector<uint8_t> fifo; fifo.reserve(8192);
    uint32_t addr = 0;
    esphome::md5::MD5Digest md5; md5.init();
    uint32_t total = 0;
    uint32_t last_pc = 0;

    // Try whole-bank erase up-front if config says so
    if (erase_mode_ == 1 /*bank*/ || erase_mode_ == 2 /*auto*/){
      if(!bank_erase_()){
        if (erase_mode_ == 1) { esp_http_client_cleanup(client); return false; }
        ESP_LOGW(TAG,"BANK_ERASE failed in 'auto' mode; falling back to sector erases as needed.");
      }
    }
    bool do_page_erases = !bank_erased_all_ && (erase_mode_ == 0 || erase_mode_ == 2);

    while(true){
      int r = esp_http_client_read(client, (char*)net_buf.data(), net_buf.size());
      if(r < 0){ esp_http_client_cleanup(client); return false; }
      if(r == 0) break;
      fifo.insert(fifo.end(), net_buf.begin(), net_buf.begin()+r);
      while (fifo.size() >= 1024) {
        size_t blk = std::min((size_t)2048, fifo.size());
        blk &= ~3u; // 4-byte align
        if (blk == 0) break;
        if(do_page_erases && !ensure_erased_(addr, blk)) { esp_http_client_cleanup(client); return false; }
        if (verbose_) ESP_LOGV(TAG, "DOWNLOAD @0x%08X size=%u", addr, (unsigned)blk);
        if(!bsl_download_(addr, (uint32_t)blk)) { esp_http_client_cleanup(client); return false; }
        size_t off=0; while(off < blk){ size_t n = std::min((size_t)192, blk - off); if(!bsl_send_data_(&fifo[off], n)) { esp_http_client_cleanup(client); return false; } off += n; feed_(); }
        md5.add(fifo.data(), blk);
        total += blk;
        update_progress_(total, expected_size_, last_pc);
        fifo.erase(fifo.begin(), fifo.begin()+blk);
        addr += blk;
        feed_();
      }
      feed_();
    }

    // Flush tail (pad to 4 for SBL, but do not include pad in MD5)
    if (!fifo.empty()){
      size_t blk = fifo.size();
      size_t pad = (4 - (blk & 3u)) & 3u;
      size_t blk_pad = blk + pad;
      if(do_page_erases && !ensure_erased_(addr, blk_pad)) { esp_http_client_cleanup(client); return false; }
      if (verbose_) ESP_LOGV(TAG, "DOWNLOAD @0x%08X size=%u (tail)", addr, (unsigned)blk_pad);
      if(!bsl_download_(addr, (uint32_t)blk_pad)) { esp_http_client_cleanup(client); return false; }
      size_t off=0; while(off < blk){ size_t n = std::min((size_t)192, blk - off); if(!bsl_send_data_(&fifo[off], n)) { esp_http_client_cleanup(client); return false; } off += n; feed_(); }
      if (pad){ uint8_t ff[4] = {0xFF,0xFF,0xFF,0xFF}; size_t offp=0; while(offp < pad){ size_t n = std::min((size_t)pad-offp, (size_t)192); if(!bsl_send_data_(ff, n)) { esp_http_client_cleanup(client); return false; } offp += n; feed_(); } }
      md5.add(fifo.data(), blk);
      total += blk;
      update_progress_(total, expected_size_, last_pc);
      addr += blk_pad;
      fifo.clear();
    }

    esp_http_client_cleanup(client);
    md5.calculate(); char md5hex[33]; md5.get_hex(md5hex); md5hex[32] = 0;
    ESP_LOGI(TAG,"BIN transfer complete. bytes=%u md5=%s", (unsigned)total, md5hex);
    if (!expected_md5_.empty()){
      if (expected_md5_.size() == 32) {
        if (strcasecmp(expected_md5_.c_str(), md5hex) != 0) {
          ESP_LOGE(TAG, "MD5 mismatch: expected %s", expected_md5_.c_str());
          return false;
        } else {
          ESP_LOGI(TAG, "MD5 verified.");
        }
      }
    }
    return true;
  }

  bool program_from_hex_(const std::string &url){
    esp_http_client_handle_t client;
    if(!http_open_(url, client)) return false;

    std::string line; line.reserve(256);
    uint8_t rx[512]; int r; uint32_t upper = 0;
    esphome::md5::MD5Digest md5; md5.init();
    esphome::md5::MD5Digest md5_hex; md5_hex.init();
    std::vector<uint8_t> seg; seg.reserve(4096); uint32_t seg_addr = 0; bool seg_open=false; uint32_t total=0; uint32_t last_pc=0;

    auto byte_at = [&](const std::string &L, size_t idx)->uint8_t{
      return (hex2_(L[1+2*idx])<<4) | hex2_(L[1+2*idx+1]);
    };

    // Try bank erase first if requested
    bool bank_done = false;
    if (erase_mode_ == 1 /*bank*/ || erase_mode_ == 2 /*auto*/){
      bank_done = bank_erase_();
      if (!bank_done && erase_mode_ == 1) { esp_http_client_cleanup(client); return false; }
      if (!bank_done) ESP_LOGW(TAG,"BANK_ERASE failed in 'auto' mode; falling back to per-page erase.");
    }
    bool do_page_erases = !bank_done && (erase_mode_ == 0 || erase_mode_ == 2);

    while((r = esp_http_client_read(client, (char*)rx, sizeof(rx))) > 0){
      md5_hex.add(rx, r);
      for(int i=0;i<r;i++){
        char ch = (char)rx[i];
        if(ch == '\r') { feed_(); continue; }
        if(ch == '\n'){
          if(line.empty()){ feed_(); continue; }
          if(line[0] != ':'){ line.clear(); feed_(); continue; }

          uint8_t  reclen = byte_at(line, 0);
          uint16_t off    = (byte_at(line,1) << 8) | byte_at(line,2);
          uint8_t  rectype= byte_at(line,3);

          uint8_t sum = reclen + (uint8_t)(off>>8) + (uint8_t)(off&0xFF) + rectype;
          for(uint8_t j=0;j<reclen;j++) sum += byte_at(line, 4+j);
          sum += byte_at(line, 4+reclen);
          if(sum != 0){ ESP_LOGE(TAG,"HEX checksum error"); esp_http_client_cleanup(client); return false; }

          if(rectype == 0x00){
            uint32_t addr = upper + off;
            // If discontinuity, flush previous segment
            if (!seg_open || addr != seg_addr + seg.size()){
              if (seg_open && !seg.empty()){
                size_t blk = seg.size(); size_t pad = (4 - (blk & 3u)) & 3u; size_t blk_pad = blk + pad;
                if(do_page_erases && !ensure_erased_(seg_addr, blk_pad)) { esp_http_client_cleanup(client); return false; }
                if (verbose_) ESP_LOGV(TAG, "DOWNLOAD @0x%08X size=%u (flush)", seg_addr, (unsigned)blk_pad);
                if(!bsl_download_(seg_addr, (uint32_t)blk_pad)) { esp_http_client_cleanup(client); return false; }
                size_t offb=0; while(offb < blk){ size_t n = std::min((size_t)192, blk-offb); if(!bsl_send_data_(&seg[offb], n)) { esp_http_client_cleanup(client); return false; } offb += n; feed_(); }
                if (pad){ uint8_t ff[4] = {0xFF,0xFF,0xFF,0xFF}; size_t offp=0; while(offp < pad){ size_t n = std::min((size_t)pad-offp, (size_t)192); if(!bsl_send_data_(ff, n)) { esp_http_client_cleanup(client); return false; } offp += n; feed_(); } }
                md5.add(seg.data(), blk); total += blk; update_progress_(total, expected_size_, last_pc); seg.clear();
              }
              seg_addr = addr; seg_open = true; seg.clear();
            }
            // Append this record's data
            for(uint8_t j=0;j<reclen;j++) seg.push_back(byte_at(line, 4+j));
            // If segment grows large, flush in 4KB blocks
            while(seg.size() >= 4096){
              size_t blk = 4096; if(do_page_erases && !ensure_erased_(seg_addr, blk)) { esp_http_client_cleanup(client); return false; }
              if (verbose_) ESP_LOGV(TAG, "DOWNLOAD @0x%08X size=%u (seg)", seg_addr, (unsigned)blk);
              if(!bsl_download_(seg_addr, (uint32_t)blk)) { esp_http_client_cleanup(client); return false; }
              size_t offb=0; while(offb < blk){ size_t n = std::min((size_t)192, blk-offb); if(!bsl_send_data_(&seg[offb], n)) { esp_http_client_cleanup(client); return false; } offb += n; feed_(); }
              md5.add(seg.data(), blk); total += blk; update_progress_(total, expected_size_, last_pc); seg.erase(seg.begin(), seg.begin()+blk); seg_addr += blk; feed_();
            }
          } else if(rectype == 0x02){
            // Extended Segment Address: upper = segment << 4
            uint16_t seg = ((uint16_t)byte_at(line,4) << 8) | byte_at(line,5);
            upper = ((uint32_t)seg) << 4;
            ESP_LOGV(TAG,"HEX ESA -> 0x%08X", upper);
          } else if(rectype == 0x04){
            uint16_t hi = ((uint16_t)byte_at(line,4) << 8) | byte_at(line,5);
            upper = ((uint32_t)hi) << 16;   // ELA upper 16 bits
            ESP_LOGV(TAG,"HEX ELA -> 0x%08X", upper);
          } else if(rectype == 0x01){
            ESP_LOGI(TAG,"HEX EOF.");
          } else {
            ESP_LOGV(TAG,"HEX record 0x%02X ignored", rectype);
          }

          line.clear();
          feed_();
        } else {
          line.push_back(ch);
          feed_();
        }
      }
      feed_();
    }

    // Flush remaining segment
    if(seg_open && !seg.empty()){
      size_t blk = seg.size(); size_t pad = (4 - (blk & 3u)) & 3u; size_t blk_pad = blk + pad;
      if(do_page_erases && !ensure_erased_(seg_addr, blk_pad)) { esp_http_client_cleanup(client); return false; }
      if (verbose_) ESP_LOGV(TAG, "DOWNLOAD @0x%08X size=%u (final)", seg_addr, (unsigned)blk_pad);
      if(!bsl_download_(seg_addr, (uint32_t)blk_pad)) { esp_http_client_cleanup(client); return false; }
      size_t offb=0; while(offb < blk){ size_t n = std::min((size_t)192, blk-offb); if(!bsl_send_data_(&seg[offb], n)) { esp_http_client_cleanup(client); return false; } offb += n; feed_(); }
      if (pad){ uint8_t ff[4] = {0xFF,0xFF,0xFF,0xFF}; size_t offp=0; while(offp < pad){ size_t n = std::min((size_t)pad-offp, (size_t)192); if(!bsl_send_data_(ff, n)) { esp_http_client_cleanup(client); return false; } offp += n; feed_(); } }
      md5.add(seg.data(), blk); total += blk; update_progress_(total, expected_size_, last_pc); seg.clear();
    }

    esp_http_client_cleanup(client);
    md5.calculate(); char md5hex[33]; md5.get_hex(md5hex); md5hex[32]=0;
    md5_hex.calculate(); char md5hex_raw[33]; md5_hex.get_hex(md5hex_raw); md5hex_raw[32]=0;
    ESP_LOGI(TAG,"HEX program complete. bytes=%u md5=%s (raw_hex_md5=%s)", (unsigned)total, md5hex, md5hex_raw);
    if (!expected_md5_.empty() && expected_md5_.size() == 32) {
      if (strcasecmp(expected_md5_.c_str(), md5hex_raw) != 0) {
        ESP_LOGW(TAG, "Manifest MD5 (HEX file) mismatch (expected %s)", expected_md5_.c_str());
      } else {
        ESP_LOGI(TAG, "Manifest MD5 (HEX file) verified.");
      }
    }
    return true;
  }

  // ---------- ZNP helpers and progress ----------
  static inline uint8_t znp_fcs_(uint8_t len, uint8_t cmd1, uint8_t cmd2, const std::vector<uint8_t> &data){ uint8_t f=len^cmd1^cmd2; for(auto b:data) f^=b; return f; }
  bool znp_send_(uint8_t cmd1, uint8_t cmd2, const std::vector<uint8_t> &data){ uint8_t len=(uint8_t)data.size(); uint8_t f=znp_fcs_(len,cmd1,cmd2,data); uint8_t hdr[4]={0xFE,len,cmd1,cmd2}; uart_->write_array(hdr,4); if(len) uart_->write_array(data.data(),len); uart_->write_byte(f); feed_(); return true; }
  bool znp_recv_(uint8_t &cmd1, uint8_t &cmd2, std::vector<uint8_t> &data, uint32_t timeout_ms=1000){ uint8_t b=0; uint32_t st=millis(); while(millis()-st<timeout_ms){ if(uart_->available()&&uart_->read_byte(&b)&&b==0xFE) break; delay_(1);} if(b!=0xFE) return false; uint8_t len=0; if(!read_exact_(&len,1,timeout_ms)) return false; if(!read_exact_(&cmd1,1,timeout_ms)) return false; if(!read_exact_(&cmd2,1,timeout_ms)) return false; data.assign(len,0); for(uint8_t i=0;i<len;i++){ uint8_t bb; if(!read_exact_(&bb,1,timeout_ms)) return false; data[i]=bb;} uint8_t f=0; if(!read_exact_(&f,1,timeout_ms)) return false; return f==znp_fcs_(len,cmd1,cmd2,data); }
  bool znp_req_resp_(uint8_t sreq_cmd1, uint8_t sreq_cmd2, const std::vector<uint8_t> &payload,
                     uint8_t expect_cmd1, uint8_t expect_cmd2,
                     std::vector<uint8_t> &out, uint8_t tries=6, uint32_t wait_ms=120){
    for(uint8_t a=0; a<tries; a++){
      flush_uart_();
      znp_send_(sreq_cmd1, sreq_cmd2, payload);
      uint8_t r1=0, r2=0; std::vector<uint8_t> pl;
      if(znp_recv_(r1,r2,pl,400)){
        if(r1==expect_cmd1 && r2==expect_cmd2){ out = std::move(pl); return true; }
        if(verbose_) ESP_LOGV(TAG, "ZNP unexpected SRSP 0x%02X 0x%02X len=%u", r1, r2, (unsigned)pl.size());
      }
      delay_(wait_ms);
    }
    return false;
  }
  void update_progress_(uint32_t total, uint32_t expected, uint32_t &last_pc){ if(!show_progress_) return; if(expected>0){ uint32_t pc=(uint64_t)total*100/expected; if(pc>=last_pc+progress_step_){ last_pc=pc-(pc%progress_step_); ESP_LOGI(TAG,"Progress: %u%% (%u/%u)",(unsigned)pc,(unsigned)total,(unsigned)expected);} } else { if(total/65536>last_pc){ last_pc=total/65536; ESP_LOGI(TAG,"Progress: %u bytes",(unsigned)total);} } }

  // Probe only the ZNP product field (from SYS_VERSION) and publish variant text if configured
  void probe_znp_product_(){
    std::vector<uint8_t> pl;
    if (znp_req_resp_(0x21, 0x02, {}, 0x61, 0x02, pl, 6, 120) && pl.size()>=2){
      znp_product_ = pl[1];
      ESP_LOGD(TAG, "Detected ZNP product=%u for variant selection", (unsigned)znp_product_);
      if (variant_text_) {
        const char* vs = nullptr;
        if (variant_detected_==7) vs="CC2652P7"; else if (variant_detected_==2) vs="CC2652P2";
        else if (znp_product_==7) vs="CC2652P7"; else if (znp_product_==2) vs="CC2652P2";
        if (vs) variant_text_->publish_state(vs); else ESP_LOGD(TAG, "Variant still unknown; keeping previous value");
      }
      if (znp_product_ != 2 && znp_product_ != 7) {
        ESP_LOGW(TAG, "SYS_VERSION product=%u does not distinguish P2/P7; variant remains unknown until flashing or manual override.", (unsigned)znp_product_);
      }
    }
  }

  // Variant chooser: 0=unknown, 2=P2, 7=P7
  uint8_t choose_variant_() const {
    if (variant_force_ == 2 || variant_force_ == 7) return variant_force_;
    if (variant_detected_ == 2 || variant_detected_ == 7) return variant_detected_;
    if (znp_product_ == 2 || znp_product_ == 7) return znp_product_;
    return 0;
  }
  void query_znp_info_(){
    if(!uart_) return;
    ESP_LOGD(TAG, "Querying ZNP info (version + IEEE)…");
    flush_uart_();
    std::vector<uint8_t> pl;
    // SYS_VERSION (SREQ 0x21,0x02 → SRSP 0x61,0x02)
    if (znp_req_resp_(0x21, 0x02, {}, 0x61, 0x02, pl, 8, 120) && pl.size()>=9){
      // SYS_VERSION payload: [0]=transport,[1]=product,[2]=major,[3]=minor,[4]=maint,[5..8]=CODE_REVISION_NUMBER (LSB first)
      znp_product_ = pl[1];
      if (variant_text_) {
        const char* vs = nullptr;
        if (variant_detected_==7) vs="CC2652P7"; else if (variant_detected_==2) vs="CC2652P2";
        else if (znp_product_==7) vs="CC2652P7"; else if (znp_product_==2) vs="CC2652P2";
        if (vs) variant_text_->publish_state(vs); else ESP_LOGD(TAG, "Variant still unknown; keeping previous value");
      }
      if (znp_product_ != 2 && znp_product_ != 7) {
        ESP_LOGW(TAG, "Variant detection via SYS_VERSION: unknown (product=%u). Set 'variant' in YAML if needed.", (unsigned)znp_product_);
      }
      uint32_t code_rev = (uint32_t)pl[5] | ((uint32_t)pl[6] << 8) | ((uint32_t)pl[7] << 16) | ((uint32_t)pl[8] << 24);
      char ver_str[40];
      snprintf(ver_str, sizeof(ver_str), "%u", (unsigned)code_rev);
      ESP_LOGI(TAG, "ZNP FW Version (CODE_REVISION_NUMBER): %s (stack %u.%u.%u, prod %u, trans %u)",
               ver_str, (unsigned)pl[2], (unsigned)pl[3], (unsigned)pl[4], (unsigned)pl[1], (unsigned)pl[0]);
      if(fw_text_) fw_text_->publish_state(ver_str);
    } else {
      ESP_LOGD(TAG, "ZNP version query did not return expected frame");
    }

    // First try: UTIL_GET_DEVICE_INFO (SREQ 0x27,0x00 → SRSP 0x67,0x00)
    bool ieee_ok = false;
    pl.clear();
    if (znp_req_resp_(0x27, 0x00, {}, 0x67, 0x00, pl, 6, 120) && pl.size()>=10){
      // Payload: short_addr (2), ieee (8), devType (1), devState (1) … (varies)
      if (pl.size() >= 12){
        char ieee[24];
        snprintf(ieee,sizeof(ieee),"%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 pl[9],pl[8],pl[7],pl[6],pl[5],pl[4],pl[3],pl[2]);
        ESP_LOGI(TAG, "ZNP IEEE (device_info): %s", ieee);
        if(ieee_text_) ieee_text_->publish_state(ieee);
        ieee_ok = true;
      }
    }
    // Fallback: UTIL_GET_EXT_ADDR (SREQ 0x27,0x02 → SRSP 0x67,0x02)
    if (!ieee_ok){
      pl.clear();
      if (znp_req_resp_(0x27, 0x02, {}, 0x67, 0x02, pl, 6, 120) && pl.size()>=8){
        char ieee[24];
        snprintf(ieee,sizeof(ieee),"%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",pl[7],pl[6],pl[5],pl[4],pl[3],pl[2],pl[1],pl[0]);
        ESP_LOGI(TAG, "ZNP IEEE (extaddr): %s", ieee);
        if(ieee_text_) ieee_text_->publish_state(ieee);
        ieee_ok = true;
      } else {
        ESP_LOGD(TAG, "ZNP IEEE query did not return expected frame");
      }
    }
  }

  // ---------- boot/restore ----------
  bool enter_bootloader_(){
    ESP_LOGI(TAG,"Asserting BSL/Reset to enter bootloader…");
    bsl_sw_->turn_on();  delay_(20);
    rst_sw_->turn_on();  delay_(20);
    rst_sw_->turn_off(); delay_(30);
    bsl_sw_->turn_off(); delay_(40);
    flush_uart_();
    return true;
  }
  void leave_bootloader_(){ rst_sw_->turn_on(); delay_(20); rst_sw_->turn_off(); delay_(20); }
  void restore_baud_(){ if (restore_baud_rate_>0){ ESP_LOGD(TAG,"Restoring UART baud: %u", restore_baud_rate_); uart_->set_baud_rate(restore_baud_rate_); uart_->load_settings(false);} }

  bool bsl_autobaud_(uint32_t timeout_ms=800){
    for (int attempt=0; attempt<3; ++attempt){
      flush_uart_();
      const uint8_t bb[2] = {0x55,0x55};
      ESP_LOGV(TAG,"Sending auto-baud sync 55 55 (attempt %d)", attempt+1);
      uart_->write_array(bb,2); feed_();

      uint8_t b = 0xFF;
      uint32_t start = millis();
      while (millis() - start < timeout_ms + attempt*400) {
        if (uart_->available() && uart_->read_byte(&b)) {
          ESP_LOGV(TAG, "Auto-baud byte=0x%02X", b);
          if (b == SBL_ACK_00 || b == SBL_ACK_CC) {
            ESP_LOGI(TAG, "Auto-baud ACK=0x%02X", b);
            return true;
          }
        } else {
          delay_(2); feed_();
        }
      }
      if (attempt < 2) { ESP_LOGV(TAG, "Auto-baud retry: pulsing reset"); rst_sw_->turn_on(); delay_(15); rst_sw_->turn_off(); delay_(25); }
    }
    return false;
  }

  bool detect_variant_via_bsl_(){
    bool ok_all = false;
    uint32_t dev=0, usr=0, flsz=0; uint16_t cid=0;
    if (boot_baud_ > 0) { uart_->set_baud_rate(boot_baud_); uart_->load_settings(false); }
    if(!enter_bootloader_()) { ESP_LOGE(TAG, "BSL probe: failed to enter bootloader"); restore_baud_(); return false; }
    if(!bsl_autobaud_(1200)) { ESP_LOGE(TAG, "BSL probe: autobaud failed"); leave_bootloader_(); restore_baud_(); return false; }
    delay_(15); flush_uart_();
    (void)bsl_get_chip_id_(cid);
    if(!bsl_mem_read32_(0x50001318u, dev)) { ESP_LOGE(TAG, "BSL probe: read ICEPICK_DEVICE_ID failed"); goto out; }
    if(!bsl_mem_read32_(0x50001294u, usr)) { ESP_LOGE(TAG, "BSL probe: read FCFG_USER_ID failed"); goto out; }
    if(!bsl_mem_read32_(0x4003002Cu, flsz)) { ESP_LOGW(TAG, "BSL probe: read FLASH_SIZE failed"); }
    {
      uint8_t d0 = (uint8_t)(dev & 0xFFu);
      uint8_t d1 = (uint8_t)((dev >> 8) & 0xFFu);
      uint8_t d2 = (uint8_t)((dev >> 16) & 0xFFu);
      uint8_t d3 = (uint8_t)((dev >> 24) & 0xFFu);
      uint32_t wafer_id = ((((uint32_t)(d3 & 0x0Fu)) << 16) | ((uint32_t)d2 << 8) | ((uint32_t)(d1 & 0xF0u))) >> 4;
      uint8_t  pg_rev   = (uint8_t)((d3 >> 4) & 0x0Fu);
      const char *ded = "unknown";
      uint8_t vdet = 0;
      if (wafer_id == 0xBB77u && pg_rev == 0x1) { vdet = 7; ded = "CC2652P7"; }
      else if (wafer_id == 0xBB41u && pg_rev == 0x3) { vdet = 2; ded = "CC2652P2"; }
      variant_detected_ = vdet;
      ESP_LOGI(TAG, "BSL detect: chip_id=0x%04X wafer=0x%04X pg_rev=%u => %s", (unsigned)cid, (unsigned)wafer_id, (unsigned)pg_rev, ded);
      if (variant_text_) variant_text_->publish_state(ded);
      ok_all = (vdet == 2 || vdet == 7);
    }
  out:
    leave_bootloader_(); restore_baud_();
    return ok_all;
  }

  // ---------- top-level ----------
  void run_erase_(uint8_t mode){
    ESP_LOGI(TAG,"Starting erase-only flow (mode=%u)…", (unsigned)mode);
    if (!uart_ || !bsl_sw_ || !rst_sw_){ ESP_LOGE(TAG,"Not configured (uart/switches)."); return; }

    if (boot_baud_ > 0) { ESP_LOGD(TAG,"Setting UART boot baud: %u", boot_baud_); uart_->set_baud_rate(boot_baud_); uart_->load_settings(false); }
    if(!enter_bootloader_()){ ESP_LOGE(TAG,"Failed to enter bootloader."); restore_baud_(); return; }
    if(!bsl_autobaud_(1200)){ ESP_LOGE(TAG,"Auto-baud failed."); leave_bootloader_(); restore_baud_(); return; }
    delay_(20); flush_uart_();
    if(!bsl_send_simple_(CMD_PING)){
      ESP_LOGE(TAG,"PING failed."); leave_bootloader_(); restore_baud_(); return;
    }

    bool ok=false;
    bank_erased_all_ = false;
    if (mode == 1 /*bank*/ || mode == 2 /*auto*/){
      ok = bank_erase_();
      if (!ok && mode == 2){
        ESP_LOGW(TAG, "BANK_ERASE failed; falling back to sector sweep.");
        ok = erase_entire_flash_by_sector_();
      }
    } else {
      ok = erase_entire_flash_by_sector_();
    }

    if (ok) {
      // Quick verify
      (void)verify_erased_quick_();
      ESP_LOGI(TAG, "Erase-only flow completed successfully");
    }
    else     ESP_LOGE(TAG, "Erase-only flow failed");
    leave_bootloader_(); restore_baud_();
  }

  void run_update_(){
    ESP_LOGI(TAG,"Starting firmware update…");
    if (manifest_url_.empty() || !uart_ || !bsl_sw_ || !rst_sw_){
      ESP_LOGE(TAG,"Not configured (url/uart/switches)."); return;
    }

    // Try to detect variant via ZNP before entering bootloader
    probe_znp_product_();
    if (choose_variant_() == 0) {
      ESP_LOGI(TAG, "Variant unknown via ZNP; probing via bootloader registers…");
      (void)detect_variant_via_bsl_();
    }

    std::string fw_url;
    if(!fetch_manifest_(manifest_url_, fw_url)){
      ESP_LOGE(TAG,"Manifest fetch/parse failed.");
      ESP_LOGE(TAG,"If your manifest has variants and variant detection shows 'unknown', set 'variant: p2|p7' in YAML.");
      return;
    }
    if (fw_url.empty()) {
      ESP_LOGE(TAG, "Manifest parsed but no firmware URL selected. Set 'variant: p2|p7' in YAML.");
      return;
    }
    ESP_LOGI(TAG,"Manifest OK. Firmware: %s", fw_url.c_str());

    erased_pages_.clear();
    bank_erased_all_ = false;

    if (boot_baud_ > 0) { ESP_LOGD(TAG,"Setting UART boot baud: %u", boot_baud_); uart_->set_baud_rate(boot_baud_); uart_->load_settings(false); }

    if(!enter_bootloader_()){ ESP_LOGE(TAG,"Failed to enter bootloader."); restore_baud_(); return; }
    if(!bsl_autobaud_()){   ESP_LOGE(TAG,"Auto-baud failed.");   leave_bootloader_(); restore_baud_(); return; }

    delay_(20);
    flush_uart_();

    if(!bsl_send_simple_(CMD_PING)){
      ESP_LOGE(TAG,"PING failed.");
      leave_bootloader_(); restore_baud_(); return;
    }
    ESP_LOGI(TAG,"PING ok.");

    bool ok = false;
    if (ends_with_(fw_url, ".hex") || ends_with_(fw_url, ".HEX")){
      ESP_LOGI(TAG,"Programming from Intel HEX…");
      ok = program_from_hex_(fw_url);
    } else {
      ESP_LOGI(TAG,"Programming from BIN (base 0x00000000)…");
      ok = program_from_bin_(fw_url);
    }

    if(ok){
      ESP_LOGI(TAG,"Transfer verified. Resetting target…");
      bsl_send_simple_(CMD_RESET, 1000);
      // Re-publish known variant so logs/UI don't show "unknown" from ZNP probe
      if (variant_text_) {
        const char* vs = nullptr; uint8_t v = choose_variant_();
        if (v==7) vs="CC2652P7"; else if (v==2) vs="CC2652P2";
        if (vs) variant_text_->publish_state(vs);
      }
    } else {
      ESP_LOGE(TAG,"Update failed.");
    }

    leave_bootloader_();
    if (ok && manifest_baudrate_ != 0) restore_baud_rate_ = manifest_baudrate_;
    restore_baud_();
    if (ok && manifest_baudrate_ != 0 && !application_baud_.commit(manifest_baudrate_)) {
      ESP_LOGE(TAG, "Radio transfer succeeded but saving the application baud failed");
      ok = false;
    }

    // Report completion only after the application UART and saved baud are ready.
    if (ok) ESP_LOGI(TAG,"Update finished OK. Application UART baud ready.");
    // After restoring normal UART, query ZNP info to refresh sensors without reboot
    if (ok) {
      this->set_timeout(1200, [this](){ this->query_znp_info_(); });
      this->set_timeout(4000, [this](){ this->query_znp_info_(); });
    }
  }

  void run_check_update_(){
    ESP_LOGI(TAG, "Checking CC2652 firmware against manifest…");
    // Detect variant for manifest selection
    probe_znp_product_();
    if (choose_variant_() == 0) {
      ESP_LOGD(TAG, "Variant still unknown (not entering bootloader during check).");
    }
    // Query current code revision via SYS_VERSION
    uint32_t code_rev = 0;
    {
      std::vector<uint8_t> pl;
      if (znp_req_resp_(0x21, 0x02, {}, 0x61, 0x02, pl, 6, 120) && pl.size()>=9){
        code_rev = (uint32_t)pl[5] | ((uint32_t)pl[6] << 8) | ((uint32_t)pl[7] << 16) | ((uint32_t)pl[8] << 24);
        ESP_LOGI(TAG, "Current CODE_REVISION_NUMBER=%u (stack %u.%u.%u)", (unsigned)code_rev, (unsigned)pl[2], (unsigned)pl[3], (unsigned)pl[4]);
        if (fw_text_) { char s[16]; snprintf(s,sizeof(s),"%u", (unsigned)code_rev); fw_text_->publish_state(s); }
      } else {
        ESP_LOGW(TAG, "Could not read current code revision from ZNP");
      }
    }
    std::string fw_url;
    bool man_ok = fetch_manifest_(manifest_url_, fw_url);
    if (!manifest_version_.empty() && latest_text_) latest_text_->publish_state(manifest_version_.c_str());
    if (!man_ok && fw_url.empty()) {
      ESP_LOGW(TAG, "Variant unknown; cannot select firmware URL from manifest (checking only). Set 'variant: p2|p7' if needed.");
    }
    // Compare numeric if possible
    uint32_t man_rev = 0;
    for(char c: manifest_version_) if (c>='0' && c<='9'){ man_rev = man_rev*10 + (uint32_t)(c-'0'); }
    if (man_rev && code_rev){
      if (man_rev > code_rev) ESP_LOGI(TAG, "Update available: manifest %u > current %u", (unsigned)man_rev, (unsigned)code_rev);
      else if (man_rev == code_rev) ESP_LOGI(TAG, "Firmware up-to-date (%u)", (unsigned)code_rev);
      else ESP_LOGI(TAG, "Device newer than manifest? current %u > manifest %u", (unsigned)code_rev, (unsigned)man_rev);
    }
  }

  void run_detect_variant_(){
    ESP_LOGI(TAG, "Detecting CC2652 variant via ROM bootloader…");
    // Make sure we have basic config
    if (!uart_ || !bsl_sw_ || !rst_sw_) { ESP_LOGE(TAG, "Not configured (uart/switches)."); return; }
    bool ok = detect_variant_via_bsl_();
    if (ok) {
      uint8_t v = choose_variant_();
      if (v == 7) ESP_LOGI(TAG, "Variant detection: CC2652P7");
      else if (v == 2) ESP_LOGI(TAG, "Variant detection: CC2652P2");
      else ESP_LOGW(TAG, "Variant detection inconclusive.");
    } else {
      ESP_LOGE(TAG, "Variant detection failed.");
    }
  }

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
  uint8_t erase_mode_{0}; // 0=sector,1=bank,2=auto
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
  uint8_t variant_force_{0};   // 0=auto, 2=P2, 7=P7
  uint8_t variant_detected_{0};
  uint8_t znp_product_{0xFF};  // captured from SYS_VERSION [product]
  bool detect_on_boot_{true};
  uint16_t detect_on_boot_delay_ms_{0};
  zigbee_firmware_baud::RuntimeBaud application_baud_;
  uint32_t manifest_baudrate_{0};
};

} // namespace cc2652_flasher
} // namespace esphome
// Provide an out-of-class destructor to act as the key function so the
// toolchain emits the vtable for CC2652Flasher in this translation unit.
namespace esphome { namespace cc2652_flasher {
CC2652Flasher::~CC2652Flasher() {}
} }
// ===== end implementation =====
