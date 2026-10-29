# MGM24 frontend and the local BP image

The tracked `tubeszb-efr32-mgm24-2023` configuration now uses the shared TubesZB
frontend with optional EFR32 bindings. The ignored local entry point
`manifests/tubeszb-mgm24-bp.yaml` includes that configuration and overrides only
the ESPHome name (`tubeszb-mgm24-bp`) and display model (`TubesZB MGM24 BP`). It
remains ignored and is not part of the published release matrix.

The original BP manifests were located in the local `TubesZB-ESPHome-Builder-gh-pages`
worktree. The user confirmed BP is a personal device name, with the same hardware
as the MGM24 model. Verified mappings: `esp-wrover-kit`, UART RX GPIO36 / TX GPIO4,
RTS GPIO2 / CTS GPIO13, reset GPIO5, bootloader GPIO16, TCP port 6638. No radio
variant is forced: the existing EFR32 flasher retains its `variant: auto` behavior.

The standard MGM24 YAML also carries forward the inline beta switches and current
GitHub Pages firmware feed paths from local release branch `2026.10.25.1`
(`0770533`), plus its nonblocking connectivity probe. Other model manifests are
not rebased or changed by this port.

## Radio and firmware controls

- **Overview** reports the actual TCP serial client, active baud and gateway identity.
- **Zigbee** reports radio board, manufacturer, IEEE address, installed version and
  probe status; it provides probe/reset, pause/resume and restored baud selection.
- **Firmware** separates ESPHome gateway OTA from EFR32 radio flashing. Each has
  its own Stable/Beta switch. Changing a switch never installs firmware.
- Custom radio firmware URLs accept direct HTTP(S) `.gbl` files only. Queries and
  fragments are allowed, and the extension is case insensitive. JSON manifests,
  `.bin` and `.hex` files are rejected by the browser and device handler. They
  retain the existing override behavior. Switching a
  channel leaves an explicit custom URL in effect; clearing the URL restores the
  currently selected radio channel.
- A channel or URL change invalidates the frontend's checked-source state. The
  user must check the selected release channel before installing from it. A
  direct custom `.gbl` URL can be flashed after an explicit review dialog. Release
  checks are disabled while a custom URL is active because it has no manifest version.
- Radio flashing always has a separate confirmation from gateway OTA. Flashing
  progress and successful-transfer status come from the existing flasher's log
  events, not from a simulated progress timer or a lost connection. Installed
  radio identity can then be refreshed with **Probe radio info**.

The bindings invoke existing buttons, switches, select and text entities on the
main loop. Baud changes therefore retain the existing pause/reconfigure/restore
flow-control/resume automation. No web handler writes UART registers or flips
bootloader/reset GPIOs directly.

Busy and text-sensor callbacks keep the radio snapshot available to the IDF HTTP
task during long flasher/probe work. The HTTP task does not hold the snapshot
mutex while sending data. Competing frontend mutations are blocked while a radio
operation or gateway installation is active. The network component also checks
this guard before accepting mutations.

During a network trial, the automatic boot probe waits for confirmation and
automatic radio manifest checks are deferred. This avoids long radio maintenance
work obstructing the two-minute network recovery window. Manual radio actions and
firmware updates are blocked until the trial is kept or reverted.

The original gateway firmware manifests remain configured for the public MGM24
release name, as in the original BP file. Installing a published public image may
replace the personal hostname; use the local BP image when that identity must be
retained. This task does not publish or change those remote release assets.

## Legacy frontend fixes

The original UI remains at `/esphome` and `/esphome/`. Its minified bundle now:

1. Uses root `/events` and REST paths at either alias.
2. Translates ESPHome 2026.10 `state_detail_all` events for the bundle's existing
   entity renderer and supplies the domain from the entity ID when needed.
3. Uses a valid static Lit SVG template for the pre-existing logo render function.
4. Links back to the TubesZB frontend when opened at the legacy alias, while
   preserving the Network settings link for models still using the original UI at `/`.

The legacy browser test serves the actual minified bundle and a native-shaped
index, sorting-group events, full entity events and REST actions. It checks visible
entities, a working button, both alias paths and the original root behavior.

