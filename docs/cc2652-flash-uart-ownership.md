# CC2652 flash UART ownership

Hardware testing on the CC2652P2 PoE 2021 found radio flashing failed in both the
TubesZB and original ESPHome interfaces. Pausing the serial bridge manually made
the same flash succeed. The serial bridge and ROM flasher were competing for the
same UART. This supersedes the earlier assumption that CC2652 did not need a pause.

## Shared fix

All nine CC2652 manifests include `packages/cc2652-flash.yaml`. Their existing
`flash_cc2652` button calls the single `flash_cc2652_safely` script. The TubesZB
frontend invokes that same button, so the fix also covers ESPHome's web interface,
Home Assistant button presses and the existing REST button endpoint. The sequence
runs on the device, so closing a browser tab does not skip restoration.

The script:

1. Records whether the Zigbee serial bridge was already paused.
2. Calls the stream server's pause action and waits for its UART handoff.
3. Queues the existing `cc2652_flasher.update_firmware` action.
4. Waits for a native flash result. The action only queues work, so immediately
   resuming after that YAML action would release the UART too soon.
5. After success, holds the pause for five seconds to cover the flasher's native
   ZNP identity reads scheduled at 1.2 and 4 seconds after flashing.
6. Restores the running bridge after success or failure. A bridge explicitly
   paused before the flash stays paused.

The script uses `mode: single`; repeated requests cannot reset the ownership
state during an existing flash. The small `FlashSession` type observes only the
native CC2652 flasher's logs. A native error makes failure cleanup eligible;
only `Update finished OK. Resetting target` marks success. A 100% progress line
or a transfer-complete line is not sufficient.

No resume occurs from a logger callback. The native updater runs synchronously
on ESPHome's main loop, so the script's wait can run only after the entire updater
returns, including its error cleanup and normal UART baud restoration. A
low-level error that is retried can still be followed by a successful native
result before that wait runs. If the native flasher changes to asynchronous UART
work or changes its result logs, this completion contract must be updated to use
the corresponding completion callback. Native INFO result logs must remain enabled.

The frontend includes the shared script's running state in its radio busy state,
so competing controls and network changes stay blocked through readback and bridge
restoration. The existing progress/confirmation and completion-banner timeout are
retained. Check, detect and manual bootloader actions are otherwise unchanged.

## Coverage and reuse

Included models:

- CC2652P2 PoE 2021, 2022 and 2023.
- CC2652P2 Ethernet/USB 2021, 2022 and 2023.
- CC2652P7 PoE 2023.
- `tubeszb-dual-radio-kit-cc2652` and `tubeszb-dr-cc2652-exp`.

Single-radio models pass `stream_srv` as `cc2652_stream_server`. The dual-radio
models pass `zb`, so only the Zigbee bridge is paused. Their Z-Wave UART/API
path is untouched. Compatibility files inherit their corresponding manifest.

Future CC2652 configs must include this package with their Zigbee stream ID and
route every radio-flash button through `flash_cc2652_safely`. The raw flasher
update action belongs only in this shared package; keeping one copy prevents
individual configs from losing the required pause or error-path resume.

Host tests cover pending starts, success, early errors, retried errors, duplicate
starts, progress without completion and preservation of an existing manual pause.
All configurations are validated, and representative P2, P7 and dual-radio builds
check the generated automation and stream binding. Physical verification of the
automatic flow remains the next device test.

## Local startup probe

The package also runs `probe_cc2652_on_boot` from a priority -100 boot hook, after
component setup. This local sequence is unconditional: no internet, Ethernet or
network-trial guard suppresses it. The bridge is paused immediately, then a 100ms
wait lets the existing GPIO reset pulse finish. Native ROM detection fills the
variant sensor; a bounded 10-second wait handles failures without leaving UART
ownership held indefinitely. A subsequent 5-second hold covers the native
component's 0.7s/4.5s startup ZNP identity reads and reset cleanup before restoring
the bridge's captured pause state. Both success and failure restore that state.

All nine update-check scripts wait for the startup probe and retain the internet
policy guard. The frontend includes the boot probe script in its busy state, so
maintenance and network controls remain blocked through restoration. Keep native
`detect_on_boot: false` to avoid a second probe outside this sequence. New CC2652
configs inherit this startup sequence with the same shared package/stream binding.

Flash ownership now uses the native flasher result/sequence getter, rather than
matching log messages. A queued operation that never starts times out after 35
seconds and restores the bridge state. Active synchronous flashing and readback
keep their existing ownership; progress at 100% alone does not signal completion.
