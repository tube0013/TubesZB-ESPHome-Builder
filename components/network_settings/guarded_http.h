#pragma once
#include "internet_policy.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/core/log.h"

namespace esphome::network_settings {
// The ESPHome update entity can be checked directly by API or its own web UI.
// Gate its transport as well as the YAML/UI entry points; no HTTP request is
// made when offline. Local uploads use the independent OTA server.
class GuardedHttp : public http_request::HttpRequestComponent {
 public:
  void bind(InternetPolicy *policy, http_request::HttpRequestComponent *parent) {
    policy_ = policy; parent_ = parent; set_timeout(parent->get_timeout());
  }
 protected:
  std::shared_ptr<http_request::HttpContainer> perform(const char *url, const char *method,
      const std::string &body, const std::vector<http_request::Header> &headers,
      const std::vector<std::string> &collect) override {
    if (!policy_->allowed()) {
      ESP_LOGI("network_settings", "Skipping firmware manifest: Use without internet is enabled or not restored yet");
      return nullptr;
    }
    return parent_->start(url, method, body, headers, collect);
  }
  InternetPolicy *policy_{nullptr};
  http_request::HttpRequestComponent *parent_{nullptr};
};
}  // namespace esphome::network_settings
