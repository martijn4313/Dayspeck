# Configuration reference (`config.json`)

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
| `display.dimAtNight` | dim the panel at night |
| `display.nightBrightness` | brightness at night in percent, 1-100 (default 10); raise it if the screen looks blank at night |
| `display.alwaysSleep` | the panel stays off; a touch wakes it for 30 s |
| `display.language` | `en` (default) or `nl`; currently without visible effect (the kids screens show no words) |
| `display.sleepMinutes` | switch the panel off after this many idle minutes at night; 0 = never |
| `display.quietStart`, `display.quietEnd` | quiet hours: panel off from start (inclusive) to end (exclusive), local hours 0-23; -1 = off |
| `ota.url` | update server (the relay), `http://` only; default `OTA_DEFAULT_URL` in `config.h` |
| `ota.autoCheck` | check for a new release once a day (default `true`) |

Thresholds are always metric (mm, km/h, °C).
