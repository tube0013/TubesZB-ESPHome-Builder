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

> Inspired by works from @nerivic, @shawly & more.