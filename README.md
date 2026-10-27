# ESPHome firmware builder

Build ESPHome firmware for specific TubesZB devices.

To Flash a TubesZB device with a web based flasher: https://tube0013.github.io/TubesZB-ESPHome-Builder

**For Static IP Firmware Builds**

This Repository can create binaries for TubesZB devices which require a static IP.

To Start Create an Issue using the Issue Form, selecting the device name, inputting the desired network configuration.

A GitHub action will start once the issue is submitted and if all goes okay the issue will get a comment including a link to download the compiled binaries for flashing your device.

The download will include 2 .bin files.
The firmware.factory.bin is suitible for flashing over usb using https://web.esphome.io
The firmware.ota.bin can be flashed through the devices web frontend (if option exists).

> Inspired by works from @nerivec, @shawly & more.

## ESPHome build versions

Set repository variables under **Settings → Secrets and variables → Actions → Variables**:

| Variable | Used by |
| --- | --- |
| `ESPHOME_VERSION` | Stable releases, static IP builds, and manual builds without an override |
| `ESPHOME_PRERELEASE_VERSION` | Releases marked as a prerelease |

Prerelease builds fall back to `ESPHOME_VERSION` if `ESPHOME_PRERELEASE_VERSION` is unset.
The manual **Build ESPHome Firmware** workflow's `esphome_version` input overrides
these variables for that run only.

For example, keep stable builds on ESPHome 2026.6.5 while testing 2026.10.0:

```sh
gh variable set ESPHOME_VERSION --body 2026.6.5
gh variable set ESPHOME_PRERELEASE_VERSION --body 2026.10.0
```

Publishing a prerelease builds with the prerelease pin and publishes its firmware
to `Beta_Release` and `beta_ota_manifests`. Publishing a stable release uses the
stable pin and publishes to `Latest_Release` and `ota_manifests`. Manual builds
upload workflow artifacts only.
