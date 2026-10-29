#include "components/network_settings/trial_state.h"
#include "components/network_settings/socket_address.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace esphome::network_settings;

static NetworkConfig static_config() {
  NetworkConfig result{1};
  assert(parse_ipv4("192.168.1.50", result.ip));
  assert(parse_ipv4("255.255.255.0", result.subnet));
  assert(parse_ipv4("192.168.1.1", result.gateway));
  result.dns1 = result.gateway;
  return result;
}

int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "parse") {
    uint32_t ip;
    std::cout << parse_ipv4(argv[2], ip);
    return 0;
  }
  if (argc == 8 && std::string(argv[1]) == "validate") {
    NetworkConfig config{};
    std::string mode = argv[2];
    bool valid = mode == "dhcp" || mode == "static";
    config.is_static = mode == "static";
    uint32_t *fields[] = {&config.ip, &config.subnet, &config.gateway, &config.dns1, &config.dns2};
    if (config.is_static) for (int i = 0; i < 5; i++) {
      if (i == 4 && std::string(argv[i + 3]).empty()) continue;
      valid = parse_ipv4(argv[i + 3], *fields[i]) && valid;
    }
    std::cout << (valid && validate_config(config) == nullptr);
    return 0;
  }
  SavedState flash{};
  unsigned writes = 0;
  auto save = [&](const SavedState &value) { flash = value; writes++; return true; };
  auto fail = [](const SavedState &) { return false; };
  const auto config = static_config();
  sockaddr_storage address{};
  auto *ipv4 = reinterpret_cast<sockaddr_in *>(&address);
  ipv4->sin_family = AF_INET;
  inet_pton(AF_INET, "192.168.1.50", &ipv4->sin_addr);
  assert(socket_reached_ipv4(address, config.ip));
  assert(!socket_reached_ipv4(address, config.gateway));
  auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(&address);
  ipv6->sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::ffff:192.168.1.50", &ipv6->sin6_addr);
  assert(socket_reached_ipv4(address, config.ip));
  assert(!socket_reached_ipv4(address, config.gateway));
  inet_pton(AF_INET6, "fe80::1234", &ipv6->sin6_addr);
  assert(!socket_reached_ipv4(address, config.ip));
  TrialState first;
  assert(first.boot(0, save));
  assert(!first.testing && !first.active.is_static && writes == 0);
  assert(!first.stage(config, fail));
  assert(first.saved.phase == Phase::CONFIRMED && !flash.confirmed.is_static);
  assert(first.stage(config, save));
  assert(!first.active.is_static && flash.phase == Phase::STAGED);
  assert(!first.stage(config, save));

  TrialState trial;
  trial.saved = flash;
  assert(trial.boot(5000, save));
  assert(trial.testing && trial.active.ip == config.ip && flash.phase == Phase::TESTING);
  assert(!trial.stage({}, save));
  assert(trial.remaining_seconds(5000) == 120);
  assert(!trial.expired(124999) && trial.expired(125000));
  assert(!trial.finish(true, 125000, save));  // Late confirmation never wins.
  assert(trial.testing && trial.remaining_seconds(125000) == 0);
  assert(!trial.finish(false, 125000, fail));
  assert(trial.testing && flash.phase == Phase::TESTING);

  // Power loss at any point during an unconfirmed trial restores DHCP.
  TrialState interrupted;
  interrupted.saved = flash;
  assert(interrupted.boot(0, save));
  assert(!interrupted.testing && !interrupted.active.is_static && flash.phase == Phase::CONFIRMED);
  assert(interrupted.stage(config, save));
  TrialState confirmed;
  confirmed.saved = flash;
  assert(confirmed.boot(100, save));
  assert(!confirmed.finish(true, 200, fail));
  assert(confirmed.testing && flash.phase == Phase::TESTING);
  assert(confirmed.finish(true, 201, save));
  assert(!confirmed.testing && flash.confirmed.ip == config.ip);
  assert(!confirmed.finish(true, 202, save));
  TrialState next;
  next.saved = flash;
  assert(next.boot(0, save));
  assert(!next.testing && next.active.ip == config.ip);

  // DHCP is also a trial, with a static confirmed configuration to return to.
  assert(next.stage({}, save));
  TrialState dhcp;
  dhcp.saved = flash;
  assert(dhcp.boot(0, save));
  assert(dhcp.testing && !dhcp.active.is_static);
  assert(dhcp.finish(false, 1, save));
  TrialState reverted;
  reverted.saved = flash;
  assert(reverted.boot(0, save) && reverted.active.ip == config.ip);

  // A failed boot marker must never activate the candidate address.
  assert(reverted.stage({}, save));
  TrialState failed_boot;
  failed_boot.saved = flash;
  assert(!failed_boot.boot(0, fail));
  assert(!failed_boot.testing && failed_boot.active.ip == config.ip);

  // millis() wraps roughly every 49 days. The deadline still holds.
  TrialState wrap;
  wrap.saved = flash;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 100;
  assert(wrap.boot(start, save));
  assert(!wrap.expired(start + 119999) && wrap.expired(start + 120000));
  assert(wrap.finish(false, start + 120000, save));

  assert(valid_saved_state(flash));
  flash.magic = 0;
  assert(!valid_saved_state(flash));
  flash = {};
  flash.phase = Phase(99);
  assert(!valid_saved_state(flash));
  flash = {};
  flash.confirmed.is_static = 1;
  assert(!valid_saved_state(flash));
  std::cout << "Network boot, persistence failure, confirmation, timeout, rollback, and wraparound tests passed.\n";
}
