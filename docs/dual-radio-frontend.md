# Dual-radio frontend

The same embedded frontend now covers all six dual-radio entry points:

| Hardware | Standard manifest | Alternate entry point |
| --- | --- | --- |
| CC2652 + Z-Wave | `tubeszb-dual-radio-kit-cc2652.yaml` | `tubeszb-dr-cc2652-exp.yaml` |
| MGM24 + Z-Wave | `tubeszb-dual-radio-kit-mgm24.yaml` | `tubeszb-dr-mgm24-exp.yaml` |
| MGM21 + Z-Wave | `tubeszb-dual-radio-mgm21-2022.yaml` | `tubeszb-dr-mgm21-22-exp.yaml` |

Overview combines independent Zigbee and Z-Wave cards. Zigbee uses its existing
TCP serial connection on port 6638. Z-Wave uses the existing ESPHome API proxy,
including its actual subscribed client, Home ID and separate UART baud. The
Zigbee page retains the standalone radio controls; the Z-Wave page shows proxy
connection details. Network, Firmware and Diagnostics remain shared device pages.

Logs stay at the bottom of each section except Overview. One EventSource and one
browser buffer survive navigation between both radio pages and the shared pages.
The same static-IP confirmation/rollback, local OTA upload, top progress banner,
short completion timeout, offline switch, release reminder and original
`/esphome` interface are retained.

## Radio capabilities

CC2652 and MGM24 retain independent persistent gateway/radio beta channels,
firmware checks and confirmed flashing, native progress/results, local identity
probe and reset, serial pause/resume and manual bootloader controls. MGM24 also
retains saved baud selection and direct custom `.gbl` URLs. CC2652 custom URLs and
baud editing remain deferred as for its standalone frontend. MGM24 flasher runtime
baud follows the selected native UART baud, matching the standalone configuration.

Both CC2652 and MGM24 startup identity probes remain independent of internet
checks. CC2652 uses the same shared pause/probe/readback/restore script with stream
`zb`, and its flash button uses the established pause/flash/readback/restore path.
MGM24 keeps its existing boot-probe/reset/flow-control and busy pause/resume paths.

MGM21 now has native identity/startup probing, stable radio flashing, direct GBL
URLs, saved baud, reset and manual bootloader controls. It uses software
bootloader entry through `efr32_flasher` because its PA0 connection is ESP32
GPIO36, an input-only pin. The physical button remains the recovery path. Both
profiles use the distinct MGM210 2022 image/feed, expose saved gateway beta
selection, and hide the unavailable radio beta channel. See
`mgm21-dual-flashing.md` for wiring, protocol and recovery details.

## Bindings and checks

Each frontend explicitly binds `uart_bus_zb` for Zigbee and `uart_bus_zw` for
Z-Wave. Validation rejects a Z-Wave binding that disagrees with its proxy, a
Zigbee binding that disagrees with its bridge/flasher, or both radios using the
same UART. Separate snapshot fields keep the Zigbee TCP connection from
overwriting the Z-Wave client or baud. Existing UART/GPIO/Ethernet definitions
are preserved; the UI calls native buttons/scripts.

Online checks and radio downloads honor **Use without internet**, including
startup, reconnect, weekly, legacy button and direct update-entity paths. Changing
a beta channel only selects a source and requires checking before installation.
The dual gateway sources use their own dual-radio OTA manifests. The CC2652
stable radio feed remains the existing raw GitHub manifest; beta uses the same
planned feed as the standalone models. An unavailable feed fails checking and
leaves flashing disabled.

The radios share an ESP32. Pausing the Zigbee stream targets only `zb`, but native
radio flashing can block the main loop and delay Z-Wave processing. The dual
flash confirmation discloses that Z-Wave traffic may also pause. The separate
connection display does not promise uninterrupted Z-Wave service during radio
maintenance; snapshots can also remain at their last sampled values while the
main loop is blocked. Gateway OTA/restart interrupts both radios.

## Verification

`tests/test_dual_radio_configs.py` checks all six models' UART separation,
preserved pins, native control bindings, correct OTA channels and startup probes.
`tests/dual_radio_ui_test.cjs` serves the actual embedded page with mock endpoints
and tests mixed connection states, distinct bauds/Home ID, controls/channels,
confirmed radio flashing/progress, MGM21 software-entry controls, offline mode,
local OTA availability, support details, retained single-stream logs, and all
sections at 320/390 pixels. Existing standalone suites check shared UI behavior.

All hardware and compatibility configurations and the native/asset/policy/config tests are validated.
All six dual-radio images and representative standalone Z-Wave, CC2652 and MGM24
images compile with ESPHome 2026.10.1. These checks do not flash physical hardware.
Physical testing should verify simultaneous radio clients, Zigbee maintenance and
reconnection, startup identity with offline mode, beta persistence, actual flash
completion and shared network/OTA/log behavior on each hardware model.
