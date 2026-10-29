# TubesZB ESPHome firmware builder

Build ESPHome firmware for TubesZB devices. Each hardware model has one firmware
image that supports both DHCP and a user-configured static IPv4 address.

All custom components and embedded frontend assets are maintained in
[esphome-components](https://github.com/tube0013/esphome-components/pull/7).
The YAMLs import them from GitHub; there is no local component-source dependency.
During review, `tubeszb_components_ref` in `packages/network-settings.yaml` points
to `refs/pull/7/head`. Change that single default to `main` after the component PR
merges; CI checks out the same reference for host/browser integration tests.

**Upgrading an old static-IP build:** the first upgrade starts on DHCP because
old compiled-in addresses are not stored in the new settings. Find the device's
DHCP lease, set and confirm its desired static address, and update the ZHA,
Zigbee2MQTT or Z-Wave connection address if it changed. See the
[upgrade and release notes](docs/release-notes.md).

[Open the web flasher](https://tube0013.github.io/TubesZB-ESPHome-Builder).

Every hardware manifest now opens the TubesZB frontend at the device's main
address. It includes device/radio status, network settings, gateway firmware
management, diagnostics and downloadable logs retained across sections. The
original ESPHome interface remains at `/esphome`.

Zigbee models include native radio identity, flashing, bootloader controls and
saved application baud. Older MGM12/MGM210 profiles use model-specific stable
images; MGM21 dual boards enter their bootloader over EZSP because the boot pin
is input-only. Dual-radio models show Zigbee and Z-Wave independently.
See [rollout coverage and feature gaps](docs/frontend-feature-parity.md),
[dual-radio notes](docs/dual-radio-frontend.md), and
[MGM24/BP controls](docs/mgm24-frontend.md).

The retired MGM12 Pro1 and original MGM210 Series 2 profiles preserve the archived
WT32-ETH01 pin mappings. Their Arduino-to-ESP-IDF migration builds need physical
OTA/partition and radio-traffic validation before release use; see the rollout notes.

New installations use DHCP. Open the device's web interface and select **Network
settings**, or visit `http://<device-ip>/network`. Choose DHCP or Static IP, enter
the address, subnet mask, gateway and DNS servers, then select **Apply & Restart**.
Open the new address and select **Keep settings** within two minutes. Without
confirmation, the device restores the previous confirmed configuration and restarts.
An unexpected restart during a trial also restores the confirmed configuration.

Confirmed settings survive normal restarts and OTA updates that preserve NVS.
The `firmware.factory.bin` image is for USB installation; `firmware.ota.bin` is
for OTA installation through the device's web interface.

Separate static-IP issue builds are retired. The files in `esphome-config/` are
compatibility entry points to the same manifests used by release builds.

Requires ESPHome **2026.10.1 or later**; development validation uses **2026.10.1**.
Before publishing a release, set the repository's `ESPHOME_VERSION` variable to
a compatible version.

See [network settings design, tests, and hardware acceptance checks](docs/network-settings.md).

The TubesZB frontend also supports the seven single-radio CC2652 configurations.
See [CC2652 frontend](docs/cc2652-frontend.md) for controls and test-image details.

## ESPHome build versions

Set repository variables under **Settings → Secrets and variables → Actions → Variables**:

| Variable | Used by |
| --- | --- |
| `ESPHOME_VERSION` | Stable releases and manual builds without an override |
| `ESPHOME_PRERELEASE_VERSION` | Releases marked as a prerelease |

Prerelease builds fall back to `ESPHOME_VERSION` if `ESPHOME_PRERELEASE_VERSION` is unset.
The manual **Build ESPHome Firmware** workflow's `esphome_version` input overrides
these variables for that run only.

Both pins must be compatible with these manifests (ESPHome 2026.10.1 or newer).
Local development and regression builds use 2026.10.1.

Publishing a prerelease builds with the prerelease pin and publishes its firmware
to `Beta_Release` and `beta_ota_manifests`. Publishing a stable release uses the
stable pin and publishes to `Latest_Release` and `ota_manifests`. Manual builds
upload workflow artifacts only.

For local source/browser tests, validate a hardware manifest first to populate
the GitHub component cache, or set `TZB_COMPONENTS_ROOT` to the `components`
directory of an esphome-components checkout. Firmware builds always use their
GitHub source; this variable only selects source for host/browser tests.

Component host tests are maintained in esphome-components. This repo keeps
hardware configuration contracts and browser integration tests; CI runs the
component suite directly from the matching external checkout.

Product model labels are explicit in the hardware manifests, so changing a device
hostname does not alter the UI's model identity. Component cleanup and the deferred
radio-controller refactor are tracked in the paired component PR.
