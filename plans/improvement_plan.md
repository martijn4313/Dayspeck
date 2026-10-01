# MotoClock Improvement Plan

Source: full code review of the firmware, web UI, tooling and docs (2026-09).
This plan supersedes `plans/todo.md`. Several items there are already done or
out of date, such as "corrupted weather.cpp" and "missing arrow bitmaps".

Status legend: `[ ]` open · `[x]` done · **P0** broken or unsafe · **P1** important · **P2** nice to have

The review was static: it was not compiled or run on hardware. Line numbers refer
to the code at commit `15f953d`.

---

## Phase 1: Fix what is broken now (P0)

> **Status:** implemented on branch `ccr-9c0b59d9-v315pr`. `pio run` and `pio run -t buildfs` succeed
> (flash 74 %, static RAM 47 %), but it has **not been run on hardware** yet. Run the hardware
> checklist before merging. Notes: a short tap in the weekly view now closes it;
> imperial mode only changes the temperature label; the "weekly view" auto-closes after 30 s.

### 1.1 Long press is never detected
- **Where:** `firmware/src/main.cpp` in `loop()` and `firmware/src/touch.cpp`
- **Problem:** `touch_short_tap()` and `touch_long_press()` each call
  `touch_get_event()`, which consumes the event. The short-tap check runs
  first and throws away `TOUCH_LONG`.
- **Fix:** read the event once (`int ev = touch_get_event();`) and `switch`
  on it. Remove or rewrite the convenience helpers so they do not consume
  events.
- **Done when:** a long press toggles the weekly view and a short tap toggles
  today/tomorrow.

### 1.2 A single WiFi drop leaves the device in AP-only mode
- **Where:** `main.cpp` loop, section 3
- **Problem:** `lastWifiAttemptMs` is set only in `setup()`. Any disconnect
  after 30 s of uptime immediately runs `WiFi.mode(WIFI_AP)`, and nothing
  switches back to STA. After a router reboot the clock stays offline until
  it is power-cycled.
- **Fix:**
  - [x] Track `everConnected` and the time of the last disconnect. Start the AP
        only if the device has never connected, or after a long outage.
  - [x] Use `WIFI_AP_STA` so the device keeps retrying STA while the AP is up.
  - [x] Call `WiFi.setAutoReconnect(true)` and `WiFi.persistent(false)`.
  - [x] Stop the AP again once STA reconnects.
  - [x] Reconnect after new credentials are saved through `/api/wifi/config`.
- **Done when:** unplugging the router for 2 minutes lets the device reconnect
  on its own.

### 1.3 No time source, and day/night detection is broken
- **Where:** `main.cpp: updateDayNight()`, `weather.cpp: fetchWeather()`
- **Problems:**
  - Nothing calls `configTime()` or sets up NTP, so `time(nullptr)` is meaningless.
  - The URL does not request `daily=sunrise,sunset`.
  - Open-Meteo returns ISO strings by default, and `.as<time_t>()` turns them into 0.
- **Fix:**
  - [x] Call `configTime(0, 0, "pool.ntp.org", "time.google.com")` once WiFi
        connects, and set `timeSynced` from `time(nullptr) > 1'600'000'000`.
  - [x] Request `daily=sunrise,sunset` with `timeformat=unixtime`, and store
        `utc_offset_seconds` for local-time calculations.
  - [x] Base the fallback night rule on local time only when `timeSynced`
        is true. Otherwise default to "day".
- **Done when:** the moon or night overlay appears after the real sunset
  for the configured location.

### 1.4 Weekly forecast and ratings are fake
- **Where:** `weather.cpp: updateWeeklyState()`, `getTodayRating()`, `getTomorrowRating()`
- **Problems:**
  - AM and PM are computed from identical inputs, so they are always equal.
  - Gust is estimated as `wind*0.7`, which understates it.
  - The function uses `API_BASE_URL` directly and ignores the configured URL and units.
  - It makes a second HTTP request each cycle.
  - The hourly data is fetched but never parsed.
  - `trend` is hard-coded to `'f'`.
  - "Today" always shows the AM rating, even in the evening.
