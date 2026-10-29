#include "components/cc2652_flash_guard/cc2652_flash_session.h"
#include <cassert>

int main() {
  using esphome::cc2652_flash_guard::FlashSession;
  FlashSession session;
  session.observe(true,"Unrelated error before flash");assert(!session.completed);
  assert(session.begin(false));assert(session.resume_after);
  assert(!session.begin(true));assert(session.resume_after);  // repeated start cannot lose prior state
  session.observe(true,"A failed background check before the flash starts");assert(!session.completed);
  session.observe(false,"Starting firmware update…");assert(session.started);
  session.observe(false,"Progress: 100% (500/500)");assert(!session.completed);
  session.observe(false,"BIN transfer complete. bytes=500 md5=abc");assert(!session.completed);
  session.observe(false,"Update finished OK. Resetting target…");assert(session.completed && session.success);
  assert(session.active);  // observing success never releases the UART from a log callback
  session.end();assert(!session.active);

  assert(session.begin(false));assert(!session.completed && !session.success);
  session.observe(false,"Starting firmware update…");
  session.observe(true,"Manifest fetch/parse failed.");
  assert(session.completed && !session.success && session.resume_after && session.active);
  session.end();

  assert(session.begin(true));assert(!session.resume_after);  // manual pause must survive cleanup
  session.observe(false,"Starting firmware update…");
  session.observe(true,"SEND_DATA failed, retrying");
  session.observe(false,"Update finished OK. Resetting target…");assert(session.success);
  session.end();assert(!session.active && !session.resume_after);
}
