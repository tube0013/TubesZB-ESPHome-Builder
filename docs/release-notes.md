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

The project version and prerelease tag are `2026.10.29.2`. This prerelease
removes the remaining deprecated stream-server buffer settings and the fixed
socket limit. The shared component pin includes the missing socket declarations,
so ESPHome calculates capacity from the enabled components. Publishing this
prerelease updates the beta asset/feed path.

`packages/network-settings.yaml` pins the tested component commit; CI uses the
same revision. ESPHome `2026.10.1` remains the minimum for this firmware. The
legacy issue action retains its own immutable source and compiler pins.
