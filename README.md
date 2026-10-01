# MotoClock

A bedside weather display for motorcyclists, on an ESP8266 (ESP-01) with a 128×64 SSD1306 OLED.
It answers one question at a glance: **can I ride today?**

- **Left half:** a big ride badge — ✓ good, ! caution, X don't ride.
- **Right half, top:** city skyline with sun or moon, temperature and trend arrow, with a rain
  animation when it rains.
- **Right half, bottom:** weather icon, wind speed and precipitation.
- **Touch:** a short tap switches *today / tomorrow*. A long press steps through the other screens:
  the *7-day AM/PM grid*, the *next hours*, the *clock*, then back to the main screen. The week and
  hours views close by themselves after 30 s; the clock stays until you tap. A tap always returns to the
  main screen.
- **Status marks:** a signal-bars icon (bottom of the left half; a cross when offline), a `TMR` tag
  while tomorrow is shown, and an `OLD` tag when the data is stale (older than twice its refresh
  interval).
- **Week grid:** the best day (see the ride score below) is shown in inverse video.
- **Next hours:** six columns, with a label on the left naming each row: `h` the hour, `°C` the
  temperature, `mm` a bar for the rain amount (taller = more), `%` a dotted line for the chance of rain
  (higher = likelier) and `kmh` the strongest gust. The bottom line shows the best time to leave in the
  next 12 hours (a 2 hour daytime ride) and when the data was last updated.
- **Clock:** the time in large digits with a blinking colon, then the weekday and date, then the year.
  It needs the network time and the first weather update (which tells the device its time zone), and
  says "Time not set yet" until then.
- **Web UI** at `http://motoclock.local` (or the device IP): location, WiFi, thresholds, logs and
  firmware updates.