- **Fix:**
  - [x] Merge everything into **one** request: `current=…`,
        `hourly=temperature_2m,precipitation,precipitation_probability,wind_speed_10m,wind_gusts_10m`,
        `daily=sunrise,sunset,wind_gusts_10m_max`, `forecast_days=7`, `timeformat=unixtime`.
  - [x] Rate each AM/PM window from the hourly values inside it: the maximum
        gust, the precipitation sum, and the minimum temperature. Use the
        existing `wd_am`, `wd_pm`, `we_am` and `we_pm` settings (start hour,
        duration) from `config.json`. They are in the file but never read.
  - [x] Show the next upcoming window for "today" and the first window of the
        next day for "tomorrow".
  - [x] Compute `trend` from the hourly temperature over the next 3 hours
        (±1 °C gives up or down, otherwise flat).
  - [x] Delete `updateWeeklyState()`'s separate fetch.
- **Done when:** AM and PM can differ, the ratings match a manual check
  against the Open-Meteo website, and each cycle makes one HTTP request.

### 1.5 The weekly view is blank and never refreshes
- **Where:** `display.cpp: renderWeeklyMatrix()` (stub), `main.cpp` render block
- **Fix:**
  - [x] Implement the grid: day letters starting from the current weekday,
        AM/PM row labels, and a small ✓ / ! / X glyph in each cell.
  - [x] Replace the special case in `loop()` with a single
        `render()` dispatch on `displayMode`, so both views redraw when
        `displayDirty` is set.
  - [x] Return to the primary view automatically after about 30 s.

### 1.6 Primary view is incomplete
- [x] Add a moon bitmap (`moon_bmp`) and draw it at night. There is currently a TODO in `renderSkylineCard`.
- [x] Show wind speed text and a cloud or rain icon in `renderBottomCard`.
- [x] Use the correct unit label (`C`/`F`). The label is hard-coded to `C`.

### 1.7 Smaller logic bugs
- [x] **Cache-Control is never read.** Call
      `http.collectHeaders(...)` before `GET()`, or remove the dead code.
- [x] **`intervalPassed()`**: remove the `if (now < lastRun) return true;`
      branch. Unsigned subtraction already handles rollover. Replace the
      `millis() - interval` first-fetch trick with an explicit `fetchNow` flag.
- [x] **Unseeded `random()`**: call `randomSeed(ESP.getChipId() ^ micros())`
      in `setup()`.
- [x] **Weather code mapping**: handle 1–3 (cloudy), 45/48 (fog) and 85/86
      (snow showers). Apply the wind override after code 0 as well. Keep the
      previous condition only when the field is missing, and log that case.
- [x] **Units**: the API is now always requested in metric (thresholds are metric);
      the display converts temperature to °F for `weatherUnits: "imperial"`.
- [x] **`manualConfigPresent`** is set whenever `config.json` parses. It
      should mean "the user chose a location". Fix the location-source label.
- [x] Remove the dead `loadThresholds()`. If called, it would overwrite
      the loaded config with defaults.
- [x] Remove `setDisplayMode()` and `getDisplayMode()` from `weather.cpp`.
      They duplicate `state.displayMode` and are unused.
- [x] `geolocation.cpp`: was dead code. Deleted together with the Google geolocation feature
      (it needed HTTPS, which the device cannot afford; see Phase 2).

### 1.8 Duplicated struct definitions (ODR violations)
- **Where:** `webserver.cpp` defines its own `struct SystemState` with a
  different layout, plus `extern SystemState state;` (the one in main.cpp is
  `static`). `SsidLocation` is also defined twice.
- **Fix:**
  - [x] Create `include/app_state.h` with the shared types.
  - [x] Replace the mirrored extern globals (`wifiConnected`, `weatherValid`,
        `weatherAge`, `mdnsStarted`, …) with one `const SystemState& getState()` accessor.
  - [x] Create `include/settings.h`: a `Settings` struct (location, thresholds, API, wifi,
        ssidLocations, windows) with `load()` and `save()`. This removes about
        15 externs.

---

## Phase 2: Security (P0)

