# MGM21 dual-radio flashing over EZSP

Both `tubeszb-dual-radio-mgm21-2022` and `tubeszb-dr-mgm21-22-exp` now use the
native EFR32 flasher and the same controls as standalone MGM210 devices. They
include startup identity probing, stable radio checks/flashing, direct GBL URLs,
saved/manual and manifest-driven baud rates, manual bootloader entry, reset,
serial pause/resume, retained logs and top-banner progress/completion. Gateway
beta selection is saved; no radio beta feed is offered for these older images.

## Confirmed physical wiring

The owner's connector diagrams confirm the original pin selection:

| MGM21 header signal | ESP32 pin | Use |
| --- | --- | --- |
| zRX | GPIO16 | Zigbee UART TX |
| zTX | GPIO5 | Zigbee UART RX |
| nRST | GPIO2 | Active-low reset output |
| PA0 / physical boot button | GPIO36 | Input only; never configured as an output |
| Z-Wave UART RX / TX | GPIO15 / GPIO14 | Separate Z-Wave proxy UART |

GPIO36 cannot assert PA0. These profiles configure `bootloader_entry: ezsp` and
omit `boot_switch`, rather than assigning a fictitious output. GPIO36 remains a
physical-button diagnostic input with active-low indication.

## Native flasher entry modes

`components/efr32_flasher` now supports:

```yaml
# Existing MG24 and standalone MGM12/MGM210 behavior:
bootloader_entry: gpio  # Default; requires boot_switch.
boot_switch: zBSL

# MGM21 dual boards:
bootloader_entry: ezsp  # Must omit boot_switch.
```

The native `efr32_flasher.enter_bootloader` action is shared by the manual button
and the flash workflow. A normal flash downloads/validates its manifest metadata
and opens the image, then obtains the bootloader before sending any XMODEM data.

For EZSP entry the component first checks at the bootloader rate for a Gecko
menu, allowing a bootloader entered through the physical button or an earlier
manual UI action. Otherwise it restores the saved application baud, resets and
synchronizes ASH, discovers/negotiates EZSP (v8 or newer), queries the installed
standalone bootloader, and sends `launchStandaloneBootloader(1)` / frame 0x008F.
It then changes to bootloader baud 115200 and requires an actual Gecko menu.
A command acknowledgment alone does not authorize a transfer; a missing final
reply is acceptable only if the real bootloader menu is observed afterward.

The new transport validates ASH CRCs, matches response sequence/frame IDs,
acknowledges duplicate DATA frames without processing them twice, retries bounded
NAKs, handles raw XON/XOFF outside the escaped data, bounds receive memory, and
uses bounded receive timeouts. If the application does not respond or the menu
never appears, it closes the image connection, restores application UART/reset
state, reports failure and releases its serial ownership without programming.

`efr32_flasher` keeps the existing GPIO path as the default. Its new code is
maintained in [esphome-components PR #7](https://github.com/tube0013/esphome-components/pull/7)
and imported from GitHub through the shared `tubeszb_components_ref`. The separate
user-owned component checkout and its uncommitted work are unchanged.

## Shared controls and radio separation

The parameterized `older-efr32-radio.yaml` binds `uart_bus_zb` and bridge `zb`.
`uart_bus_zw` remains exclusively bound to the Z-Wave proxy. Busy callbacks pause
only Zigbee and preserve a pre-existing manual pause. Native flash completion
applies and saves the manifest application baud before releasing the busy sensor.
Startup identity probing remains local and independent of internet checks.

Manual entry keeps Zigbee paused for an external flasher. A failed manual entry
restores the prior bridge state. Reset restores the saved application baud and
exits manual mode. Internet requests and radio downloads honor offline mode and
network-trial guards. The experimental entry point now selects its own gateway
OTA/beta feeds, preserving its experimental ESPHome name on future updates.

Both profiles use the existing `efr32manifest_mgm210_poe.json` and its MGM210 2022
7.4.3 software-flow image, default baud 115200. They do not use the original
MGM210 2021 image or MG24 images. The owner published that feed; its HTTP 200 response,
7.4.3 version and 115200 baud metadata were verified before opening the draft PRs.

Both radios still share the ESP32 main loop. The Zigbee flasher does not retune
or reset Z-Wave, but synchronous radio maintenance can delay Z-Wave processing;
the existing confirmation explains this. Network changes, gateway OTA and reboot
continue to affect the shared device.

## Recovery and verification

Software entry requires a responsive EZSP application. For an unresponsive or
non-EZSP image, hold the physical boot button while resetting the Zigbee radio,
release it, then flash again. The native flasher recognizes the Gecko menu at
115200. This hardware cannot automatically assert that recovery button.

Native tests exercise real ASH wire encoding/decoding and CRC, negotiation,
launch parameters, unavailable bootloaders, rejection/timeouts, corrupted frames,
duplicates, NAK retries, oversized frames and XON/XOFF interleaving. Configuration
checks cover UART/GPIO/source separation across all six dual entry points.
Browser checks cover MGM21 flashing/controls alongside MG24 and CC2652, separate
Z-Wave state/baud, offline mode, retained logs and mobile layouts. The existing
GPIO profiles are also built as regressions. These checks do not flash hardware.

Protocol references:

- [Silicon Labs EZSP bootloading](https://docs.silabs.com/zigbee/9.0.0/emberznet-serial-protocol-uart-host-interfacing-guide/06-bootloading)
- [Silicon Labs bootloader frames](https://docs.silabs.com/zigbee/latest/sisdk-ezsp-reference-guide/13-bootloader-frames)
- [Normal bootloader mode value](https://docs.silabs.com/gecko-platform/4.5.1/platform-service/alone-bootload)
