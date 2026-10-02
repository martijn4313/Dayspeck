# Contributing

## Project layout

```
firmware/src, include   device code (display, touch, weather, web server, main loop)
firmware/lib/motologic  pure logic, unit tested on the host
firmware/data           files for the LittleFS filesystem (config.json)
docs/images             the screenshots used in this README
test/                   host-side unit tests
tools/                  png_to_bitmap.py — bitmap converter and OLED simulator (see tools/README.md)
tools/ota_tool.py       signing key, image signing and release manifest for OTA updates
tools/ota-relay         Cloudflare Worker that serves releases to the device over HTTP
plans/                  improvement plan and design notes
```

## Continuous integration

CI (`.github/workflows/ci.yml`) runs the unit tests, builds all firmware environments and the
filesystem image, checks that the firmware leaves room for an update, and lints and tests the Python
tools. Tags `vX.Y.Z` publish a signed release (`.github/workflows/release.yml`). See `plans/improvement_plan.md` for the roadmap.
