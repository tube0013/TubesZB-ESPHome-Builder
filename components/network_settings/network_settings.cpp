#include "network_settings.h"
#include "socket_address.h"

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/json/json_util.h"
#include <esp_netif.h>
#include <lwip/sockets.h>
#include <nvs.h>

namespace esphome::network_settings {

static const char *const TAG = "network_settings";

static network::IPAddress to_address(uint32_t ip) {
  return network::IPAddress(ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

bool NetworkSettings::save_(const SavedState &state) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("tzb_network", NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_blob(handle, "state_v1", &state, sizeof(state));
    if (err == ESP_OK)
      err = nvs_commit(handle);
    nvs_close(handle);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Cannot persist network settings: %s", esp_err_to_name(err));
    storage_ready_ = false;
    return false;
  }
  return true;
}

bool NetworkSettings::set_use_without_internet(bool value) {
  std::lock_guard<std::mutex> lock(mutex_);
  return set_use_without_internet_(value);
}

bool NetworkSettings::set_use_without_internet_(bool value) {
  if (!storage_ready_ || restart_pending_ || state_.testing || state_.saved.phase == Phase::STAGED ||
      (mutation_guard_ && !mutation_guard_())) {
    ESP_LOGW(TAG, "Wait for network/firmware operations before changing internet use");
    return false;
  }
  bool ok = internet_policy_.set(value, [](bool offline) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("tzb_network", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
      err = nvs_set_u8(handle, "offline_v1", offline ? 1 : 0);
      if (err == ESP_OK) err = nvs_commit(handle);
      nvs_close(handle);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "Cannot save internet use: %s", esp_err_to_name(err));
    return err == ESP_OK;
  });
  if (ok) ESP_LOGI(TAG, "Use without internet: %s (saved)", value ? "ON" : "OFF");
  return ok;
}

void NetworkSettings::setup() {
  guarded_http_.bind(&internet_policy_, http_request_);
  update_->set_request_parent(&guarded_http_);
  nvs_handle_t handle;
  esp_err_t err = nvs_open("tzb_network", NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    uint8_t offline = 0;
    const esp_err_t policy_err = nvs_get_u8(handle, "offline_v1", &offline);
    if ((policy_err == ESP_OK && offline <= 1) || policy_err == ESP_ERR_NVS_NOT_FOUND) {
      internet_policy_.restore(offline != 0);
    } else {
      storage_ready_ = false;  // Policy remains fail-closed on an unreadable value.
      ESP_LOGE(TAG, "Cannot restore internet policy; online checks disabled");
    }
    SavedState loaded{};
    size_t size = sizeof(loaded);
    err = nvs_get_blob(handle, "state_v1", &loaded, &size);
    if (err == ESP_OK && size == sizeof(loaded) && valid_saved_state(loaded)) {
      state_.saved = loaded;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGW(TAG, "Invalid/unreadable network settings; using DHCP");
    }
    nvs_close(handle);
  } else {
    storage_ready_ = false;
    ESP_LOGE(TAG, "Network settings storage unavailable: %s", esp_err_to_name(err));
  }
  if (!state_.boot(millis(), [this](const SavedState &value) { return save_(value); }))
    ESP_LOGE(TAG, "Trial transition could not be saved; using confirmed network settings");

  // This runs before EthernetComponent::setup(), keeping its own reconnect path
  // consistent with the selected configuration for the entire lifetime of this boot.
  if (state_.active.is_static) {
    ethernet::ManualIP ip{};
    ip.static_ip = to_address(state_.active.ip);
    ip.subnet = to_address(state_.active.subnet);
    ip.gateway = to_address(state_.active.gateway);
    ip.dns1 = to_address(state_.active.dns1);
    ip.dns2 = to_address(state_.active.dns2);
    ethernet_->set_manual_ip(ip);
  }
  char nonce[33];
  snprintf(nonce, sizeof(nonce), "%08x%08x%08x%08x", unsigned(random_uint32()), unsigned(random_uint32()),
           unsigned(random_uint32()), unsigned(random_uint32()));
  nonce_ = nonce;
  // Uses the standard web-server authentication middleware when configured.
  base_->add_handler(this);
  ESP_LOGI(TAG, "Internet checks: %s (restored before Ethernet)", internet_allowed() ? "enabled" : "disabled");
}

void NetworkSettings::queue_restart_() {
  restart_pending_ = true;
  restart_at_ = millis() + 1500;  // Let the response reach the browser first.
}

void NetworkSettings::loop() {
  bool restart = false;
  {
    // HTTP handlers run on the IDF HTTP task, not ESPHome's main loop.
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.expired(millis()) && !restart_pending_) {
      ESP_LOGW(TAG, "Network trial expired; restoring confirmed configuration");
      state_.finish(false, millis(), [this](const SavedState &value) { return save_(value); });
      // Even if flash fails, the persisted TESTING marker restores the confirmed
      // settings on the next boot. Do not leave an unreachable trial running.
      queue_restart_();
    }
    restart = restart_pending_ && int32_t(millis() - restart_at_) >= 0;
  }
  if (restart)
    App.safe_reboot();
}