> **Status:** implemented on branch `ccr-9c0b59d9-v315pr`; builds, **not run on hardware**. Deviations from the original text, all deliberate:
> - **Login:** HTTP Basic, user `admin`; the password is the device password below. 10 failed
>   attempts lock the web UI for 60 s.
> - **Device password:** `moto` + 6 hex digits of the chip ID until the user sets their own (8-63
>   chars, via the new "Admin Password" card; the device restarts afterwards). The same password
>   protects the setup access point, the web UI and OTA. While the setup AP is active the OLED
>   shows the AP name, the password and the IP. The chip ID is derivable from the MAC address, so
>   this is a per-device default, not a secret: the web UI nags until it is changed.
> - **CSRF:** instead of a fixed `X-MotoClock: 1` header (which a cross-site *form* post cannot send,
>   but is guessable), every POST must carry a random per-boot token in `X-MotoClock`, readable only
>   by same-origin script via `GET /api/token`. Writes also reject non-POST requests with 405.
> - **OTA path:** `/update-<token>` (token changes every boot) behind the password, so a cross-site
>   form post cannot reach it.
> - **`data/config.json`** is kept, with empty WiFi credentials. To avoid committing your own, run
>   `git update-index --skip-worktree firmware/data/config.json`.
> - **No HTTPS, no API key, no geolocation:** the ESP8266 is too slow and too short on RAM for TLS
>   (flash dropped from 74 % to 60 % and the heap guard no longer needs a TLS case when it was removed),
>   and Open-Meteo's public data needs no key. The `apikey` setting, the Google geolocation feature
>   (HTTPS-only) and the certificate-verification item were therefore removed. An `https://` API URL
>   in an old config is downgraded to `http://`; the web UI only accepts `http://`.

- [x] **Stop leaking secrets:** remove `wifi.password` and `weatherApi.key`
      from `/api/status`. Return `"passwordSet": true/false` instead.
- [x] **Authentication:** protect all `/api/*` write endpoints and `/update`
      with `server.authenticate(user, pass)`. Set the admin password in the
      web UI and store it in config.
- [x] **OTA:** use `httpUpdater.setup(&server, "/update", user, pass)`.
- [x] **AP password:** derive a password per device (for example from the
      chip ID) instead of the hard-coded `password123`, and show it on the
      OLED while in AP mode. Optionally add a captive portal with
      `DNSServer`.
- [x] **XSS:** use `textContent` or DOM nodes instead of `innerHTML` for
      SSIDs, logs and SSID-location lists (web UI JS, around lines 398, 477
      and 506). A neighbour's AP name must not be able to run script.
- [x] **CSRF:** require a custom header (for example `X-MotoClock: 1`) on POSTs.
      Browsers cannot send it cross-origin without a preflight.
- [x] **Input validation:** check lat ∈ [-90, 90], lon ∈ [-180, 180],
      thresholds within sane ranges, SSID ≤ 32 chars, a maximum of about 10
      SSID locations. Return 400 with a message on error.
- [x] **Secrets in the repo:**
  - [x] Move `WIFI_SSID` and `WIFI_PASS` to a
        git-ignored `include/secrets.h`, and commit a `secrets.h.example`.
  - [x] Rename `data/config.json` to `data/config.example.json` and git-ignore
        the real one. Alternatively, keep it with empty credentials only.
- [x] **API key transport:** resolved by removing the API key and HTTPS altogether (see the status note).
- [ ] Known limits to document in the README: Basic auth is unencrypted on the LAN (use a trusted
      network), and DNS rebinding is not blocked (consider checking the `Host` header against the
      device IP or `motoclock.local`).
- [x] Privacy impact of Google geolocation: the feature was removed.

---

## Phase 3: Robustness and performance (P1)

### Network and memory
- [x] Call `http.setTimeout(5000)` (set to 8 s), and show a small "updating" indicator
      during the fetch, because the fetch blocks the loop.
- [x] Stream-parse with an ArduinoJson **filter**
      (`DeserializationOption::Filter`) directly from `http.getStream()`
      instead of `getString()`.
- [x] Before each fetch, log free heap and max free block. Skip the fetch
      and log it if the heap is below about 12 KB.
