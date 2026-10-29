# Persistent Ethernet configuration

Based on `refs/tags/2026.10.25.1` (`7f968ca`), the same commit as `2026.10.25.0`.
ESPHome 2026.10.1 is the validation target. The source tag remains unchanged; build
this feature from its implementation branch or a new release commit.

## Firmware and web UI

Every release manifest includes `packages/network-settings.yaml`. This adds the
`network_settings` external component from the shared GitHub repository. The old `esphome-config/` paths
include these same manifests. There is no separately compiled static-IP firmware.
The old static-IP issue form and workflow are removed.

The existing web frontend links to `/network`. The settings page embeds all HTML,
CSS and JavaScript in firmware and needs no Internet connection or Home Assistant.
It uses short status requests during a trial, not another event-stream socket.
Normal settings pages do not poll continuously. Requests use the existing server's
port and authentication.

Changing address mode or static settings saves a complete candidate and restarts.
The success screen links to the candidate IP (or `.local` hostname for DHCP).
The user opens that address and explicitly selects **Keep settings**. No page load,
ping or connectivity check confirms implicitly. Static confirmation verifies that
the HTTP connection reached the candidate IPv4 address. A DHCP lease may need to
be found in the router if mDNS does not work.

HTTP and main-loop state is serialized with a mutex. Handlers respect ESPHome web
authentication and require a per-boot token. Mutations from a different browser
Origin are rejected, even though the shared server permits CORS. Settings and
tokens are not cached.

## Persistence and rollback

NVS namespace `tzb_network`, key `state_v1`, stores one fixed-size record: a layout
identifier, phase, confirmed configuration and candidate configuration. Addresses
use a fixed octet order. The component uses IDF NVS directly so HTTP-task writes
do not touch ESPHome's main-loop preference write queue. Writes are committed
before transitions are acknowledged. ESPHome factory reset erases all NVS,
including these settings.

| Stored phase at boot | Boot action |
| --- | --- |
| Missing/invalid | DHCP |
| Confirmed | Load confirmed settings |
| Staged | Persist Testing, then boot candidate and start the 120-second timer |
| Testing | A trial was interrupted: restore confirmed settings and clear candidate |

Setup priority 251 runs immediately before Ethernet's priority 250. Static mode
supplies the selected `ManualIP` to ESPHome's Ethernet component. DHCP starts
without one. This keeps cable reconnects consistent with the selected settings.
Code generation enables `USE_ETHERNET_MANUAL_IP` even though YAML has no `manual_ip`.

The deadline starts before Ethernet initialization. It does not wait for a link,
lease, gateway or browser. At timeout, or on **Revert now**, the confirmed settings
are restored and the device restarts. Failed persistence disables further changes
until restart. If clearing a timed-out trial fails, the device still restarts:
the saved Testing marker restores the confirmed configuration on boot. Timer
calculations tolerate `millis()` wrapping.

The component rejects `ethernet.manual_ip`, Ethernet disabled on boot, and
simultaneous WiFi. It supports the Ethernet-only ESP-IDF models in this repo.
Enable web-server authentication when authenticated changes are required.

## Validation

Browser field errors appear before submission. Firmware independently parses
and validates the entire configuration before saving. Editing fields never
changes the live network or persisted settings.

- Strict dotted-decimal IPv4: four decimal octets, 0–255. Leading zeros,
  shorthand, whitespace, hostnames and IPv6 are rejected.
- Device and gateway addresses must be unicast. Loopback, link-local, multicast
  and reserved high ranges are rejected.
- Subnet masks must be contiguous, /1 through /30. /31 and /32 are unsupported.
- The device IP cannot be the subnet's network or broadcast address.
- Gateway is required, different from the device, and a usable host in its subnet.
- Primary DNS is required and may be outside the subnet. Secondary DNS is optional;
  blank or `0.0.0.0` disables it. Public unicast DNS is supported.
- DHCP ignores static fields and saves a clean DHCP configuration.

Validation cannot prove an IP is unused, outside the DHCP pool, or reachable from
the user's computer. No ping or Internet dependency gates confirmation. Rollback
protects unconfirmed trials; a user can still confirm an inappropriate configuration.

## Upgrade and release notes

Old static-IP firmware has no record in this NVS namespace. Its first upgrade
therefore starts with DHCP. Find the device through the router or mDNS and
configure the desired static address in the UI. After confirming it once, normal
OTA updates retain it. Erasing NVS or factory reset returns networking to DHCP.
Older firmware does not read this component's settings.

Project version `2026.10.25.1` is the development baseline. Set the final version
and a new tag when preparing the release. Set repository variable `ESPHOME_VERSION`
to 2026.10.1 or another tested compatible version. The release workflow now finds
binaries in the native-IDF layout. No existing tag or external variable is changed.

## Verification