Weather data comes from [Open-Meteo](https://open-meteo.com) (free, no account or API key).

## Kids variant: what do I wear today?

A second firmware build for children of about 4-5 who are starting to read. Instead of the ride
rating it shows what to wear and what the weather is like, with big shapes and as little text as
possible. A tap on the touch sensor switches between the two screens (they close by themselves after
30 s).

**Clothes screen**

| Temperature | Picture | Word (English / Dutch) |
|-------------|---------|------------------------|
| 20 °C and up | t-shirt and shorts | `WARM` / `WARM` |
| 15 to under 20 °C | t-shirt | `MILD` / `MILD` |
| under 15 °C | sweater | `COOL` / `KOEL` |

The left half shows the clothes, the right half the current temperature as a big number (a number
to read, no unit) and a short word.

**Weather screen:** a big picture (sun or moon, partly cloudy, cloud, rain, thunderstorm, snow, wind)
with its name: `SUN`/`ZON`, `MOON`/`MAAN`, `CLOUD`/`WOLK`, `RAIN`/`REGEN`, `STORM`/`ONWEER`,
`SNOW`/`SNEEUW`, `WIND`/`WIND`.

**Settings** (web UI, *Display*): *Language* (English by default, or Nederlands) and *Always sleep*:
the screen stays off and a touch wakes it for 30 s (the first touch only wakes it). *Always sleep*
also works in the normal build. The clothes limits are `DEFAULT_SHORTS_FROM_C` and
`DEFAULT_SWEATER_BELOW_C` in `firmware/include/config.h` (compile-time). The rain animation and the
`OLD` tag are left out in this build; Wifi, location, the web UI and OTA work as before.

```sh
pio run -e esp01_1m_kids -t upload
```

## Hardware

| Part | Notes |
|------|-------|
| ESP-01 (ESP8266, 1 MB flash) | the `esp01_1m` board in PlatformIO |
| SSD1306 128×64 I²C OLED, address `0x3C` | |
| Touch input | a TTP223 module or a push button |
| 3.3 V supply, ≥ 300 mA | the ESP8266 draws current spikes when transmitting |

Wiring (all 3.3 V):

| ESP-01 pin | GPIO | Connects to |
|-----------|------|-------------|
| 0 | GPIO0 | OLED **SDA** |
| 2 | GPIO2 | OLED **SCL** |
| RX | GPIO3 | touch sensor output |

Notes:

- GPIO0 and GPIO2 are boot-strap pins and must be **high at power-up**. The OLED's I²C pull-ups
  normally do that; do not hold either low.
- GPIO3 is the UART RX pin, so **serial output and serial flashing are unavailable while the touch
  sensor is attached**. All diagnostics go to the log in the web UI instead. Disconnect the sensor
  while flashing over serial.
- Touch polarity is set in `firmware/include/config.h`: `TOUCH_ACTIVE_HIGH 0` (default) expects the
  pin to be pulled **low** when touched (button to ground, internal pull-up). Set it to `1` for
  modules such as the TTP223 that drive the pin **high**.

## Build and flash

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

## First boot

1. If there are no (working) WiFi credentials the device starts its own network after about 30 s.
   The OLED shows the network name (**MotoWeather**), its password and `192.168.4.1`.
2. Join that network and open `http://192.168.4.1`. Sign in with user **`admin`** and the password
   from the display. The default password is `moto` plus six hex digits derived from the chip ID.
3. Enter your WiFi network under *WiFi Configuration*, pick your city (or add per-network
   locations), and **set your own admin password**. The device restarts after a password change.
4. Afterwards the page is at `http://motoclock.local`.

If the WiFi connection is lost later, the device keeps retrying on its own and only opens the setup
network again after 5 minutes without a connection.

## How the ride rating works

For each day the forecast is split into a morning (AM) and an evening (PM) ride window. A window is
rated by its **worst hour**:

| Rating | Condition (defaults) |
|--------|----------------------|
| ✗ don't ride | rain over the window > 2.0 mm, or gusts > 60 km/h |
| ! caution | any rain, temperature < 5 °C, gusts > 40 km/h, or a chance of rain of 50 % or more |
| ✓ good | none of the above |

*Today* shows the morning ride until its window is over, then the evening ride. *Tomorrow* shows
tomorrow's morning. The week grid shows both rides for seven days, starting at today. With
`previewHr` set, the main screen shows tomorrow by default from that hour on (a tap then shows today).

### Ride score and best day

Each window also gets a score from 0 to 100: 100 points, minus 3 per degree away from 20 °C, minus 20
per mm of rain, minus 2 per km/h of gusts above 20 km/h. A day scores as its better window, plus 15 on
Saturday and Sunday so that weekend rides are preferred. Windows rated "don't ride" do not count. The
highest-scoring day is highlighted in the week grid.

### Screen power

The panel can be dimmed at night, switched off after some idle minutes at night, and switched off
during fixed quiet hours (for example 23 to 6). A touch wakes it (the first touch only wakes it, and it
then stays on for 30 s even in quiet hours). All of this needs the clock to be synced.

## Configuration reference (`config.json`)

The web UI edits this file; you can also edit it before `uploadfs`. Unknown keys are kept.

| Key | Meaning |
|-----|---------|
| `version` | config layout version (written automatically) |
| `lat`, `lon` | location used for the forecast |
| `manualLocation` | `true` once a city was picked in the web UI (then network locations are ignored) |
| `wifi.ssid`, `wifi.password` | WiFi credentials (the password is never sent back to the browser) |
| `auth.password` | admin and setup-network password, 8–63 characters (set it in the web UI) |
| `thresholds.maxRainMm` | rain over a ride window above which you should not ride |
| `thresholds.maxWindKmh` | gust speed above which you should not ride |
| `thresholds.minTempC` | below this the rating is "caution" |
| `thresholds.warnWindKmh` | gusts above this give "caution" |
| `thresholds.rainProbPct` | a chance of rain from this percentage on gives "caution"; 101 switches it off |
| `wd_am`, `wd_pm` | weekday morning / evening ride window: `[start hour, hours]` |
| `we_am`, `we_pm` | the same for Saturday and Sunday |
| `ssidLocations` | `[{"ssid", "lat", "lon"}]` — use this location when connected to that network |
| `weatherApiUrl` | forecast endpoint, `http://` only (default Open-Meteo) |
| `weatherUnits` | `metric` or `imperial`; imperial only changes the temperature shown on the display |
| `weatherDebug` | log extra detail to the web UI log |
| `previewHr` | from this local hour on, the main screen shows tomorrow's ride by default; 24 = never |
| `display.dimAtNight` | lowest brightness at night |
| `display.alwaysSleep` | the panel stays off; a touch wakes it for 30 s |
| `display.language` | words in the kids build: `en` (default) or `nl` |
| `display.sleepMinutes` | switch the panel off after this many idle minutes at night; 0 = never |
| `display.quietStart`, `display.quietEnd` | quiet hours: panel off from start (inclusive) to end (exclusive), local hours 0-23; -1 = off |

Thresholds are always metric (mm, km/h, °C).

## Updating the firmware over the air

In the web UI, *OTA Update*: choose the `firmware.bin` from `pio run`
(`.pio/build/esp01_1m/firmware.bin`) and upload. The device restarts. CI also publishes it as a
build artifact.

## Security notes

- The web UI, the API and OTA all need the admin password (HTTP Basic). Basic auth is **not
  encrypted**: use a network you trust, and change the default password.
- The ESP8266 is too slow for TLS, so weather requests are plain HTTP. No account, key or
  personal data is sent — only your configured coordinates.
- Writes need a per-boot token, which protects against forged requests from other web pages.
- DNS rebinding is not blocked.

## Project layout

```
firmware/src, include   device code (display, touch, weather, web server, main loop)
firmware/lib/motologic  pure logic, unit tested on the host
firmware/data           files for the LittleFS filesystem (config.json)
test/                   host-side unit tests
tools/                  png_to_bitmap.py — bitmap converter and OLED simulator (see tools/README.md)
plans/                  improvement plan and design notes
```

## Contributing

CI (`.github/workflows/ci.yml`) runs the unit tests, builds both firmware environments and the
filesystem image, and lints the Python tool. See `plans/improvement_plan.md` for the roadmap.
