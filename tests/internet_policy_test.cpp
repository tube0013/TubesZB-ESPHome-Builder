#include "components/network_settings/internet_policy.h"
#include <cassert>
using esphome::network_settings::InternetPolicy;
int main() {
  InternetPolicy boot;
  assert(!boot.allowed());  // No check before restoration, even on a fresh device.
  int writes = 0;
  auto save = [&](bool) { ++writes; return true; };
  assert(!boot.set(false, save)); assert(writes == 0);
  boot.restore(false); assert(boot.allowed());
  assert(boot.set(true, save)); assert(!boot.allowed()); assert(writes == 1);
  assert(boot.set(true, save)); assert(writes == 1);  // No flash writes on polling.
  assert(!boot.set(false, [](bool) { return false; }));
  assert(!boot.allowed());  // Failed commit cannot claim a new policy.
  InternetPolicy reboot; assert(!reboot.allowed());
  reboot.restore(true); assert(reboot.offline() && !reboot.allowed());
  assert(reboot.set(false, save)); assert(reboot.allowed());
}
