#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace esphome::network_settings {

// All addresses are stored in host-independent, most-significant-octet-first order.
struct NetworkConfig {
  uint32_t is_static{0};
  uint32_t ip{0};
  uint32_t subnet{0};
  uint32_t gateway{0};
  uint32_t dns1{0};
  uint32_t dns2{0};
};

inline bool parse_ipv4(const std::string &text, uint32_t &result) {
  if (text.empty() || text.size() > 15)
    return false;
  uint32_t value = 0;
  size_t pos = 0;
  for (unsigned part = 0; part < 4; part++) {
    const size_t start = pos;
    unsigned octet = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      octet = octet * 10 + (text[pos++] - '0');
      if (pos - start > 3 || octet > 255)
        return false;
    }
    // Reject ambiguous octal-looking notation as well as abbreviated addresses.
    if (pos == start || (pos - start > 1 && text[start] == '0'))
      return false;
    value = (value << 8) | octet;
    if (part < 3 && (pos >= text.size() || text[pos++] != '.'))
      return false;
  }
  if (pos != text.size())
    return false;
  result = value;
  return true;
}

inline std::string format_ipv4(uint32_t ip) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", unsigned(ip >> 24), unsigned((ip >> 16) & 255),
           unsigned((ip >> 8) & 255), unsigned(ip & 255));
  return buffer;
}

inline bool unicast_ipv4(uint32_t ip) {
  const uint32_t first = ip >> 24;
  return first != 0 && first != 127 && first < 224 && (ip & 0xFFFF0000U) != 0xA9FE0000U;
}

inline const char *validate_config(const NetworkConfig &config) {
  if (config.is_static > 1)
    return "Invalid address mode.";
  if (!config.is_static)
    return nullptr;
  if (!unicast_ipv4(config.ip))
    return "IP address must be a usable unicast IPv4 address (no loopback or link-local addresses).";
  const uint32_t hosts = ~config.subnet;
  // Ordinary LANs only: /1 through /30. /31 and /32 require different routing semantics.
  if (config.subnet == 0 || hosts < 3 || (hosts & (hosts + 1)) != 0)
    return "Subnet mask must be contiguous, from /1 through /30.";
  const uint32_t host = config.ip & hosts;
  if (host == 0 || host == hosts)
    return "IP address cannot be the subnet's network or broadcast address.";
  if (!unicast_ipv4(config.gateway) || config.gateway == config.ip ||
      (config.gateway & config.subnet) != (config.ip & config.subnet) ||
      (config.gateway & hosts) == 0 || (config.gateway & hosts) == hosts)
    return "Gateway must be a different usable host in the same subnet.";
  if (!unicast_ipv4(config.dns1))
    return "Primary DNS must be a usable IPv4 address.";
  if (config.dns2 != 0 && !unicast_ipv4(config.dns2))
    return "Secondary DNS must be empty or a usable IPv4 address.";
  return nullptr;
}

}  // namespace esphome::network_settings
