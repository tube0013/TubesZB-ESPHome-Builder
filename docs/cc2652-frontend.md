# CC2652 frontend

The branded frontend now covers all seven existing single-radio CC2652 models:
CC2652P2 PoE 2021/2022/2023, CC2652P2 Ethernet/USB 2021/2022/2023, and CC2652P7
PoE 2023. Both existing CC2652 dual-radio manifests also use the combined frontend and the
same UART ownership package; see [dual-radio frontend](dual-radio-frontend.md).

The test image targets `tubeszb-cc2652p2-poe-2021`. Hostnames, ESP32 configuration,
Ethernet and UART pins, reset/boot GPIO definitions, and flash baud settings are
preserved. The shared package is `packages/cc2652-web.yaml`.

## Controls

- Overview shows the actual serial TCP client, active UART baud, gateway identity,
  and `tcp://<IPv4>:6638` connection address.
- Zigbee shows native firmware code revision, IEEE address and detected CC2652
  variant. Detect variant and reset actions have confirmations.
- **Trigger Bootloader Manually** is in the Firmware page's Zigbee radio card
  and invokes the existing `fw_update_mode` script after confirmation. Reset returns
  the target to normal operation after an external flashing session.
- Firmware has independent persistent gateway and Zigbee Stable/Beta switches.
  A channel change does not install anything and invalidates the checked source.
- Radio installation requires a successful check of the selected source, followed
  by explicit confirmation. Failed checks leave installation disabled.
- Radio progress comes from the existing CC2652 flasher's native log messages.
  A percentage of 100 or a transfer-complete line cannot confirm success; the
  native `Update finished OK. Resetting target` result is required.
- Native sensor callbacks and a mutex-protected snapshot keep identity/progress
  available to the HTTP task during a blocking radio operation. Completion is
  sampled after the native main-loop operation returns.
- CC2652 flashing pauses the Zigbee serial bridge through the shared device-side
  script used by both frontends. It restores the bridge after success or failure,
  while preserving an existing manual pause. Manual pause/resume controls remain available.
- CC2652 custom firmware URLs and editable baud settings are not exposed in this
  pass. The EFR32-only `.gbl` field is hidden and unsupported API requests reject.

The shared gateway OTA/upload progress banner, network validation and rollback,
log viewer/downloads, original ESPHome interface, logo and appearance controls
are retained. The shared Device logs dropdown appears in every section except
Overview and keeps one capture stream and its buffer across section changes and
collapsing; see [log lifecycle and downloads](zwave-frontend.md#logs-and-downloads).
Network changes and competing firmware actions are rejected while
tracked radio maintenance or gateway OTA is active. Automatic YAML update checks
are deferred during a network trial.

## Startup radio identity

All nine CC2652 manifests run the shared `probe_cc2652_on_boot` script once at
startup, independently of Ethernet, internet policy and firmware-update checks.
It pauses the Zigbee serial bridge before the native ROM variant probe and keeps
it paused through the native startup ZNP firmware/IEEE reads. It restores the
bridge's prior paused/running state even if detection fails or times out. On
dual-radio devices only the Zigbee bridge is paused.

The native component's separate `detect_on_boot` stays false to prevent duplicate
uncoordinated probes. Firmware-check scripts wait for this local startup sequence;
they do not initiate radio discovery. The frontend reports the startup sequence
as busy until UART readback and bridge restoration finish. The manual Detect
CC2652 variant button remains available afterward. Use without internet suppresses
remote checks and downloads while retaining local startup radio identity.

## Firmware sources

The newer local release branch has the persistent beta switches and nonblocking
connectivity probe. Those changes are included for the single-radio CC2652
configurations, following the MGM24 integration.

The planned stable and beta GitHub Pages CC2652 manifest URLs returned HTTP 404
when checked. Their files currently exist only as untracked local files in the
separate `tube_gateways/docs/firmware` checkout. No files in that checkout were
changed or published by this task.

The stable channel therefore retains the existing working manifest:
`https://raw.githubusercontent.com/tube0013/tube_gateways/refs/heads/main/misc/manifest.json`.
It was fetched successfully and contains P2/P7 HEX image entries and version
`20250321`.

The beta switch retains the planned release-branch URL:
`https://tube0013.github.io/tube_gateways/firmware/cc2652manifest-beta.json`.
Until that feed is published, checking the beta radio channel fails and flashing
stays disabled. There is no silent fallback to stable firmware for a beta check.

## Validation

```sh
python -m pytest -q tests/test_network_settings.py tests/test_tubeszb_web_assets.py
python tests/check_configs.py
NODE_PATH=/tmp/tzb-browser-tests/node_modules node tests/cc2652_web_ui_test.cjs
esphome compile manifests/tubeszb-cc2652p2-poe-2021.yaml
esphome compile manifests/tubeszb-cc2652p7-poe-2023.yaml
```

The browser test uses the real embedded page against mock endpoints. It covers
identity, TCP address, independent channels, failed checks, radio confirmation,
real-percent presentation without invented success, failed-flash retry,
variant detection, bootloader/reset, explicit serial controls and mobile layouts.
Host tests exercise the native CC2652 log decoder. Existing MGM24 and Z-Wave
browser tests and firmware compiles check the shared frontend regression surface.
Hardware radio detection/flashing still requires the user's device test.

## Radio flash banner

Radio flash preparation, percentage and progress appear in the top status banner
for both CC2652 and EFR32. After the native flasher reports completion, the banner
shows **Firmware flash complete.** and 100% for five seconds, then clears. The
result remains in the radio firmware card. Polling does not restart the timeout
or redisplay an old completion. Tab navigation preserves the remaining timeout.
Failures retain a separate error notice.