## Verification and bench testing

```sh
python -m pytest -q tests/test_network_settings.py tests/test_tubeszb_web_assets.py
python tests/check_configs.py
NODE_PATH=/tmp/tzb-browser-tests/node_modules node tests/mgm24_web_ui_test.cjs
NODE_PATH=/tmp/tzb-browser-tests/node_modules node tests/legacy_web_ui_test.cjs
esphome compile manifests/tubeszb-mgm24-bp.yaml
```

The local checker includes the ignored BP file; CI uses only tracked manifests
and compiles the standard MGM24 configuration. Browser tests use mock hardware
endpoints and exercise independent channel switches, stale-source protection,
custom-source precedence, flash confirmation/progress, probe, serial and baud
controls, and 320/390-pixel layouts. The Z-Wave browser suite remains a regression
check for the shared page. These tests do not flash a real EFR32 radio.

On the physical BP device, check persisted beta switches after restart, radio
identity/probe and active baud, Zigbee client traffic, channel-specific manifest
checks, radio flashing, retained network settings, and full entities/controls at
`/esphome`. The shared Device logs dropdown is available in every section except Overview.
Capture and retained text survive section navigation and collapsing the panel;
see [log lifecycle and downloads](zwave-frontend.md#logs-and-downloads).

## HTTP responses and firmware layout

Queued frontend actions return HTTP 200 with `ok: true`; completion is reported by
the status endpoint. The ESPHome 2026.10 IDF server maps unsupported status codes
(including 202, 403, 405 and 503) to 500, so handlers use its supported statuses.
Channel changes also write an INFO message naming the gateway or Zigbee channel.
The gateway OTA upload card sits immediately after TubesZB firmware, before the
Zigbee radio firmware card. Diagnostics labels ESPHome as an alternate interface.

## Gateway OTA progress

Gateway updates show progress from ESPHome's native OTA state callbacks. The
callback updates a mutex-protected HTTP snapshot even while the HTTP downloader
blocks the main loop. The page polls once per second during installation, shows
real percentages, and waits for OTA completion before reporting verification.
An interrupted connection preserves the last percentage and reports completion
as unconfirmed; a detected reboot reports the gateway's installed version.

Manual `.ota.bin` uploads use browser upload events for their own progress bar.
100% sent means the browser has finished uploading; the page then waits for the
existing `/update` success response before reporting firmware verification.
Errors and timeouts do not imply successful installation. Network changes and
other frontend mutations are blocked while native gateway OTA is active.

The upload/installation percentage and progress bar appear together in the top status banner, above the firmware cards.

## Radio flash banner

Radio flash preparation, percentage and progress appear in the top status banner
for both CC2652 and EFR32. After the native flasher reports completion, the banner
shows **Firmware flash complete.** and 100% for five seconds, then clears. The
result remains in the radio firmware card. Polling does not restart the timeout
or redisplay an old completion. Tab navigation preserves the remaining timeout.
Failures retain a separate error notice.

## Manual MGM24/BM24 bootloader

**Trigger Bootloader Manually** is available in the Firmware page's radio card.
It binds the existing `Trigger MGM24 Module Bootloader` template button, which
runs `fw_update_mode`: assert `zBSL`, wait one second, assert `zRST_gpio`, wait one
second, release reset, wait five seconds, then release `zBSL`. GPIO16 and GPIO5
retain their existing active-low switch definitions.

The frontend waits for that native script to finish before reporting the manual
bootloader ready. Network and competing maintenance actions remain blocked while
it runs. The original ESPHome button runs the same sequence and its busy state
is reflected in the custom frontend. Triggering and resetting both require
confirmation in the custom frontend. This applies to MGM24 and BM24 identities
using the shared MGM24 configuration, including the local BP wrapper. Existing
UART baud selection remains under user control for external flashing.

Both MGM24 dual-radio entry points also use the combined frontend with independent
Zigbee/Z-Wave status and the same radio controls. See [dual-radio frontend](dual-radio-frontend.md).
