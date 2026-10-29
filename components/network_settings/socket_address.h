#pragma once

#include <cstdint>
#include <cstring>
#ifdef USE_ESP32
#include <lwip/sockets.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

namespace esphome::network_settings {

inline bool socket_reached_ipv4(const sockaddr_storage &address, uint32_t expected) {
  if (address.ss_family == AF_INET)
    return ntohl(reinterpret_cast<const sockaddr_in *>(&address)->sin_addr.s_addr) == expected;
#if !defined(USE_ESP32) || CONFIG_LWIP_IPV6
  if (address.ss_family == AF_INET6) {
    const auto &ip = reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_addr;
    if (!IN6_IS_ADDR_V4MAPPED(&ip))
      return false;
    uint32_t mapped;
    memcpy(&mapped, &ip.s6_addr[12], sizeof(mapped));
    return ntohl(mapped) == expected;
  }
#endif
  return false;
}

}  // namespace esphome::network_settings
