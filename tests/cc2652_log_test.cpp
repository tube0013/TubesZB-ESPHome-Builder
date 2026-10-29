#include "components/tubeszb_web/cc2652_log.h"
#include <cassert>

int main() {
  using esphome::tubeszb_web::decode_cc2652_log;
  using E = esphome::tubeszb_web::CC2652LogEvent;
  assert(decode_cc2652_log("Starting firmware update…").kind == E::FLASH_START);
  assert(decode_cc2652_log("Checking CC2652 firmware against manifest…").kind == E::CHECK_START);
  assert(decode_cc2652_log("Detecting CC2652 variant via ROM bootloader…").kind == E::PROBE_START);
  auto progress = decode_cc2652_log("Progress: 55% (275/500)");
  assert(progress.kind == E::PROGRESS && progress.progress == 55);
  assert(decode_cc2652_log("Progress: 100% (500/500)").kind == E::PROGRESS);
  assert(decode_cc2652_log("Progress: 65536 bytes").kind == E::NONE);
  assert(decode_cc2652_log("Progress: 101% (501/500)").kind == E::NONE);
  assert(decode_cc2652_log("Progress: %").kind == E::NONE);
  assert(decode_cc2652_log("BIN transfer complete. bytes=500 md5=abc").kind == E::NONE);
  assert(decode_cc2652_log("Update failed.").kind != E::FLASH_OK);
  assert(decode_cc2652_log("MD5 mismatch: expected abc").kind != E::FLASH_OK);
  assert(decode_cc2652_log("Update finished OK. Resetting target…").kind == E::FLASH_OK);
  assert(decode_cc2652_log("Transfer verified. Resetting target…").kind == E::NONE);
  assert(decode_cc2652_log("Update finished OK. Application UART baud ready.").kind == E::FLASH_OK);
  assert(decode_cc2652_log("Variant detection: CC2652P2").kind == E::PROBE_OK);
  assert(decode_cc2652_log("Variant detection: CC2652P7").kind == E::PROBE_OK);
  assert(decode_cc2652_log("Variant detection failed.").kind == E::PROBE_FAILED);
  assert(decode_cc2652_log("Variant detection inconclusive.").kind == E::PROBE_FAILED);
  assert(decode_cc2652_log("Manual bootloader requested.").kind == E::BOOTLOADER_START);
  assert(decode_cc2652_log("Manual bootloader ready.").kind == E::BOOTLOADER_READY);
}
