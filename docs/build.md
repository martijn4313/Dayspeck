# Build and flash

Requires [PlatformIO](https://platformio.org).

```sh
pio run                       # build
pio run -t upload             # flash the firmware
pio run -t uploadfs           # flash firmware/data (config.json) to the filesystem
pio test -e native            # host unit tests for the pure logic
pio run -e esp01_1m_debug     # development build with on-screen debug status
pio run -e esp01_1m_kids      # kids variant (what to wear)
```

Optional compile-time defaults (WiFi credentials) go in `firmware/include/secrets.h`; copy
`secrets.h.example` and edit it. The file is git-ignored. Everything can also be set later in the
web UI, so this is only a convenience.

`firmware/data/config.json` ships with empty WiFi credentials. To keep your own local edits out of
commits: `git update-index --skip-worktree firmware/data/config.json`.
