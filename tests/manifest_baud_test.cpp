#include "components/zigbee_firmware_baud/manifest_baud.h"
#include <cassert>
using esphome::zigbee_firmware_baud::read_baud;
int main() {
  auto check = [](const char *json, bool valid, uint32_t expected) {
    JsonDocument doc; assert(!deserializeJson(doc, json)); uint32_t baud = 123;
    const auto root = doc.as<JsonVariantConst>();
    const auto image = root["image"].isNull() ? root : root["image"];
    assert(read_baud(image, root, baud) == valid);
    if (valid) assert(baud == expected);
  };
  check("{}", true, 0);
  check("{\"baudrate\":115200}", true, 115200);
  check("{\"baudrate\":115200,\"image\":{\"baudrate\":460800}}", true, 460800);
  check("{\"baudrate\":115200,\"image\":{\"url\":\"a.gbl\"}}", true, 115200);
  check("{\"other\":{\"baudrate\":false},\"image\":{\"baudrate\":921600}}", true, 921600);
  check("{\"baudrate\":null}", false, 0);
  check("{\"baudrate\":true}", false, 0);
  check("{\"baudrate\":\"460800\"}", false, 0);
  check("{\"baudrate\":460800.0}", false, 0);
  check("{\"baudrate\":-115200}", false, 0);
  check("{\"baudrate\":0}", false, 0);
  check("{\"baudrate\":1000000}", false, 0);
  check("{\"baudrate\":4294967296}", false, 0);
  check("{\"baudrate\":115200,\"image\":{\"baudrate\":null}}", false, 0);
}