void NetworkSettings::dump_config() {
  ESP_LOGCONFIG(TAG, "Web network settings: /network (%s%s)", state_.active.is_static ? "static" : "DHCP",
                state_.testing ? ", awaiting confirmation" : "");
}

bool NetworkSettings::canHandle(AsyncWebServerRequest *request) const {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  auto url = request->url_to(buffer);
  return url == "/network" || url == "/network/" || url == "/network/status" || url == "/network/apply" ||
         url == "/network/confirm" || url == "/network/revert" || url == "/network/internet";
}

void NetworkSettings::send_json_(AsyncWebServerRequest *request, int code, const std::string &body) {
  auto *response = request->beginResponse(code, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void NetworkSettings::send_error_(AsyncWebServerRequest *request, int code, const char *message) {
  auto body = json::build_json([message](JsonObject root) { root["error"] = message; });
  send_json_(request, code, std::string(body.c_str(), body.size()));
}

void NetworkSettings::status_(AsyncWebServerRequest *request) {
  esp_netif_ip_info_t info{};
  esp_netif_dns_info_t dns1{}, dns2{};
  auto *netif = ethernet_->get_esp_netif();
  if (netif != nullptr) {
    esp_netif_get_ip_info(netif, &info);
    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns1);
    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns2);
  }
  auto body = json::build_json([&](JsonObject root) {
    root["mode"] = state_.active.is_static ? "static" : "dhcp";
    root["testing"] = state_.testing;
    root["remaining"] = state_.remaining_seconds(millis());
    root["restarting"] = restart_pending_;
    const bool staged = state_.saved.phase == Phase::STAGED;
    root["trial_pending"] = staged;
    const auto &next = staged ? state_.saved.candidate : state_.saved.confirmed;
    root["next_ip"] = next.is_static ? format_ipv4(next.ip) : "";
    root["storage_ok"] = storage_ready_;
    root["use_without_internet"] = use_without_internet();
    root["internet_allowed"] = internet_allowed();
    root["nonce"] = nonce_;
    root["ip"] = format_ipv4(ntohl(info.ip.addr));
    root["subnet"] = format_ipv4(ntohl(info.netmask.addr));
    root["gateway"] = format_ipv4(ntohl(info.gw.addr));
    root["dns1"] = format_ipv4(ntohl(dns1.ip.u_addr.ip4.addr));
    root["dns2"] = format_ipv4(ntohl(dns2.ip.u_addr.ip4.addr));
    root["return_ip"] = state_.saved.confirmed.is_static ? format_ipv4(state_.saved.confirmed.ip) : "";
    root["hostname"] = App.get_name() + ".local";
  });
  send_json_(request, 200, std::string(body.c_str(), body.size()));
}

bool NetworkSettings::authorize_(AsyncWebServerRequest *request) {
  const auto token = request->get_header("X-TubesZB-Network");
  const auto origin = request->get_header("Origin");
  const auto host = request->get_header("Host");
  // The shared ESPHome server enables CORS. Explicitly restrict mutations to
  // this origin, and require a boot-specific nonce, including for command-line clients.
  if (!token.has_value() || *token != nonce_ ||
      (origin.has_value() && (!host.has_value() ||
       (*origin != "http://" + *host && *origin != "https://" + *host)))) {
    send_error_(request, 401, "Reload network settings on this device before making changes.");
    return false;
  }
  if (!storage_ready_) {
    send_error_(request, 500, "Network settings storage failed. Restart the device before trying again.");
    return false;
  }
  if (restart_pending_) {
    send_error_(request, 409, "The device is restarting.");
    return false;
  }
  if (mutation_guard_ && !mutation_guard_()) {
    send_error_(request, 409, "Wait for the radio or firmware operation to finish before changing the network.");
    return false;
  }
  return true;
}

