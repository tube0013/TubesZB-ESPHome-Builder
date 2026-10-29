# TubesZB Z-Wave frontend

The four Z-Wave manifests include `packages/zwave-web.yaml`. All hardware
manifests now include the branded frontend with their own radio-capability
bindings; see [rollout coverage](frontend-feature-parity.md). The frontend uses the approved mockup's layout and palette,
and the flask/radio symbol from the user-supplied `tubeszb_new.svg`. The original
artwork is preserved as `components/tubeszb_web/tubeszb-logo-original.svg`; the
header symbol contains its original two paths, cropped and set to inherit color.

## Routes and integration

| Route | Purpose |
| --- | --- |
| `/` | Default branded page, with Overview, Network, Firmware and Diagnostics |
| `/esphome` | Original ESPHome index and JavaScript frontend |
| `/network` | Existing standalone network settings page |
| `/network/*` | Existing network transaction, validation and rollback API |
| `/tubeszb/status` | Device, proxy client and update status |
| `/tubeszb/restart` | Queue a restart on the main loop |
| `/tubeszb/check-update` | Check the configured release manifest |
| `/tubeszb/install-update` | Install an available release through ESPHome |
| `/events` | Existing ESPHome event stream, connected only for log capture |
| `/update` | Existing ESPHome web OTA upload handler |

The component registers at priority 249.5, before the ESPHome web component at
249. Its root handler therefore serves the branded page without patching core
ESPHome. Requests to `/esphome` invoke the existing index handler; absolute URLs
for `/0.js`, entity APIs, events and uploads continue to work.

All markup, scripts, styles, logo paths and selected icons are embedded in one
gzip-compressed page. No CDN, fonts, Internet connection, extra HTTP listener or
second port is required. The approved layout is now fed by real device status;
no mock values, simulated actions or ChatGPT visualization APIs ship in firmware.
Proxy connection means a client has subscribed to Z-Wave proxy traffic; it does
not infer a healthy Z-Wave mesh from an unrelated API connection.

Device and update state are sampled on the ESPHome main loop, copied under a
mutex, and served by the IDF HTTP task. Commands are reserved atomically and
handed back to the main loop before invoking ESPHome's update or reboot methods.
Actions respect configured HTTP authentication, require a per-boot token, and
reject a different browser Origin. Checks/installations are blocked while a
network trial is unconfirmed. Restarting during a trial intentionally invokes the
existing network rollback behavior, and the confirmation dialog explains this.

Status requests run every ten seconds during normal use and every two seconds
on Firmware or during a network trial. They pause when the browser tab is hidden.
The overview does not open a persistent event stream.

The manual upload form accepts `.ota.bin` files and directs factory images to USB
installation. It uploads through the existing `/update` handler after the user
reviews the device model and filename. HTTP success is checked against ESPHome's
OTA success message; an interrupted response is reported as unconfirmed, rather
than assumed successful. ESPHome's OTA backend remains responsible for validating
the firmware image. The filename alone cannot verify hardware compatibility.

## Logs and downloads

The shared **Device logs** dropdown appears below the content of every section except
Overview. Opening it starts capture from `/events`. The page uses one EventSource
connection across all sections; collapsing the dropdown or moving between
sections keeps capture running, including while Overview hides the panel. Its
open/collapsed state and captured text are retained during section navigation.
Received events are stored in browser memory, with browser UTC timestamps and
ANSI color codes removed. Log content is rendered as text, never HTML.

- **Stop capture** closes the event stream and keeps the captured history.
- **Start capture** reconnects without clearing retained history. Ordinary connection
  interruptions retry automatically and show a reconnecting state.
- Gateway file uploads, gateway restarts, and leaving/reloading the browser page
  close the stream. Upload/restart notices explain why capture stopped; retained
  text can still be downloaded before reloading. Capture stays available during
  radio maintenance.
- **Download logs** exports retained history as a UTF-8 `.txt` file, including the
  device/model, firmware versions, capture/export timestamps and discarded-line count.
- **Clear** removes captured history. Closing/reloading the page also loses it.
- Retention is bounded to 10,000 lines or 1,048,576 JavaScript string units,
  whichever is reached first. Only the latest 250 lines are rendered. Discarded
  older lines are counted and disclosed in the UI and export.

There is no flash-based log history and no extra device RAM ring buffer. The
download contains what this browser received while capture was active, subject
to ESPHome's normal event-stream message limits. A new download can be made while
capture continues. Logs do not include boot history from before capture began.

## Verification

```sh
python -m pytest -q tests/test_network_settings.py tests/test_tubeszb_web_assets.py
python tests/check_configs.py
NODE_PATH=/tmp/tzb-browser-tests/node_modules node tests/tubeszb_web_ui_test.cjs
NODE_PATH=/tmp/tzb-browser-tests/node_modules node tests/log_panel_ui_test.cjs
esphome compile manifests/tubeszb-2026-zw.yaml
```

The browser test uses the actual embedded page against mock device endpoints. It
checks real API URLs and request tokens, validation and confirmation, update
checks, SSE lifecycle, log retention and download contents, text-only log rendering,
manual OTA handling, the original-interface link, dark appearance and mobile layout.
The shared log suite checks all three frontend radio types, navigation/collapse
with a single stream, stop/resume, reconnection, downloads, mobile layout and
page-close cleanup. Asset tests check offline packaging and supplied branding; firmware compilation
checks integration with ESPHome's real C++ APIs.

Physical testing for this iteration should cover root-page loading, `/esphome`
including its scripts, actual proxy client connect/disconnect state, live log
capture/download, manual OTA, release updates, authentication when configured,
and the network confirmation/rollback flow through the new form. Existing network
storage keys and boot transition behavior are unchanged.

Lucide/Feather icon notices are included in `LICENSE-icons.txt` and in the embedded
page source. The TubesZB symbol is the supplied brand artwork.

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
