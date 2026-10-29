# CC2652 Flasher (ESPHome external component)

Flash Texas Instruments CC2652 (P2/P7) Zigbee radios over UART using the ROM bootloader. Automatically detects the module variant (P2 vs P7), selects the right firmware from a JSON manifest, and provides buttons/actions + text sensors for visibility.

## Features
- Auto‑detect variant
  - Order: YAML `variant` override → ROM BSL probe → ZNP SYS_VERSION (if definitive)
- Manifest with variants (P2/P7) and fallbacks
- HEX and BIN streaming (Intel HEX parsed on‑device)
- Erase modes: `sector`, `bank` (default), `auto`
- Actions: update, erase, check update, detect variant
- Text sensors: current FW code revision, IEEE address, latest manifest version, detected variant

## Requirements
- ESPHome 2025.8.3
- ESP32 (ESP‑IDF) with a UART connected to the CC2652 and two GPIOs:
  - Reset line
  - BSL/BOOT line (GPIO that pulls the CC2652 boot pin)

## Install (external_components)
```yaml
external_components:
  # optional, for UART streaming/debug
  - source: github://oxan/esphome-stream-server
  # main component
  - source: github://tube0013/esphome-components
    components: [cc2652_flasher]
```

## YAML example
```yaml
esp32:
  board: esp32-poe
  framework:
    type: esp-idf   # This component is ESP-IDF only

uart:
  id: uart_bus
  rx_pin: GPIO36
  tx_pin: GPIO4
  baud_rate: 115200
  rx_buffer_size: 1024

switch:
  - platform: gpio
    id: zRST_gpio
    pin: 5
    inverted: true
    restore_mode: ALWAYS_OFF
  - platform: gpio
    id: zBSL
    pin: 16
    inverted: true
    restore_mode: ALWAYS_OFF

# Optional debug stream (remove if not needed)
stream_server:
  uart_id: uart_bus
  port: 6638

cc2652_flasher:
  id: zigbee_flasher
  uart_id: uart_bus
  url: https://example.com/path/to/manifest.json
  bsl_switch: zBSL
  rst_switch: zRST_gpio
  boot_baud_rate: 500000
  restore_baud_rate: 115200
  erase_mode: bank  # default; sector | bank | auto
  debug: false
  show_progress: true
  progress_step: 5
  fw_version_text: zb_fw_version
  ieee_text: zb_ieee
  latest_fw_version_text: zb_latest_fw
  variant_text: zb_variant
  check_on_boot: true
  check_interval_days: 7
  detect_on_boot: true
  detect_on_boot_delay_ms: 0
  # Optional: force variant when radio cannot answer
  # variant: p2  # or p7

button:
  - platform: template
    name: Flash CC2652
    on_press:
      - cc2652_flasher.update_firmware: { id: zigbee_flasher }
  - platform: template
    name: Check CC2652 Update
    on_press:
      - cc2652_flasher.check_update: { id: zigbee_flasher }
  - platform: template
    name: Detect CC2652 Variant
    on_press:
      - cc2652_flasher.detect_variant: { id: zigbee_flasher }

text_sensor:
  - platform: template
    id: zb_fw_version
    name: Zigbee FW Version
  - platform: template
    id: zb_ieee
    name: Zigbee IEEE
  - platform: template
    id: zb_latest_fw
    name: Zigbee Latest FW
  - platform: template
    id: zb_variant
    name: Zigbee Variant
```

## Actions
- `cc2652_flasher.update_firmware` – Fetches manifest, selects variant, erases as needed, programs image, verifies MD5, resets target.
- `cc2652_flasher.erase_flash` – Erase only. Optional mode: `sector|bank|auto`.
- `cc2652_flasher.check_update` – Fetches manifest and publishes the latest version to `latest_fw_version_text`.
- `cc2652_flasher.detect_variant` – Briefly enters ROM BSL to read chip IDs and publish the detected variant.

## Manifest formats
Preferred (object):
```json
{
  "version": "20250321",
  "variants": {
    "p2": { "fw_url": "https://…/cc2652p2.hex", "md5": "…", "size": 507877 },
    "p7": { "fw_url": "https://…/cc2652p7.hex", "md5": "…", "size": 508798 }
  }
}
```

Preferred (array):
```json
{
  "version": "20250321",
  "variants": [
    { "match": { "variant": "p2" }, "fw_url": "https://…/cc2652p2.hex" },
    { "match": { "variant": "p7" }, "fw_url": "https://…/cc2652p7.hex" }
  ]
}
```

Legacy supported:
- `fw_url_p2` / `fw_url_p7`
- Single `fw_url` / `firmware_url` / `url`
- MD5 fields: `md5`, `md5sum`, `md5_hex`
- Size fields can be number or string (`size` / `length`)
- Version fields: `version`, `code_revision`, or `rev`

## Notes
- Variant detection uses ROM BSL registers to differentiate P2 vs P7 when ZNP cannot.
- If your radio cannot answer, set `variant: p2|p7` to force selection for flashing.
- For local manifests, serve the JSON over HTTP and use its URL; the ESP cannot read files from your workstation.
- Links `ArduinoJson` and `MD5` automatically via AUTO_LOAD (no manual libs needed).
- Requires ESP-IDF framework for ESP32 (Arduino framework is not supported).

## License
This component is licensed under the GNU General Public License v3.0 (GPLv3).

- Portions inspired by work in the XZG project (Arduino CC2652 flashing utilities).
- You should have received a copy of the GNU General Public License
  along with this program. If not, see https://www.gnu.org/licenses/