- [x] Move `locationsJson` to `PROGMEM` (currently about 2 KB of RAM).
- [x] `handleApiLogs`: stream logs into the JSON array directly instead of
      using a 2 KB stack buffer plus `strtok`.
- [x] Resolve the SSID location only on the connect event, not on every
      `loop()` iteration (`WiFi.SSID()` allocates a String each time).
- [x] Run `updateDayNight()` about once per second, not every loop.

### Data freshness
- [x] Mark weather as stale after 2× the fetch interval, and show a stale marker on the OLED
      (an "OLD" tag in the top-left corner).
- [x] Back off exponentially on failures (1, 2, 4, 8 minutes, capped at
      15) instead of retrying every minute forever.
- [x] Fix the night-time fetch interval so it depends on the fixed `isNight`
      value from 1.3.

### Display
- [x] Call `Wire.setClock(400000)`. At 100 kHz a full frame takes about 90 ms,
      which exceeds the 66 ms frame budget.
- [x] Remove the double `display.display()` (`renderPrimaryView` and
      `renderDisplay` both flush).
- [x] Replace the per-pixel float `sin`/`cos` circles in `drawGiantBadge` and
      `renderLoadingView` with `drawCircle` or precomputed tables.
- [x] Night mode: `display.dim(true)` at night (done). Optionally blank the screen after N minutes without a touch. This is a
      bedside device.
- [ ] Shift the layout by a pixel now and then to reduce OLED burn-in. (Deferred: the SSD1306
      display-offset command wraps a row to the opposite edge, so it needs a proper layout offset in the
      renderers.)

### Config persistence
- [x] Replace the six copy-pasted read-modify-write blocks in `webserver.cpp`
      with a single `Settings::save()`. (Done as an `updateConfig()` helper in
      `webserver.cpp`; a shared `Settings` module is still open.)
- [x] Write atomically: write `/config.tmp`, then
      `LittleFS.rename()` it over `/config.json`.
- [x] If `config.json` fails to parse, log it and keep the defaults. A save never destroys the
      broken file: it is moved to `config.bad` first.
- [x] Add a `version` field to config for future migrations.

### Web server
- [x] Return 405 for wrong methods. Currently handlers send nothing and the
      client hangs.
- [x] Return 500 when a file write fails, and a JSON body on success.
- [ ] Serve the UI from LittleFS (`data/index.html.gz`) instead of a
      roughly 400-line string in C++. Add cache headers.
- [ ] Add a config export and import endpoint.

### Debugging
- [x] `Serial` is never started, and GPIO3/RX is the touch pin. Remove the
      `Serial.print` debug paths, or put them behind a build flag that
      disables touch. Route all diagnostics through `logMessage()` and
      `/api/logs`.
- [x] Record the reset reason (`ESP.getResetReason()`) in the log at boot.
- [x] Move `DISPLAY_STATUS_DEBUG` out of `config.h` into a debug build env.

---

## Phase 4: Build, tests, CI and repo hygiene (P1)

> **Status:** done except where noted. Verified locally: `pio test -e native` (14 passed),
> `pio run` with `-Wall -Wextra` (no warnings), `pio run -e esp01_1m_debug`, `pio run -t buildfs`,
> `ruff check tools --select E9,F63,F7,F82`. The GitHub Actions workflow itself has not run yet.
> Library detail: PlatformIO does not scan headers in `firmware/include`, so `main.cpp` includes
> `motologic.h` directly to get the library linked.

### Build
- [x] `platformio.ini` (`--no-stub` kept: it is needed for reliable flashing of some ESP-01 boards):
  - [x] Pin the platform (`platform = espressif8266@4.2.1`).
  - [x] Remove the framework-bundled libraries from `lib_deps`
        (`ESP8266WebServer`, `ESP8266mDNS`, `ESP8266HTTPUpdateServer`).
  - [x] Add `-Wall -Wextra` to `build_flags`.
  - [x] Add an `[env:esp01_1m_debug]` with the debug flags.
  - [x] Checked `upload_flags = --no-stub`: kept.
- [x] Add `firmware/include/version.h` (or a build flag with the git hash),
      and show the version in the web UI and `/api/status`.
