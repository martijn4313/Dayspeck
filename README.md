# Dayspeck

<p align="center"><img src="docs/images/hero.png" alt="The rider screen (a big check mark, skyline and weather) next to the kids screen (a t-shirt now, a rain coat this afternoon)" width="640"></p>

A small weather display for a bedside table or a wall, on an ESP8266 with a 128×64 SSD1306 OLED.
It comes in two flavours, built from the same code:

- **Rider** (default): answers one question at a glance, **can I ride today?**
- **Kids** (`esp01_1m_kids`): for children of 4-5 who are learning to read. It answers **what do I
  wear today?** with pictures and numbers.

Weather data comes from [Open-Meteo](https://open-meteo.com) (free, no account or API key).

**📖 Read the manual: <https://martijn4313.github.io/Dayspeck/>**

## Quick start

Requires [PlatformIO](https://platformio.org).

```sh
pio run -t upload             # flash the rider firmware (or -e esp01_1m_kids for the kids one)
pio run -t uploadfs           # flash firmware/data (config.json) to the filesystem
pio test -e native            # host unit tests for the pure logic
```

Then join the `Dayspeck` WiFi network the device opens and follow the
[first boot](https://martijn4313.github.io/Dayspeck/first-boot/) steps. Wiring, configuration,
over-the-air updates and security notes are all in the manual.

## Project layout

```
docs/                   the manual (MkDocs) and its screenshots
firmware/src, include   device code (display, touch, weather, web server, main loop)
firmware/lib/motologic  pure logic, unit tested on the host
firmware/data           files for the LittleFS filesystem (config.json)
test/                   host-side unit tests
tools/                  bitmap converter and OLED simulator, OTA signing tool and relay
plans/                  improvement plan and design notes
```

## Building the manual

```sh
pip install -r requirements-docs.txt
mkdocs serve        # live preview at http://127.0.0.1:8000
mkdocs build --strict
```

The manual is published to GitHub Pages by `.github/workflows/docs.yml` on every push to `main`.

## Contributing

CI (`.github/workflows/ci.yml`) runs the unit tests, builds all firmware environments, and lints and
tests the Python tools; see the manual's *Contributing* page for details. Tags `vX.Y.Z` publish a signed
release. See `plans/improvement_plan.md` for the roadmap.

## License

See [LICENSE](LICENSE).
