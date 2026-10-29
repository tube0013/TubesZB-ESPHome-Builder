# Older MGM12 and MGM210 radio flashing

The four standalone profiles now expose native radio flashing on the Firmware
page, linked from Zigbee. The UI includes top-banner progress and completion,
confirmation before flashing, manual bootloader control, direct custom `.gbl`
URLs, a saved baud selector, and the existing shared log dropdown. Startup radio
identity probing runs locally even with Use without internet enabled.

| Gateway | Feed | Radio image | UART TX/RX | Boot/reset GPIO |
| --- | --- | --- | --- | --- |
| MGM12 Pro1 | `efr32manifest_mgm12.json` | MGM12 2021 7.4.3 | 5 / 17 | 32 / 33 |
| Original MGM210 Series 2 | `efr32manifest_mgm210.json` | MGM21 2021 7.4.3 | 5 / 17 | 32 / 33 |
| MGM210 PoE 2022 | `efr32manifest_mgm210_poe.json` | MGM21 2022 7.4.3 | 16 / 5 | 4 / 13 |
| MGM210 PoE 2023 | `efr32manifest_mgm210_poe.json` | MGM21 2022 7.4.3 | 4 / 36 | 16 / 5 |

All application and bootloader rates default to 115200. These are two-wire UARTs
using software flow control. The flasher preserves configured TX/RX pins during
baud changes and disables hardware flow control. MG24 retains its existing
hardware RTS/CTS configuration. Each older profile uses `variant: flat`; a variant
catalog is rejected rather than falling back to an MG24 image.

The new PoE feed references the distinct 2022 NCP image already archived under
`models/current/tubeszb-efr32-MGM210-poe/firmware/`. Its MD5 and size were checked
against the local GBL. The original Series 2 continues to use its separate 2021
image. No radio beta feed exists for these profiles: the UI shows stable radio
updates and hides the radio beta checkbox. Gateway beta selection is available
and restored before Ethernet-triggered checks.

The shared `packages/older-efr32-radio.yaml` owns serial pause/resume through the
native busy sensor. It records whether the bridge was already paused and resumes
only when it owned the pause. The identity reader uses the same owner, so it
cannot resume an existing manual pause. Manifest baud changes are applied and
saved before busy is released. The selector callback cannot resume the bridge
while the native updater owns it.

Manual bootloader mode pauses the bridge and calls the native
`efr32_flasher.enter_bootloader` action. On these standalone boards it asserts
the configured boot pin before pulsing reset. The bridge stays paused for an external flasher; Reset
Zigbee radio exits that mode and restores its prior running/paused state.

Internet-dependent radio checks and flashing are gated in the shared template
buttons as well as the branded UI. They are also blocked during a network trial.
Identity probing is independent of internet access. Direct GBL files carry no
manifest baud metadata and retain the current application rate.

## Hardware verification

On 2026-10-28 the owner confirmed BM24 7.4.4.0 / 115200 → 9.1.2.1 / 460800 and
the reverse transition to 7.4.4.0. The supplied upgrade log shows matching MD5,
UART retuning, preferences committed with zero failures, NCP startup and then
serial bridge resume. Power-cycle persistence was not separately reported.

The new older-radio profiles have compilation and browser/native regression
coverage and are ready for the owner's device tests. The archived MGM12 7.4.3
image directory contains an `UNTESTED` marker; successful hardware flashing and
normal Zigbee traffic still need checking on that device.

The two retired WT32-ETH01 gateway profiles are Arduino-to-ESP-IDF migration
builds. Initial OTA compatibility depends on the installed partition layout;
the factory binaries are also supplied for USB recovery/install. The two MGM21
dual-radio profiles now use native EZSP bootloader entry; see
`mgm21-dual-flashing.md`. Their GPIO36 boot signal remains an input and the
physical button is retained for recovery.

Publish the new `docs/firmware/efr32manifest_mgm210_poe.json` in `tube_gateways`
before testing PoE radio checks/downloads. Existing original-Series-2 and MGM12
feeds are reused. No gateway or radio image was flashed by this implementation.
