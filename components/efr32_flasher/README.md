# EFR32 Flasher (ESPHome External Component)

`efr32_flasher` adds firmware update support for EFR32 Zigbee NCPs to ESPHome.
It downloads a `.gbl` firmware image from a JSON manifest, enters the NCP’s
serial bootloader, and streams the image via XMODEM-CRC. Manifest variants can
be selected automatically using board/mfg information probed by the
[`efr32_info`](../efr32_info/README.md) component.

## Features

* Fetch firmware metadata from a JSON manifest (version, download URL, MD5,
  size). Supports multiple variants (e.g. MGM24 vs BM24).
* Optional auto-detection of the correct variant using manufacturing tokens
  (board name, MFG string) gathered by `efr32_info`.
* Hardware bootloader control via configurable GPIO switches (RST / BSL).
* Progress logging with optional percentage updates.
* Actions to “check for update” (manifest only) and “update firmware” (full
  bootloader flash).
* Safety hooks: exposes a `busy` binary sensor, pauses `stream_server` if you
  wire one in, clears UART buffers before/after flashing.

## Requirements

* ESPHome 2025.8.0 or newer (tested on 2025.8.3).
* `efr32_info` recommended so we can auto-select manifest variants.
* UART wiring to the EFR32 module. MG24 uses RTS/CTS; MGM12/MGM210 two-wire profiles set `hardware_flow_control: false`.
* GPIOs wired to the module’s RST and BSL pins.

## Manifest Format

`efr32_flasher` expects the manifest JSON to look like:

```json
{
  "version": "7.5.0",
  "variants": {
    "BM24": {
      "fw_url": "https://…/tubeszb-BM-MG24-7.5.0.gbl",
      "md5": "62fbe6f5…",
      "size": 253504
    },
    "MGM24": {
      "fw_url": "https://…/tubeszb-mgm24-7.5.0.gbl",
      "md5": "d9b7f04b…",
      "size": 254508
    }
  }
}
```

`fw_url` and `md5` are required for each variant. If `variants` is omitted the
root-level `fw_url`, `md5` and optional `size` are used.

## Configuration

```yaml
external_components:
  - source: github://youruser/yourrepo@latest
    components: [efr32_flasher, efr32_info, zigbee_firmware_baud]

uart:
  id: uart_bus
  rx_pin: GPIO36
  tx_pin: GPIO4
  rts_pin: GPIO2
  cts_pin: GPIO13
  baud_rate: 115200
  hw_flow_control: CTS_RTS

switch:
  - platform: gpio
    id: zRST_gpio
    pin: GPIO5
    inverted: true
    restore_mode: ALWAYS_OFF

  - platform: gpio
    id: zBSL
    pin: GPIO16
    inverted: true
    restore_mode: ALWAYS_OFF
    disabled_by_default: true

binary_sensor:
  - platform: template
    id: ncp_busy

text_sensor:
  - platform: template
    id: board_name
  - platform: template
    id: mfg_string
  - platform: template
    id: zb_latest_fw
  - platform: template
    id: ezsp_firmware

efr32_flasher:
  id: zigbee_flasher
  uart_id: uart_bus
  url: https://…/efr32manifest.json
  boot_switch: zBSL
  rst_switch: zRST_gpio
  busy_binary_sensor: ncp_busy
  show_progress: true
  progress_step: 5
  variant: auto          # auto | bm24 | mgm24
  baud_rate: 460800      # optional runtime rate; omit to keep UART default
  bootloader_baud_rate: 115200  # optional, defaults to 115200
  board_name_text: board_name
  mfg_string_text: mfg_string
  latest_fw_version_text: zb_latest_fw
  fw_version_text: ezsp_firmware
```

### Options

