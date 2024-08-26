This Repository can create binaries for TubesZB devices which require a static IP.

To Start Create an Issue using the Issue Form, selecting the device name, inputting the desired network configuration.

A GitHub action will start once the issue is submitted and if all goes okay the issue will get a comment including a link to download the compiled binaries for flashing your device.

The download will include 2 .bin files.
The firmware.factory.bin is suitible for flashing over usb using web.esphome.io
The firmware.ota.bin can be flashed through the devices web frontend (if option exists).
