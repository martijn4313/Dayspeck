# Build and flash

Requires [PlatformIO](https://platformio.org).

```sh
pio run                       # build
pio run -t upload             # flash the firmware
pio run -t uploadfs           # flash firmware/data (config.json) to the filesystem
pio test -e native            # host unit tests for the pure logic
pio run -e esp01_1m_debug     # development build with on-screen debug status
```

An ESP-01 has no USB port: flash it with a USB-serial adapter (3.3 V) and GPIO0 held low at power-up; most
ESP-01 programmers have a switch or button for that. The touch sensor shares the RX pin, so disconnect it
while flashing (see [hardware](hardware.md)). After the first serial flash, updates can come
[over the air](ota.md).

Optional compile-time defaults (WiFi credentials) go in `firmware/include/secrets.h`; copy
`secrets.h.example` and edit it. The file is git-ignored. Everything can also be set later in the
web UI, so this is only a convenience.

`firmware/data/config.json` ships with empty WiFi credentials. To keep your own local edits out of
commits: `git update-index --skip-worktree firmware/data/config.json`.