- [x] Make `bitmaps.h` self-contained (`#include <Arduino.h>` / `<pgmspace.h>`),
      and add a "generated by png_to_bitmap.py, do not edit" header.

### Tests
- [x] Add `[env:native]` and move pure logic into an Arduino-free library
      (`firmware/lib/motologic`): ride rating (single and per window), WMO code mapping, trend,
      day/night with day roll-forward, day of week, rollover-safe interval check.
- [x] Unity tests in `test/test_logic` (14 cases, `pio test -e native`).
- [ ] Forecast parsing from a saved Open-Meteo JSON fixture (`parseForecast` still depends on
      ArduinoJson and globals; split it so it can run natively).
- [ ] The web handlers' validation helpers (`argFloat`, `validUrl`) are not unit tested yet.

### CI
- [x] Add `.github/workflows/ci.yml` to run `pio run`, `pio test -e native`
      and `pio run -t buildfs` on every push and PR, and to upload
      `firmware.bin` as an artifact.
- [x] Add a Python lint job (`ruff`) for `tools/`.

### Repo hygiene
- [x] Delete the committed `tools/__pycache__/` and add `__pycache__/` and
      `*.pyc` to `.gitignore`.
- [x] Remove the duplicate `firmware/.gitignore`. It is identical to the
      root one.
- [x] Archived (moved to `plans/archive/`) `codebase_analysis.md`, `codebase_analysis_grok.md` and
      `plans/todo.md`. They describe problems that no longer exist.
- [x] (Details in Phase 4b.) Split the 1,866-line `tools/png_to_bitmap.py` into `converter.py` (pure,
      with a CLI mode for CI or regeneration) and `gui.py`. Fix `requirements.txt`:
      `customtkinter` is listed but plain tkinter is used.
- [ ] Generate the shared layout constants (`SKYLINE_X`, …) from one source,
      so that `display.h` and the Python simulator cannot drift apart.

---

## Phase 4b: Python bitmap tool (`tools/png_to_bitmap.py`) (P1/P2)

> **Status:** sections A-E are implemented; the tool is now the package `tools/bitmaptool` (with
> `tools/png_to_bitmap.py` as launcher). Verified: `pytest tools/tests` (21 passed), the GUI starts and
> switches views and colour schemes under a virtual display (xvfb), `regen --check` passes, ruff is clean.
> The pixel-editor dialog and the animation timer were not exercised interactively.
> Found and fixed in the firmware while syncing the simulator: the badge check mark was a thin, barely
> recognisable stroke (now a 3 px check), and the wind effect drew only 12 pixels (hidden under the
> temperature box) with no gaps (now repeats across the card with gaps).

Review of the 1,866-line tool: a Tk GUI that converts PNGs to `PROGMEM` arrays and simulates
the 128×64 OLED (rain animation, badge, weekly and "time" views). It compiles, but I could not run
it headless (no `tkinter`/Pillow in the review sandbox), so the items below come from reading it.

### A. The simulator no longer matches the firmware (biggest problem)
The point of the simulator is to design and verify before flashing. It has drifted:
- [x] **Weekly view:** the firmware now draws a glyph grid (`renderWeeklyMatrix`, 16 px columns,
      day letters starting at today, AM/PM rows). The tool uses `cell_*` bitmaps (14×20) that the
      firmware never loads. Make the tool mirror the firmware layout, and drop the `cell_*` slots.
- [x] **Bottom card:** the firmware draws a procedural cloud, rain, snow, wind or sun icon plus
      `NNkm/h` and `N.Nmm` text. The tool blits `cloud_bmp` or `rain_cloud_bmp` and draws precipitation
      bars. Port the firmware's version, or move this card to shared data (see D).
- [x] **Badge:** `Procedural.draw_giant_badge` uses different geometry than `drawGiantBadge`
      (for example, the "!" bar is 8 px wide in Python and 3 px in the firmware, and the check
      coordinates differ). Make one the reference and copy it.
- [x] **Rain constants:** `MAX_RAIN_DROPS = 40` and `MAX_SPLASHES = 30` in the tool, versus 16 and
      8 in `display.h`. The preview shows far more rain than the device can.
