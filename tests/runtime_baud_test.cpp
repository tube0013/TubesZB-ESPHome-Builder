#include "components/zigbee_firmware_baud/runtime_baud.h"
#include <cassert>
using namespace esphome;
using zigbee_firmware_baud::RuntimeBaud;
int main() {
  RuntimeBaud first; first.set_preference_hash(123); first.setup(115200);
  assert(first.current() == 115200 && saved.empty());
  assert(first.commit(460800)); assert(saved.at(123) == 460800);
  RuntimeBaud reboot; reboot.set_preference_hash(123); reboot.setup(115200);
  assert(reboot.current() == 460800);
  reboot.manual(230400); assert(saved.at(123) == 230400);
  assert(!reboot.commit(0)); assert(!reboot.commit(1000000));
  assert(reboot.current() == 230400);
  select::Select selector;
  RuntimeBaud selected; selected.set_preference_hash(456); selected.set_selector(&selector);
  selector.value = "460800"; selected.setup(115200);
  assert(selected.current() == 460800); // Adopt a pre-existing user's saved selector.
  bool guarded = false;
  selector.callback = [&] { guarded = selected.applying_manifest(); };
  assert(selected.commit(115200)); assert(guarded && !selected.applying_manifest());
  assert(selector.value == "115200" && saved.at(456) == 115200);
  assert(!selected.commit(38400)); assert(selector.value == "115200");
  // Numeric persistence wins over a selector index that now points to a different rate.
  saved[456] = 921600; selector.value = "230400";
  RuntimeBaud reordered; reordered.set_preference_hash(456); reordered.set_selector(&selector);
  selector.callback = [&] { assert(reordered.applying_manifest()); };
  reordered.setup(115200); assert(reordered.current() == 921600 && selector.value == "921600");
  sync_ok = false;
  assert(!first.commit(115200)); assert(first.current() == 115200);
  sync_ok = true; save_ok = false;
  assert(!first.commit(460800)); assert(first.current() == 460800);
}
