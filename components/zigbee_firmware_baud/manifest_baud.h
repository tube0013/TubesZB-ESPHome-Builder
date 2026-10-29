#pragma once
#include <ArduinoJson.h>
#include "baud_rate.h"

namespace esphome::zigbee_firmware_baud {

// Metadata belongs to the selected image. Root-level metadata is an optional
// default for old flat manifests or entries that omit the field.
inline bool read_baud(JsonVariantConst image, JsonVariantConst root, uint32_t &baud) {
  baud = 0;
  JsonVariantConst value;
  if (image.is<JsonObjectConst>() && !image["baudrate"].isUnbound())
    value = image["baudrate"];
  else if (root.is<JsonObjectConst>() && !root["baudrate"].isUnbound())
    value = root["baudrate"];
  else return true;
  if (!value.is<uint32_t>()) return false;
  baud = value.as<uint32_t>();
  return valid_baud(baud);
}
}  // namespace esphome::zigbee_firmware_baud
