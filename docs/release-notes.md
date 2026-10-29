# Release preparation and upgrade notes

## Existing static-IP installations

The first upgrade from an older firmware with a compiled-in static IP **starts
with DHCP**. The firmware cannot recover the old compiled address from NVS.
Find the new lease in your router (or use the device hostname), open Network,
enter the desired static configuration, and select **Keep settings** at the new
address within two minutes. Update the connection address in ZHA, Zigbee2MQTT
or Z-Wave JS if it changed. Once confirmed, settings survive subsequent OTA
updates and normal restarts. Include this notice in the published release body.

## Scope and version

This review intentionally builds on the 2026.10.25.1 release work: TCP send-buffer
settings, background connectivity probes where already enabled, beta channels,
and GitHub Pages feeds remain included. The static-IP issue form/workflow stays available on the frozen `2026.10.25.0`
source, ESPHome `2026.6.5` and pinned legacy components during testing.

The project version and prerelease tag are `2026.10.29.1`. This replaces the failed `2026.10.29.0` release build; the release version
guard now uses the ESPHome CLI without requiring Python `packaging`. Publishing
this prerelease triggers only the beta asset/feed path. Keep
`refs/pull/7/head` until component PR #7 is merged, then switch the shared default
to `main`. ESPHome `2026.10.1` is the minimum for the new firmware; the legacy
issue action has its own immutable source and compiler pins.
