# Configuration reference (`config.json`)

The web UI edits this file; you can also edit it before `uploadfs`. Unknown keys are kept.

| Key | Meaning |
|-----|---------|
| `version` | config layout version (written automatically) |
| `lat`, `lon` | location used for the forecast |
| `locationName` | name of the place chosen in the web UI (display only) |
| `manualLocation` | `true` once a location was set in the web UI (then network locations are ignored) |
| `wifi.ssid`, `wifi.password` | WiFi credentials (the password is never sent back to the browser) |
| `auth.password` | admin and setup-network password, 8–63 characters (set it in the web UI) |
| `thresholds.maxRainMm` | rain over a ride window above which you should not ride |
| `thresholds.maxWindKmh` | gust speed above which you should not ride |
| `thresholds.minTempC` | below this the rating is "caution" |
| `thresholds.warnWindKmh` | gusts above this give "caution" |
| `thresholds.rainProbPct` | a chance of rain from this percentage on gives "caution"; 101 switches it off |
| `kids.hotFromC`, `kids.shortsFromC`, `kids.sweaterBelowC`, `kids.coatBelowC`, `kids.freezeBelowC` | picture screens: the outfit limits in °C, from warm to cold (defaults 25, 20, 15, 5, 0); each must be equal to or below the one before it, otherwise all of them fall back to the defaults |
| `kids.windyGustKmh` | gusts above this show the wind picture and blow the weather report away (default 50) |
| `kids.birthdays` | countdown: up to two `[{"date": "YYYY-MM-DD", "initial": "E"}]`, the date of birth and the letter on the cake (A-Z, may be empty) |
| `kids.halloween`, `kids.sinterklaas`, `kids.christmas` | countdown: count down to 31 October, 5 December and 25 December (default `true`) |
| `kids.countdownDays` | countdown: shows from this many sleeps before the day, 1-60 (default 14) |
| `wd_am`, `wd_pm` | weekday morning / evening ride window: `[start hour, hours]` |
| `we_am`, `we_pm` | the same for Saturday and Sunday |
| `ssidLocations` | `[{"ssid", "lat", "lon"}]` — use this location when connected to that network |
| `weatherApiUrl` | forecast endpoint, `http://` only (default Open-Meteo) |
| `weatherUnits` | `metric` or `imperial`; imperial only changes the temperature shown on the display |
| `weatherDebug` | log extra detail to the web UI log |
| `previewHr` | from this local hour on, the `ride` screen shows tomorrow (and `rideOther` today); 24 = never |
| `display.dimAtNight` | dim the panel at night |
| `display.nightBrightness` | brightness at night in percent, 1-100 (default 10); raise it if the screen looks blank at night |
| `display.alwaysSleep` | the panel stays off; a touch wakes it for 30 s (ignored when the touch sensor is off) |
| `display.touchEnabled` | the touch sensor on GPIO3 is read (default `true`); set `false` when none is connected |
| `display.screens.tap` | the screens a tap steps through, by [name](screens.md#the-screens); the first is the home screen (not `clock` or `countdown`); 1-6 |
| `display.screens.hold` | the screens a long press steps through, 0-6 (empty: a long press is a tap) |
| `display.screens.returnSeconds` | back to the home screen after this many seconds, 0-3600; 0 = never (default 30) |
| `display.cycleSeconds` | step through the tap list by itself every this many seconds, 2-3600; 0 = off (default) |
| `display.language` | `en` (default) or `nl`: the language of the weather report screen |
| `display.sleepMinutes` | switch the panel off after this many idle minutes at night; 0 = never |
| `display.quietStart`, `display.quietEnd` | quiet hours: panel off from start (inclusive) to end (exclusive), local hours 0-23; -1 = off |
| `ota.url` | update server (the relay), `http://` only; default `OTA_DEFAULT_URL` in `config.h` |
| `ota.autoCheck` | check for a new release once a day (default `true`) |

Thresholds are always metric (mm, km/h, °C).
