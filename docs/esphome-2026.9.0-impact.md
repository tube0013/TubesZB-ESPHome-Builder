# ESPHome 2026.9.0 — impact review for TubesZB firmware

Reviewed 2026-09-16 against `fix/check-connectivity-watchdog` @ f1e4af0 — the
current line (descends from `main`, carries the `ESPHOME_VERSION` pin).
Release notes: https://esphome.io/blog/2026/09/16/esphome-2026-9/

Validated with a clean venv running ESPHome **2026.9.0** (Python 3.12).

> Supersedes an earlier pass run against `beta_channel_switch`, which is a
> divergent 2026-05 line that does not contain `main` and uses `common/`
> packages this branch does not have. Conclusions that changed are marked.

## Verdict

No breaking changes hit these configs. `esphome config` and code generation are
clean on all three reviewed targets. One deprecation warning, from an external
component, now fixed upstream. Three changes are worth acting on, all upside.

| Target | radio bridge | `esphome config` | codegen |
|---|---|---|---|
| `manifests/tubeszb-efr32-mgm24-2023.yaml` | `stream_server` | valid | ok |
| `manifests/tubeszb-cc2652p7-poe-2023.yaml` | `stream_server` | valid | ok |
| `manifests/tubeszb-2026-zw.yaml` | native `zwave_proxy` | valid | ok |

Only pre-existing strapping-pin warnings (GPIO5 / GPIO12 / GPIO15).

Full C++ link was **not** completed — see "Compile status" at the bottom.

---

## 1. `network: tcp_send_buffer` — relevant to the two stream_server builds