On 2026-10-27, the user reported successful physical testing of the network-only
Z-Wave image: OTA installation, repeated address changes, web restarts, cable/power
interruptions, and recovery to the previous address after an unreachable-IP trial
expired at two minutes. These results validate that image's network feature; the
new branded frontend still needs its own physical-device acceptance test.

```sh
python -m pip install esphome==2026.10.1 pytest
python -m pytest -q tests
# Run network unit tests from the matching esphome-components checkout.
python tests/check_configs.py
esphome compile manifests/tubeszb-2026-zw.yaml
esphome compile manifests/tubeszb-cc2652p7-poe-2023.yaml
```

Tests run the actual C++ parser and boot state machine on the host, covering mode
changes, confirmation, manual rollback, timeout, late confirmation, interrupted
trials, failed persistence, invalid records and timer wraparound. Browser and
firmware validators share the same test cases. CI validates all entry points
and compiles representative Z-Wave and Zigbee images. The mock-device browser
checks exercise inline validation, apply/reconnect links, explicit confirmation,
manual revert, expired trials, failed requests, unavailable storage and mobile layout:

```sh
npm install --prefix /tmp/tzb-browser-tests playwright@1.62.1
/tmp/tzb-browser-tests/node_modules/.bin/playwright install chromium
NODE_PATH=/tmp/tzb-browser-tests/node_modules node tests/network_ui_test.cjs
```

Before release, check a physical device:

1. Fresh DHCP boot; open Network settings without Internet access.
2. Reject invalid IPs, masks, gateway and DNS without changing the live network.
3. Trial static settings, keep them, power cycle and verify retention.
4. Trial an unreachable IP; verify rollback with the browser closed or cable unplugged.
5. Power-cycle during a trial; verify immediate recovery on confirmed settings.
6. Trial DHCP from confirmed static settings; test both keeping and reverting.
7. Reconnect Ethernet after confirmation; verify IP and DNS remain correct.
8. OTA update and verify retention; factory reset and verify DHCP.
9. Verify radio TCP clients reconnect and web authentication still applies.
10. Check free heap and flash headroom on every hardware variant.

## Use without internet

The branded Network page includes **Use without internet**. Enabling it requires
confirmation explaining that the device cannot check for new firmware, with a
link to [TubesZB Builder releases](https://github.com/tube0013/TubesZB-ESPHome-Builder/releases).
The Firmware page shows the same release reminder while enabled. Gateway/radio
check and download controls are disabled; local OTA file uploads, serial traffic,
radio identity/bootloader/reset controls and log capture remain available.

Every hardware manifest also exposes a template switch with ID
`use_without_internet`. Both interfaces use the same NVS value, `tzb_network` /
`offline_v1`, committed immediately before acknowledging a change. The template
uses `restore_mode: DISABLED` because restoration belongs to `network_settings`;
it reads the component's actual state instead of keeping another preference.
An absent value defaults to online checks enabled. Read failures keep online
checks disabled. Factory reset erases the setting with the rest of NVS.

The policy begins disabled until restored. Restoration and installation of the
HTTP guard run at priority `ETHERNET + 1`, before Ethernet starts and before the
HTTP update component's `AFTER_WIFI` setup. The switch's earlier setup does not
write a default value. There is no startup window that launches checks before
the saved setting is known.

All 22 hardware manifests gate connectivity and update-check scripts and every
native gateway/radio check action. Weekly and reconnect checks respect the flag;
background connectivity tasks recheck before requesting and before using their
results. Radio firmware download actions are gated too, including both existing
CC2652 dual-radio configurations. CC2652's own periodic/boot manifest checks
remain disabled. The gateway updater also uses a guarded HTTP parent so direct
ESPHome API/web update-entity checks cannot bypass the policy. A direct native
check may report an unsuccessful check, but does not issue an HTTP request.
Already-started requests can finish; no new request is started while disabled.
Turning the setting off permits subsequent manual, scheduled or reconnect checks.

The setting is independent of DHCP/static-IP rollback. Changes are rejected while
a network trial, gateway update or tracked radio operation is active. Save failures
leave the current setting unchanged and are reported to the browser. No reboot is
needed to change it. The GitHub release link is opened by the user's browser;
the device does not fetch it.

Verification: esphome-components owns the native restoration/commit and
HTTP-transport tests. This repo's `tests/test_internet_policy.py` checks the
manifest guards, online re-enable automation and single connectivity-check path.
`tests/internet_mode_ui_test.cjs` checks enable/cancel, failed saves, persisted
reloads, release links, disabled online controls, retained local controls and
mobile layout for Z-Wave, CC2652 and EFR32.

CC2652 radio identity discovery is independent of the online-check scripts: the
shared startup probe runs locally even with Use without internet enabled or no
Ethernet connection. The bridge is paused during ROM detection and startup ZNP
reads, then its prior state is restored. Update checks wait for that sequence.
MGM24's existing local boot-probe sequence is also independent of internet policy.