- [x] **"Time" view** (`VIEW_TIME`, large digits) exists only in the tool. Either implement it in
      the firmware (needs the NTP time from Phase 1) or remove it.
- [x] **Asset slots do not match reality:** `moon` is listed as 8×8 but `bitmaps.h` has 10×10;
      `cloud`, `rain_cloud`, `rain_drop_*`, `splash_*` and `cell_*` have no arrays in `bitmaps.h`
      (the firmware falls back to procedural drawing through `#ifdef RAIN_DROP_1_BMP_W`). Generate
      the slot list from the firmware or `bitmaps.h`, and mark each slot as "used by firmware" or
      "unused".
- [ ] Add a **fidelity test**: render a fixed scene in Python and compare it to a PNG or hex dump
      produced by the firmware (see the native test environment in Phase 4). That makes drift fail
      CI instead of surprising someone later.

### B. Conversion correctness
- [x] **Transparency:** `load_png` does `.convert("L")`, which discards alpha. Transparent pixels
      take whatever RGB sits under them, usually black. Composite onto black first (or treat alpha < 128
      as off) and document which one is "ink".
- [x] **Polarity:** add an **Invert** option and a threshold slider. Dark-on-white art currently
      converts to the opposite of what was drawn. Show the 1-bit result next to the source before saving.
- [x] **Size check:** warn when the PNG's size differs from the slot's size (for example a 9×10
      image for a 10×10 slot), instead of silently emitting whatever size it was.
- [x] **Speed:** replace per-pixel `getpixel` loops with `img.tobytes()` or `numpy`-free
      `img.point()` plus `Image.getdata()`.
- [x] **Close files:** use `with Image.open(...)`.
- [x] **Naming:** `to_c_array` appends `_bmp` and `_BMP_W/_H`. Reject or sanitise names that are not
      valid C identifiers and name clashes with existing arrays.

### C. Safer `bitmaps.h` round trip
- [x] **Writing the header:** "Save" removes the old definition with a regex and appends the new one
      at the end of the file, so ordering and comments are lost, and a new file has no include guard
      or includes. Regenerate the whole file from a manifest (name → PNG) in a fixed order, with a
      "generated by tools/png_to_bitmap.py, do not edit" banner, `#ifndef BITMAPS_H`, and
      `#include <Arduino.h>` / `<pgmspace.h>` (which also fixes the include-order dependency noted in Phase 4).
- [x] **Source of truth:** keep the PNGs in `assets/` (committed) and treat `bitmaps.h` as build
      output. At the moment the only record of the art is the header, and hand-edits in the pixel
      editor cannot be reproduced.
- [x] **Parser robustness:** `parse_bitmaps_h` requires the two `#define`s to follow the array
      directly, prints warnings with `print` and skips arrays with a wrong byte count. Return the
      problems to the UI and tolerate comments and different ordering.
- [x] Make "Save" write atomically (temp file then rename) and keep a `.bak` copy.
- [x] Add a **round-trip test**: parse the committed `bitmaps.h`, re-emit it, and assert the output is
      byte-identical.

### D. Structure and maintainability
- [x] Split the single file (already planned in Phase 4 hygiene) into:
      `tools/bitmaptool/convert.py` (PNG ⇄ C array, pure), `canvas.py` (`OLEDCanvas`, drawing),
      `scene.py` (`SceneComposer`, rain, procedural), `gui.py` (Tk). The first three need no `tkinter`
      or display so they can run in CI.
- [x] Add a CLI: `python -m bitmaptool convert in.png --name sun --out bitmaps.h`,
      `... regen --manifest assets/manifest.json`, and `... render --scene rain --out preview.png`
      (headless PNG preview). Use it from CI and from a `pre-commit` check that the header is up to date.
- [ ] **Shared constants:** `SKYLINE_X`, `HORIZON_Y`, `RAIN_FRAME_INTERVAL`, the colour of each region and
      so on are duplicated in `display.h` and the tool. Generate one from the other (a small
      `layout.json` that both a generated `layout.h` and the Python import).
- [x] Remove module-level mutable colours (`global OLED_ON, OLED_OFF` changed from `_on_scheme_change`);
      pass a theme to the canvas renderer instead.
