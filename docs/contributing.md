# Contributing

## Project layout

```
firmware/src, include   device code (display, touch, weather, web server, main loop)
firmware/lib/motologic  pure logic, unit tested on the host
firmware/data           files for the LittleFS filesystem (config.json)
docs/                   this manual (MkDocs) and its screenshots
test/                   host-side unit tests
tools/                  bitmap converter and OLED simulator (see tools/README.md)
tools/docs_screenshots.py, webui_screenshots.py   the pictures in this manual
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

They are renders, not photos. `tools/docs_screenshots.py` draws the ride screens with the host simulator
(which mirrors `firmware/src/display.cpp`) and makes the hero picture and the animations;
`tools/webui_screenshots.py` takes the web UI pictures from the page in `webserver.cpp` with a mock
server. The pictures of the picture screens and the weather report were drawn by one-off host builds of
the firmware's own drawing and report code; those builds are not in the repository yet.
