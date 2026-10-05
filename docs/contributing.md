# Contributing

## Project layout

```
firmware/src, include   device code (display, touch, weather, web server, main loop)
firmware/lib/motologic  pure logic, unit tested on the host
firmware/data           files for the LittleFS filesystem (config.json)
docs/                   this manual (MkDocs) and its screenshots
test/                   host-side unit tests
tools/                  bitmap converter and OLED simulator (see tools/README.md)
tools/docs_screenshots.py, screen_pictures.py, webui_screenshots.py   the pictures in this manual
tools/ota_tool.py       signing key, image signing and release manifest for OTA updates
tools/ota-relay         Cloudflare Worker that serves releases to the device over HTTP
plans/                  design notes and the history of the project (partly out of date)
```

## Continuous integration

CI (`.github/workflows/ci.yml`) runs the unit tests, builds all firmware environments and the
filesystem image, checks that the firmware leaves room for an update, and lints and tests the Python
tools. Tags `vX.Y.Z` publish a signed release (`.github/workflows/release.yml`) once the signing key is set up
(see [over-the-air updates](ota.md)).

## The pictures in this manual

They are renders, not photos, and three scripts make them:

```sh
python tools/docs_screenshots.py      # the ride screens: rider-today.png, rider-wind.gif
python tools/screen_pictures.py       # the other screens, the GIFs of the leaves and the report, hero.png
python tools/webui_screenshots.py     # the web UI (needs Playwright and a Chromium)
```

`docs_screenshots.py` uses the host simulator in `tools/bitmaptool`, which mirrors `firmware/src/display.cpp`.
`screen_pictures.py` compiles the firmware's own `display.cpp`, `motologic` and the Adafruit GFX library for the
host (with the stand-ins in `tools/screenshots_host/mock` for the Arduino core and the display driver) and runs
the scenes in `tools/screenshots_host/screens.cpp`, so those pictures are exactly what the device draws. It
needs a C++17 compiler and the libraries PlatformIO downloads; CI checks that it still builds and runs.
`webui_screenshots.py` loads the page from `webserver.cpp` with a mock server.