- [x] Move the stray `import re` inside `_on_save_h` and the imports placed after the colour constants to
      the top of the file; enable `ruff` (planned in CI) to catch these.
- [ ] Use `after_cancel` on window close for the rain-animation timer; guard `_tick_rain` so it stops when
      the window is destroyed.
- [ ] Add type hints and docstrings for the public functions; require Python ≥ 3.9 (it uses
      `list[list[bool]]`) and say so in the README.

### E. Packaging and hygiene
- [x] `requirements.txt` lists `customtkinter`, but the tool never imports it. Remove it, or port the GUI.
- [x] Delete `tools/__pycache__/` (two `.pyc` files, 233 KB, are committed) and ignore it.
- [ ] `plans/png_to_bitmap_architecture.md` describes the original design; update it after the split
      (`requirements.txt` there lists only Pillow, which is correct).
- [x] Add a short `tools/README.md`: install, run, the asset workflow (PNG → header → build), and how to
      add a new sprite end to end.
- [x] Add pytest tests for `convert.py` (threshold, padding for widths that are not a multiple of 8,
      MSB-first bit order, the round trip) and a smoke test that imports the scene code headless.

### F. Nice to have
- [ ] Live **firmware preview of the real C++ renderer**: build `display.cpp` natively with a tiny
      Adafruit_GFX shim and dump the framebuffer to PNG. That removes the Python re-implementation and the
      drift problem entirely (the best long-term fix for section A).
- [ ] Onion-skin and frame stepping for the rain animation, and an export of the animation to a GIF for
      the README.
- [ ] Zoom and grid options, and keyboard shortcuts in the pixel editor (the editor already has undo and
      redo).
- [ ] A dark and light theme for the tool UI (colours are currently hard-coded as hex strings).

## Phase 5: Documentation (P1)

> **Status:** `README.md` rewritten. The "Python tool" README is part of Phase 4b. Touch polarity
> defaults to the original behaviour (active low); set `TOUCH_ACTIVE_HIGH 1` for TTP223 modules.

- [x] Expand `README.md` with:
  - [x] Photo or render, and a feature list.
  - [x] Bill of materials: ESP-01, SSD1306 128×64 I²C, touch module (type and
        polarity), 3.3 V regulator.
  - [x] Wiring diagram: SDA=GPIO0, SCL=GPIO2, touch=GPIO3 (RX). Note the
        GPIO0/2 boot-strap pull-up requirements, and that serial is
        unavailable while touch is connected.
  - [x] Build and flash steps: `pio run -t upload`, `pio run -t uploadfs`.
  - [x] First boot: AP name and password, and the web UI at `motoclock.local`.
  - [x] Config reference for every `config.json` key.
  - [x] How the ride rating works (thresholds, windows).
  - [x] OTA update procedure.
- [x] Make touch polarity configurable (`TOUCH_ACTIVE_LOW`). Common TTP223
      modules are active-high.

---

## Phase 6: Features (P2, after phases 1–5)

> **Status:** implemented; firmware builds, 20 host tests and 25 tool tests pass, but nothing has run on
> hardware. Design decisions:
> - **Score:** per ride window, from its hourly values (average temperature, total rain, strongest gust);
>   a day is its better window plus 15 on weekends (so it can exceed 100); windows rated X or without data
>   do not count. Best day = highest score, earliest on ties.
> - **Rain chance:** `thresholds.rainProbPct` (default 50, 101 = off) makes a window at least "caution".
>   This makes default ratings more cautious than before. The forecast request now includes
>   `precipitation_probability`.
> - **Next hours / best time to leave:** the next 24 hours are kept in 5 bytes each (120 bytes). The advice
>   is the best 2 hour daytime (06:00-20:00 local start) window in the next 12 hours.
> - **Navigation:** long press cycles main -> week -> next hours; tap leaves a detail view (so "view on tap"
>   from the original list became "view on long press").
> - **Status marks:** WiFi bars and the `TMR` tag are on the main screen; "last updated HH:MM" is in the
>   next-hours footer (there was no room on the main screen).
> - **Screen power:** dim at night, optional sleep after idle minutes at night, optional quiet hours; the
>   first touch only wakes the panel. Needs NTP.

