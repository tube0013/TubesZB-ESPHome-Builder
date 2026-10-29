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
and GitHub Pages feeds remain included. The static-IP issue form/workflow is
retired because each model now has one firmware with saved network settings.

The project version stays 2026.10.25.1 while this is a draft. Before publishing,
assign the actual next release version/tag and switch the shared component ref
from refs/pull/7/head to main after the component PR merges. ESPHome 2026.10.1 is
the minimum for both release pins and manual builds. No release is published as
part of these review fixes.