New option (PR #18610, @bdraco). Maps to `CONFIG_LWIP_TCP_SND_BUF_DEFAULT`.

- Range 2880 - 65535 bytes, ESP32/ESP-IDF only, `cv.only_on_esp32`.
- ESP-IDF stock default is **5744 bytes** (about 4 x MSS).
- An explicit value wins over `enable_high_performance` (which sets 65534) —
  the assignment is deliberately placed after the high-performance block.
- Note the unit parsing: `16kB` validates to **16000**, not 16384.

### Why this matters here, not just for BT proxies

The release note frames this as a Bluetooth-proxy fix, but `stream_server` has
the same failure mode. In `stream_server.cpp`:

```cpp
bool is_retryable_socket_error(int err) {
    // ENOMEM/ENOBUFS: LwIP runs out of pbufs/segments transiently during bursts of
    // small TCP_NODELAY writes (e.g. an EZSP startup); the pool frees as ACKs arrive,
    // so treat as retryable instead of tearing down the client session.
    return err == EWOULDBLOCK || err == EAGAIN || err == EINTR || err == ENOMEM || err == ENOBUFS;
}
```

`flush()` does a single `writev()` per loop from the ring buffer. When lwIP's
per-socket send buffer is full that `writev()` returns -1/EWOULDBLOCK, the ring
buffer stops draining, and `read()` eventually logs:

```
Incoming bytes available, but outgoing buffer is full: stream will be corrupted!
```

…and drops pending bytes. That is a corrupted EZSP/ZNP frame, not just a slow
one. Raising the lwIP send buffer widens the window before the ring buffer
backs up.

The mgm24 build is the most exposed: `rx_buffer_size: 8192` and stream_server
`buffer_size: 8192`. The p7 build runs 1024/1024 at 115200.

### Applied on this branch (commit 76389a7) — still needs bench testing

**Changed from the earlier pass:** this branch has no `common/` packages, so the
block goes at top level in each manifest that uses `stream_server`
(`tubeszb-efr32-mgm24-2023.yaml`, `tubeszb-cc2652p7-poe-2023.yaml`, and the
other CC2652/EFR32 manifests), not in a shared include:

```yaml
network:
  tcp_send_buffer: 16kB   # 16000 bytes; ESP-IDF default is 5744
```

Applied to all 32 files (manifests + esphome-config) that declare
`stream_server`. Verified with esphome 2026.9.0: all 20 manifests validate, the
16 changed ones report `tcp_send_buffer: 16000`, and the generated sdkconfig for
`tubeszb-efr32-mgm24-2023` carries `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=16000`. The
option coexists with the existing `esp32: framework: advanced: sdkconfig_options`.

Heap cost scales with the value only while a connection has data in flight, and
these devices carry a single client on one socket, so 16k is a modest ask.
Start there on the mgm24, watch for the "outgoing buffer is full" log under a
network-map/backup load, and only go higher if it still appears. There is no
reason to reach for `enable_high_performance` — that also inflates TCP windows
and mailboxes.

**Not applied to the four Z-Wave builds.** `tubeszb-2026-zw`,
`tubeszb-2026-zw-experimental`, `tubeszb-zw` and `tubeszb-zw-experimental` all
use ESPHome's native `zwave_proxy:`, which tunnels frames over the API
connection rather than a raw TCP listener, so there is no `stream_server` socket
to widen. (The setting is global per-socket, so adding it there would still
enlarge the API socket's buffer — but at heap cost, for no stream_server
benefit.)

## 2. `zwave_proxy` now reports why a subscribe failed

2026.9.0 changes `ZWaveProxy::zwave_proxy_request()` from `void` to returning
`api::enums::ZWaveProxyStatus`:

- `ZWAVE_PROXY_STATUS_OK`
- `ZWAVE_PROXY_STATUS_IN_USE` — another API client already holds the proxy
- `ZWAVE_PROXY_STATUS_NOT_SUPPORTED`

Unsubscribe is now explicitly idempotent. Previously a second client asking to
subscribe while the first held it got silence; now it gets a reason. No YAML
change — it lands with the version bump. Worth remembering next time a Z-Wave
JS UI "connects but no frames" report comes in.

## 3. UART now shuts down cleanly on reboot — free win

`IDFUARTComponent::on_shutdown()` is new in 2026.9.0:

```cpp
uart_wait_tx_done(this->uart_num_, pdMS_TO_TICKS(100));
// Keep the peripheral quiet across a soft reset so ROM output does not reach
// the attached device (#15472)
uart_driver_delete(this->uart_num_);
```

ESP32 ROM boot chatter no longer reaches the radio's UART across a soft reset.
That is the class of problem the mgm24 `on_boot` reset sequence works around.
Worth checking on hardware whether that sequence can be trimmed — but the reset
pulse is also doing NCP-side work, so verify before removing anything.

`uart_num_` is now initialised to `UART_NUM_MAX` instead of being uninitialised.

## 4. IDF component exclusion list nearly doubled — checked, no impact

`DEFAULT_EXCLUDED_IDF_COMPONENTS` went from 28 entries (2026.8.2) to 54 (2026.9.0).
Newly excluded:

```
app_trace, bt, console, esp-tls, esp_coex, esp_driver_cam, esp_driver_gptimer,
esp_driver_i2c, esp_driver_ledc, esp_driver_sdio, esp_driver_sdm,
esp_driver_sdmmc, esp_driver_sdspi, esp_gdbstub, esp_hal_ieee802154,
esp_http_server, esp_phy, esp_wifi, ieee802154, json, nvs_sec_provider,
protobuf-c, rt, sdmmc, tcp_transport, wpa_supplicant
```

Audited every non-ESPHome include in `tube0013/esphome-components`:

- `esp_http_client.h`, `esp_crt_bundle.h` — already declared in each manifest's
  `include_builtin_idf_components`. Still correct.
- `driver/uart.h` (stream_server, and the mgm24 `on_boot` / `apply_uart_baud`
  lambdas) — lives in `esp_driver_uart` in IDF 5.5.5, **not** in the excluded
  `driver` shim. Safe.
- `gpio_reset_pin` / `gpio_set_*` in the mgm24 lambda — `esp_driver_gpio`, not
  excluded. Safe.
- The flashers parse manifests with **ArduinoJson**, not cJSON, so the new
  `json` exclusion is a non-issue.
- `esp-tls` / `tcp_transport` are pulled back automatically by `http_request`
  and `esp_http_client`.
- `console` is pulled back by `espressif/mdns`, which these configs use.
- `esp_wifi` / `wpa_supplicant` excluded is a straight flash/RAM saving — no
  manifest in this tree declares `wifi:`.

One thing the audit could **not** settle, and it is the reason a real link still
matters: the generated `CMakeLists.txt` passes `tcp_transport` in
`EXCLUDE_COMPONENTS` while keeping `esp_http_client`, whose IDF manifest declares
`PRIV_REQUIRES tcp_transport http_parser`. ESPHome's own comment on that entry
says esp_http_client pulls it back, and `http_request` is common enough that
2026.9.0 would not have shipped otherwise — but `__build_resolve_and_add_req()`
in IDF 5.5.5 raises a fatal error on an unregistered requirement, so this is
worth watching for in the first real build. `esp-tls`, `esp_http_client`,
`esp_crt_bundle`, `esp_driver_uart` and `esp_driver_gpio` are all correctly kept
out of the exclusion list.

## 5. Custom eFuse MAC now applies system-wide — check before mass rollout

New in `components/esp32/core.cpp`:

```cpp
extern "C" void app_main() {
  // Apply the custom eFuse MAC (if burned and valid) as the base MAC before any
  // interface (Wi-Fi, Ethernet, Bluetooth, 802.15.4) derives its address from it.
  uint8_t mac[MAC_ADDRESS_SIZE];
  if (get_custom_mac_address(mac)) {
    set_mac_address(mac);   // esp_base_mac_addr_set()
  }
```

Previously the custom eFuse MAC only affected what ESPHome *reported*. Now it
becomes the base MAC, so the **Ethernet interface MAC changes** on any unit with
a custom MAC burned in eFuse.

Consequences if that applies to any units:

- DHCP reservations keyed to the old MAC stop matching.
- Every manifest advertises `serial_number: esphome::get_mac_address()` in
  `mdns`, and exposes an `ethernet_info` MAC sensor — so the identity ZHA / Z2M /
  Z-Wave JS sees changes too.

Stock Olimex ESP32-PoE / WROVER modules do not normally have a custom MAC
burned, so this most likely affects nothing. Confirm on a sample unit before a
fleet OTA. Escape hatch if needed:

```yaml
esp32:
  framework:
    advanced:
      ignore_efuse_custom_mac: true
```

## 6. stream_server deprecation — fixed upstream

Codegen on 2026.9.0 emits:

```
WARNING parse_esphome_version() is deprecated. Use cv.require_esphome_version
to gate on a minimum version. Removed in 2027.2.0
```

From `components/stream_server/__init__.py` in `tube0013/esphome-components`:

```python
if (2025, 12, 0) <= parse_esphome_version() < (2026, 3, 0):
    uart.request_wake_loop_on_rx()
```

That block is dead code twice over:

1. `uart.request_wake_loop_on_rx()` existed only in ESPHome 2025.12.0 - 2026.2.x.
   It opted a build into `USE_UART_WAKE_LOOP_ON_RX`, which compiled in a
   FreeRTOS task that blocked on the IDF UART event queue and called
   `App.wake_loop_threadsafe()` so RX bursts did not wait for the next loop
   iteration. 2026.3.0 replaced the task with an ISR callback
   (`Application::wake_loop_isrsafe()`), made it the default on ESP32 builds
   with networking, and deleted the helper. Confirmed present in the generated
   `defines.h` for all three targets on 2026.9.0.
2. The component cannot load on 2025.12 - 2026.2 at all. The
   `@automation.register_action(..., synchronous=False)` decorators further down
   the same file need `synchronous=`, which ESPHome added in **2026.3.0**. On
   2026.1.5 / 2026.2.4 the module raises
   `TypeError: register_action() got an unexpected keyword argument 'synchronous'`
   at import time.

So the condition can never be true on any ESPHome that can import the module.

Fixed on `fix/stream-server-drop-parse-esphome-version` in
`tube0013/esphome-components` (commit 35ae92d): import and block removed, and
the declared floor corrected from `cv.require_esphome_version(2022, 3, 0)` to
`(2026, 3, 0)`. Note the floor is documentation only — the decorator runs at
import, before `CONFIG_SCHEMA` is evaluated, so older ESPHome still fails with
the `TypeError` rather than a clean version message.

Re-validated on 2026.9.0 with the patched component: all three manifests valid,
deprecation warning gone, `USE_UART_WAKE_LOOP_ON_RX` still defined.

## 7. Checked and not applicable

- **Modbus overhaul** (the bulk of the breaking changes) — modbus is not used.
- **`ethernet: spi_id`** and the W5500 SPI rework — every manifest is
  `type: LAN8720`, no SPI Ethernet in the tree.
- **`clk_mode` deprecation** — already migrated to the `clk:` block; removal was
  pushed from 2026.9.0 out to 2026.11.0 anyway.
- **UART config-time validation** for cm1106, daly_bms, hrxl_maxsonar_wr,
  hydreon_rgxx, mhz19, pylontech, teleinfo, vbus, wl_134 — none used.
- **LEAmDNS double-free fix** — ESP8266/Arduino only; these are ESP-IDF builds.
- **Time component POSIX TZ parser removal** — `time:` not used.
- **`improv_serial` over Ethernet** — the only thing it would add here is a
  static-IP setup flow, which upstream has said is out of scope. Skipping.
- **ESP8266 Arduino <3.0.0 rejection**, ESP32 Hosted IDF 5.3 floor — not applicable.
- **ESP-IDF version** — unchanged at 5.5.5 (same as 2026.8.2). No toolchain bump.

## 8. Worth knowing, no action

- API encryption flash cost down ~50%; Noise-encrypted `esphome` OTA now
  available using the existing API key.
- Stalled API writes now use a reusable overflow buffer with a capped backlog
  instead of allocating per write — relevant given `api: reboot_timeout: 0s`,
  and doubly so for the ZW build where Z-Wave frames ride the API connection.
- OOM hardening: stalled API writes and OTA verification failures no longer
  reboot the device.
- OTA timeouts raised (device waits 105s for data, CLI 160s for ack).
- Build-time: registry library downloads parallelised; no-op rebuild of a
  six-library config 2.9s -> 0.4s.

## 9. Build reproducibility

**Corrected from the earlier pass.** This branch *does* pin ESPHome:
`build.yml` and `main.yml` both use
`ghcr.io/esphome/esphome:${{ vars.ESPHOME_VERSION }}`, and
`esphome-version-check.yml` opens a tracking issue every Monday when upstream
ships a newer stable. Bump with
`gh variable set ESPHOME_VERSION --body 2026.9.0` when ready.

The remaining gap is the other direction: the external components are not
pinned. The manifests use

```yaml
external_components:
  - source: github://tube0013/esphome-components
    refresh: 1s     # cc2652p7
    refresh: 2min   # mgm24
```

so every build pulls whatever is on `main` of that repo at that moment. That is
convenient during development and the reason the stream_server fix will land in
builds automatically — but it also means two builds of the same tag can differ.
Worth pinning `ref:` to a tag or commit on release branches, even if `main`
stays live for development.

---

## Compile status

`esphome config` passes and code generation completes for all three targets. The
full C++ compile and link could not be run in this environment: ESPHome's
ESP-IDF installer fetches
`https://dl.espressif.com/dl/esp-idf/espidf.constraints.v5.5.txt` to build the
IDF Python env, and `dl.espressif.com` is blocked by the sandbox egress policy
(403 from the proxy). The IDF 5.5.5 framework and the xtensa toolchain
downloaded fine from GitHub — only that one host is blocked.

So the findings above rest on config validation, code generation, and a source
audit of the 2026.8.2 -> 2026.9.0 diff plus every include in the external
components. A real link is still needed to be certain about section 4.

To run it locally on macOS:

```bash
cd ~/Documents/GitHub/TubesZB-ESPHome-Builder-gh-pages
python3 -m venv .venv-2026.9 && source .venv-2026.9/bin/activate
pip install 'esphome==2026.9.0'
esphome compile manifests/tubeszb-efr32-mgm24-2023.yaml
esphome compile manifests/tubeszb-cc2652p7-poe-2023.yaml
esphome compile manifests/tubeszb-2026-zw.yaml
```

Or dispatch the existing workflow, which uses the pinned container:
`gh workflow run build.yml -f manifest_glob='tubeszb-efr32-mgm24-2023.yaml'`
