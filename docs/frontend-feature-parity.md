# Frontend rollout and feature parity

The branded frontend is enabled for all 22 hardware manifests and all 22
compatibility entry points. The private MGM24 BP wrapper inherits it too.
The 20 existing profiles now include the MGM210 PoE 2022/2023 interfaces, and two
retired Silicon Labs profiles have been added from their archived configurations.
No device was flashed or public release published by this rollout task.

Native MG24/BM24 and CC2652 flashers accept per-image `baudrate` metadata,
apply it after successful flashing before bridge resume, and save it for boot.
The older MGM12/original MGM210 and MGM210 PoE 2022/2023 profiles now use the same
native EFR32 updater with software-flow UARTs and their own flat radio manifests.
See `older-efr32-flashing.md` and `zigbee-manifest-baudrate.md`.

## Common interface

All profiles have the same layout, Overview, Network, Firmware and Diagnostics;
Zigbee profiles also have their radio section, and dual-radio devices have separate
Zigbee/Z-Wave status and sections. Common functions include DHCP/static-IP trial
and rollback, persistent Use without internet restored before online checks,
gateway firmware checks, local OTA upload/progress, restart confirmation,
bottom-of-section logs retained across navigation and downloadable as text, and
the original ESPHome interface at `/esphome`.

Features shown on a radio page follow its configured native capabilities. Missing
radio features below are assessment items; they have not been implemented just to
make the controls look identical. Raw GPIO controls remain available in the
original ESPHome interface where they existed previously.

## Current capability matrix

| Profile family | Count | Radio identity/startup probe | Radio firmware update | Manual bootloader button in branded UI | Baud selection | Firmware beta switches |
| --- | ---: | --- | --- | --- | --- | --- |
| MGM24 standalone/dual | 3 | Firmware, IEEE, board/manufacturer, local startup probe | Stable/beta + direct custom `.gbl` | Yes | Saved selection | Gateway + radio |
| MGM210 PoE 2022/2023 | 2 | Native EZSP identity + local startup probe | Model-specific stable 2022 image + direct `.gbl` | Yes | Saved selection + manifest rate | Gateway; radio stable only |
| MGM21 dual | 2 | Native EZSP identity + local startup probe | Model-specific stable 2022 image + direct `.gbl` | Yes, through native EZSP software entry; physical recovery button | Saved selection + manifest rate | Gateway; radio stable only |
| MGM12 Pro1 / original MGM210 Series 2 | 2 | Native EZSP identity + local startup probe | Separate model-specific stable images + direct `.gbl` | Yes | 115200 default; saved selection + manifest rate | Gateway; radio stable only |
| CC2652 standalone/dual | 9 | Variant, firmware, IEEE and independent local startup probe | Stable/beta HEX; custom URL remains deferred | Yes | Manifest-controlled saved application rate; dual's legacy selector remains available | Gateway + radio |
| Z-Wave-only | 4 | Native proxy client/Home ID and actual UART baud | Gateway firmware; radio firmware remains with Z-Wave host tools | Not applicable to this UI | Existing fixed rate | Not configured |

The remaining gaps to assess are:

1. **Gateway beta switches on Z-Wave profiles.** All six older Silicon Labs
   standalone/dual profiles now have saved gateway beta switches. Their radio
   feeds have stable images only; the UI hides the unavailable radio beta option.
2. **MGM21 dual hardware recovery.** Native EZSP entry now enables web flashing
   without asserting PA0. GPIO36 remains an input; an unresponsive radio still
   requires the physical boot button. See `mgm21-dual-flashing.md`.
3. **CC2652 custom URL and manual baud controls.** Custom URLs remain deferred.
   Dual manifests retain their legacy selector; standalone profiles have no
   manual selector. Both persist manifest-driven application baud.

The four standalone older-radio profiles are ready for physical testing. Their
own pins and two-wire/software-flow configuration are used; MG24 RTS/CTS and
MG24 firmware variants are never applied to these models.

## Archived hardware coverage

The source configurations were read from
`tube_gateways/models/retired/`; the baud-rate follow-up also adds model-specific radio manifests there.

| Archived model/config | Rollout profile | Preserved mapping |
| --- | --- | --- |
| `tubeszb-efr32-MGM112-pro1/.../tubeszb-efr32-mgm12-pro1.yaml` | `manifests/tubeszb-efr32-mgm12-pro1.yaml` | WT32-ETH01; UART RX17/TX5; reset33/boot32 |
| `tubeszb-efr32-MGM210_Series_2/.../tubeszb-efr32-mgm210-series-2.yaml` | `manifests/tubeszb-efr32-mgm210-series-2.yaml` | WT32-ETH01; UART RX17/TX5; reset33/boot32 |
| `tubeszb-cc2652-eth/.../tubezb-cc2652p2-eth.yaml` | Existing CC2652P2 Ethernet/USB 2021 hardware profile | WT32-ETH01; UART RX5/TX17; reset33/boot32 |
| `tubeszb-cc2652-poe/.../tubeszb-cc2652-poe{,-idf}.yaml` | Existing CC2652P2 PoE 2021 hardware profile | Olimex PoE; UART RX5/TX16; reset33/boot32 |

The two new Silicon Labs manifests preserve their original ESPHome names,
LAN8720 MDC23/MDIO18, external GPIO0 clock, PHY address 1, power GPIO16, active-low
GPIO33 reset and GPIO32/PA2 boot, port 6638 and a default 115200 UART baud. Their
archived `_tubeszb._tcp` discovery service is retained alongside the current
Zigbee coordinator advertisement. Their native updater, startup probe, saved baud selector and manual bootloader
controls now use that existing serial bridge and the original reset/boot pins.

**The archived Silicon Labs firmware used Arduino.** These are ESP-IDF migration
builds because the common frontend/network components use ESP-IDF. They compile,
but physical migration/OTA partition compatibility and normal radio traffic must
be verified before treating them as release-tested hardware. Their archives do
not contain this UI's persistent network record; an initial migration uses DHCP
unless that record has already been configured. A factory/USB installation is the
recovery path if an installed legacy OTA layout cannot accept the new image.
Their new automatic update feeds are created when the normal release workflow
publishes these newly added manifests; they are not published by this task.

## Verification and outputs

`tests/test_frontend_rollout.py` verifies frontend coverage for every hardware and
compatibility entry, the distinct 2022/2023 PoE wiring, and independently recorded
archived WT32-ETH01 mappings. `tests/older_silabs_ui_test.cjs` exercises all four
older Silicon Labs profiles: identity, confirmed reset, serial controls, saved baud capabilities, model-specific radio flashing and hidden unavailable beta controls, gateway OTA
availability, offline mode, retained logs and mobile layouts.

The existing network, native policy, radio/OTA, dual-radio and standalone UI checks
remain in place. Build outputs and per-profile verification reports are in
`build-artifacts/all-device-frontend/`; the rollout index links to each OTA and
factory image. All profiles are built locally with ESPHome 2026.10.1. Compiled
frontends and native UART/control bindings are checked against the tested source.