| Option                   | Type                         | Description                                                           |
|--------------------------|------------------------------|-----------------------------------------------------------------------|
| `uart_id`                | `uart::UARTComponent`        | UART connected to the NCP bootloader.                                 |
| `boot_switch`            | `switch::Switch`             | GPIO controlling the bootloader/BM pin (active high).                 |
| `rst_switch`             | `switch::Switch`             | GPIO controlling reset.                                               |
| `pause_switch`           | `switch::Switch` (optional)  | Optional stream-server pause switch (not required).                   |
| `busy_binary_sensor`     | `binary_sensor::BinarySensor`| Publishes `true` during check/update operations.                      |
| `url`                    | `string`                     | Manifest URL (HTTPS supported).                                       |
| `variant`                | `auto`/`bm24`/`mgm24`        | Override variant. `auto` uses `board_name_text`/`mfg_string_text`.    |
| `baud_rate`              | `int` (9600–921600)          | Optional runtime UART speed; leave unset to keep the UART component setting. |
| `bootloader_baud_rate`   | `int` (9600–921600)          | UART speed used while the bootloader is active (default 115200).      |
| `board_name_text`        | `text_sensor::TextSensor`    | Optional – feed board name from `efr32_info`.                         |
| `mfg_string_text`        | `text_sensor::TextSensor`    | Optional – feed manufacturing string from `efr32_info`.               |
| `latest_fw_version_text` | `text_sensor::TextSensor`    | Publishes manifest version.                                           |
| `fw_version_text`        | `text_sensor::TextSensor`    | Optional “current firmware” display.                                  |
| `show_progress`          | `bool`                       | Emit progress logs during flashing (default true).                    |
| `progress_step`          | `int` 1–50                   | Percentage increments for progress logs.                              |

### Automations

```yaml
button:
  - platform: template
    name: "Check EFR32 Update"
    internal: true
    on_press:
      - efr32_flasher.check_update: zigbee_flasher

  - platform: template
    name: "Flash EFR32"
    internal: true
    on_press:
      - efr32_flasher.update_firmware: zigbee_flasher
```

The component provides two automation actions:
* `efr32_flasher.check_update` – fetch manifest, update `latest_fw_version_text`.
* `efr32_flasher.update_firmware` – enter bootloader and flash the firmware.

## Recommended Boot Flow

Run a probe first so auto-detection has the latest board info, then check the
manifest:

```yaml
script:
  - id: boot_probe_sequence
    mode: queued
    then:
      - efr32_info.probe: zigbee_info
      - wait_until:
          condition:
            lambda: 'return !id(ncp_busy).state;'
          timeout: 45s
      - delay: 500ms
      - efr32_flasher.check_update: zigbee_flasher
```

## Troubleshooting

* **Variant auto-detect fails:** ensure `board_name_text` / `mfg_string_text`
  sensors are hooked up and that `efr32_info` successfully publishes tokens. Use
  DEBUG logging to see the normalized values.
* **Manifest fetch errors:** check network connectivity and that the manifest URL
  returns HTTP 200. The component currently follows up to five redirects.
* **Bootloader doesn’t start:** verify the GPIO wiring (RST/BSL) and the required
  delays for your hardware.

## Contributing

The component shares ASH helpers with `efr32_info` (`ash_util`). If you change
frame handling or CRC logic, keep both components synchronized. Future work:
* expose bootloader progress via a sensor instead of log-only
* add retry/back-off for manifest fetch
* upstream stream-server pause/resume APIs

## Builder extensions

This bundled version is tested with ESPHome 2026.10.1. `variant: flat` disables
MG24/BM24 identification and rejects variant catalogs, for model-specific older
radio manifests. `hardware_flow_control: false` preserves the configured TX/RX
pins without applying MG24 RTS/CTS. The default remains the MG24 hardware-flow
profile for existing MG24 configurations.

An optional manifest `baudrate` integer changes and persists the application UART
rate only after successful programming, before the busy sensor releases serial
ownership. See [baud-rate behavior](../../docs/zigbee-manifest-baudrate.md) and
[older-board mappings and feeds](../../docs/older-efr32-flashing.md).

`bootloader_entry: ezsp` enables software entry and omits `boot_switch`; the
default `gpio` mode still requires that switch. The native
`efr32_flasher.enter_bootloader` action supports the manual UI button and the
flash workflow. EZSP uses application baud for negotiation and requires the
Gecko menu at bootloader baud before transferring firmware. See
[MGM21 dual flashing](../../docs/mgm21-dual-flashing.md) for recovery and details.