bool NetworkSettings::at_trial_address_(AsyncWebServerRequest *request) {
  // A stale tab, a proxy, or another address must not confirm an unreachable IP.
  // For DHCP trials the exact address is unknown until the lease is obtained.
  if (!state_.active.is_static)
    return ethernet_->is_connected();
  sockaddr_storage local{};
  socklen_t length = sizeof(local);
  int socket = httpd_req_to_sockfd(static_cast<httpd_req_t *>(*request));
  return getsockname(socket, reinterpret_cast<sockaddr *>(&local), &length) == 0 &&
         socket_reached_ipv4(local, state_.active.ip);
}

void NetworkSettings::apply_(AsyncWebServerRequest *request) {
  if (state_.testing) {
    send_error_(request, 409, "Keep or revert the current trial before applying another configuration.");
    return;
  }
  const auto mode = request->arg("mode");
  if (mode != "static" && mode != "dhcp") {
    send_error_(request, 400, "Address mode must be DHCP or Static.");
    return;
  }
  NetworkConfig config{};
  config.is_static = mode == "static";
  if (config.is_static) {
    const char *fields[] = {"ip", "subnet", "gateway", "dns1", "dns2"};
    uint32_t *values[] = {&config.ip, &config.subnet, &config.gateway, &config.dns1, &config.dns2};
    for (unsigned i = 0; i < 5; i++) {
      const auto text = request->arg(fields[i]);
      if (i == 4 && text.empty())
        continue;
      if (!parse_ipv4(text, *values[i])) {
        const std::string error = std::string(fields[i]) + ": enter four decimal numbers from 0 to 255.";
        send_error_(request, 400, error.c_str());
        return;
      }
    }
  }
  if (const char *error = validate_config(config)) {
    send_error_(request, 400, error);
    return;
  }
  if (!state_.stage(config, [this](const SavedState &value) { return save_(value); })) {
    send_error_(request, 500, "Could not save the trial configuration. The current network is unchanged.");
    return;
  }
  queue_restart_();
  send_json_(request, 200, "{\"ok\":true}");
}

void NetworkSettings::handleRequest(AsyncWebServerRequest *request) {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(buffer);
  if (request->method() == HTTP_GET && (url == "/network" || url == "/network/")) {
    auto *response = request->beginResponse(200, "text/html; charset=utf-8",
                                           reinterpret_cast<const uint8_t *>(NETWORK_PAGE), strlen(NETWORK_PAGE));
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Frame-Options", "DENY");
    request->send(response);
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (request->method() == HTTP_GET && url == "/network/status") {
    status_(request);
    return;
  }
  if (request->method() != HTTP_POST) {
    send_error_(request, 400, "Use GET for status and POST for changes.");
    return;
  }
  if (!authorize_(request))
    return;
  if (url == "/network/internet") {
    const auto value = request->arg("use_without_internet");
    if (value != "true" && value != "false") {
      send_error_(request, 400, "Choose whether to use the device without internet."); return;
    }
    if (!set_use_without_internet_(value == "true")) {
      send_error_(request, 409, "Could not save internet use. Finish active operations and try again."); return;
    }
    send_json_(request, 200, "{\"ok\":true}"); return;
  }
  if (url == "/network/apply") {
    apply_(request);
    return;
  }
  if (url != "/network/confirm" && url != "/network/revert") {
    send_error_(request, 404, "Unknown network settings action.");
    return;
  }
  const bool keep = url == "/network/confirm";
  if (!state_.testing || (keep && state_.expired(millis()))) {
    send_error_(request, 409, "No active network trial, or the confirmation time has expired.");
    return;
  }
  if (keep && !at_trial_address_(request)) {
    send_error_(request, 409, "Open network settings directly at the new IPv4 address to confirm.");
    return;
  }
  if (!state_.finish(keep, millis(), [this](const SavedState &value) { return save_(value); })) {
    send_error_(request, 500, "Could not save the configuration. Automatic rollback remains enabled.");
    return;
  }
  if (!keep)
    queue_restart_();
  send_json_(request, 200, "{\"ok\":true}");
}

}  // namespace esphome::network_settings