- [x] Ride score (0–100) as described in `plans/memo_09042026.md`: a baseline of 100,
      minus penalties for deviation from 20 °C, rain, and wind above 20 km/h, plus a
      weekend bonus. Highlight the best day in the weekly view.
- [x] Rain thresholds based on precipitation probability, not only mm.
- [x] A "best time to leave" hint from the hourly data.
- [x] A third view on tap: a strip showing the next 6 hours.
- [x] "Last updated HH:MM" and a WiFi signal icon on the OLED.
- [x] Configurable brightness schedule and auto-off.
- [x] Use the preview hour: after `previewHr`, default the display to
      tomorrow. The setting is in `config.json` but never read.

---

## Follow-ups after the first release

- [x] Rain drop and splash sprites: four variants each (`rain_drop_1..4`, `splash_1..4`), added to
      `tools/assets` and `bitmaps.h`. The sprite path in `display.cpp` is now compiled in and used.
      (Five variants were considered and rejected: four is enough.)
- [x] Next-hours view: label column (`h`, `°C`, `mm`, `%`, `kmh`) so the rows are self-explanatory.
- [x] Clock screen: time and date (fourth long-press screen; stays until tapped). Needs NTP and the UTC
      offset from the first forecast.
- [x] Rain splashes landed up to 9 px below the horizon, inside the bottom card and over its text. The
      ground level is now 38-41, above the divider (firmware and simulator; found while making the leaflet).
- [x] Wind: removed the dashed lines in the sky (they collided with the temperature box, skyline and moon
      and duplicated the icon) and redrew the bottom-card wind icon as three gusts of different lengths with
      curls, leaving a gap before the speed text. The `Wind Effect` entry is gone from the tool's GUI.
- [ ] Remember the UTC offset across reboots (it is only learned from the first forecast, so the clock
      shows "Time not set yet" until then).
- [ ] The clock is a static image for long periods: the sleep timer and quiet hours cover OLED burn-in, but a
      small periodic position shift would help.
- [ ] Hardware check: sprite drops and splashes look right on the real panel; the clock colon blink does
      not flicker.

---

## Suggested commit or PR sequence

| # | Scope | Items |
|---|-------|-------|
| 1 | Touch and WiFi reliability | 1.1, 1.2, `randomSeed`, `intervalPassed` |
| 2 | Time and forecast | 1.3, 1.4, weather-code mapping, units, single request plus filter |
| 3 | Display completion | 1.5, 1.6, render dispatch, I²C clock, double flush |
| 4 | Shared state and settings refactor | 1.8, atomic config writes, dead code removal |
| 5 | Security hardening | Phase 2 |
| 6 | CI and native tests | Phase 4 |
| 7 | Python tool: sync with firmware, fix conversion, split and test | Phase 4b A–D |
| 8 | Docs and repo cleanup | Phase 4 hygiene, Phase 4b E, Phase 5 |
| 9+ | Features | Phase 6, Phase 4b F |

Each PR should build cleanly with `pio run`. From PR 6 onward, CI should be
green before merging.

## Test checklist on hardware (every release)

- [ ] Cold boot with no config: AP mode starts, and the password is shown on the OLED.
- [ ] Configure WiFi through the web UI: the device reconnects without a power cycle.
- [ ] Router off for 2 minutes, then on: the device recovers by itself.
- [ ] Short tap toggles today/tomorrow. Long press toggles the weekly grid.
- [ ] The rain animation runs smoothly and the web UI stays responsive.
- [ ] Day/night switches at the real sunset.
- [ ] OTA update works with credentials and is rejected without them.
- [ ] Free heap stays stable over 24 hours (check `/api/logs`).
- [ ] Long press cycles main, week, next hours; the detail views close after 30 s.
- [ ] The best day is highlighted in the week grid and matches a manual check of the forecast.
- [ ] The next-hours view matches Open-Meteo for the same hours; the "Best HH:00" advice is sensible.
- [ ] Quiet hours switch the panel off and a touch wakes it for 30 s; sleep-at-night works.
- [ ] After `previewHr` the main screen shows tomorrow (`TMR` tag); a tap shows today.
