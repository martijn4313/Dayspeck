# Web UI

Everything about the device is set in its web page: location, WiFi, which screens it shows, the display
options, the limits behind the ride rating and the outfits, and firmware updates. Open `http://dayspeck.local` (or the device's IP address) in a browser
on the same network.

The page asks for a login: user **`admin`** and your admin password. Until you set your own, the
password is the default one shown on the display during WiFi setup (see [first boot](first-boot.md)).
After 10 failed attempts the page locks for a minute.

Each card has its own **Save** button, and a green or red bar at the top of the page tells you whether
it worked. The settings are stored in [`config.json`](configuration.md) on the device.

!!! note
    The screenshots below are renders of the real page with demo data, made with
    `tools/webui_screenshots.py`, not photos of a device.

## Location and the default-password banner

![The location card and the place search with three results for Amsterdam, below a banner that asks for a new password](images/webui-location.png){ .center }

- **Location** shows the place, the coordinates the forecast uses and where they came from: *Manual
  Selection*, *SSID-based*, *Configuration file* or *Default Fallback*. *Show on the map* opens the
  spot on OpenStreetMap, so you can check it.
- **Set Location**: type a town or village and press *Search*. Pick the right one from the list (it shows
  the municipality, province and country, to tell places with the same name apart); that fills in the
  name and coordinates. Press *Set Location* to use it. The device then ignores any
  [per-network location](#per-network-locations).
    - The search runs in your browser, against [Open-Meteo's geocoding](https://open-meteo.com/en/docs/geocoding-api)
      service (free, no account), and covers places worldwide down to villages. Only the place you pick
      is sent to the device.
    - Without internet, for example in the setup network, the search cannot work: type the latitude and
      longitude yourself, e.g. copied from a map app.
    - Coordinates are rounded to 2 decimals, about 1 km. That is finer than the forecast grid (a few km)
      and does not pinpoint your house.
- The yellow banner only shows while the device still has its default password. Set your own in the
  [Admin Password](#admin-password) card.

## WiFi

![WiFi status with a scanned network list, the WiFi configuration form and the SSID location settings](images/webui-wifi.png){ .center }

- **WiFi Status** shows whether the device is connected and the signal strength in dBm (closer to 0 is
  better; around -70 or lower is weak). *Scan Networks* lists the networks in range. Pressing one fills
  in the network name below.
- **WiFi Configuration**: the network name (1-32 characters) and its password (8-63 characters, or empty
  for an open network). The saved password is never sent back to the browser, so the field shows
  `(unchanged)`; leave it empty to keep the current one. *Save WiFi Settings* makes the device connect
  to the network right away.

If the connection is lost the device keeps retrying, and opens its own setup network again after 5
minutes without a connection (see [first boot](first-boot.md)).

### Per-network locations

*SSID Location Settings* ties a location to a WiFi network, so a device that moves between home and the
office shows the local forecast at each. Pick a scanned network (press *Scan Networks* first), enter its
latitude and longitude and press *Add Location*. Up to 10 can be stored; *Delete* removes one. A location
set with *Set Location* takes precedence.

## Ride thresholds, screens, display, clothing and countdowns

![The ride thresholds, screens, display, clothing and countdown cards](images/webui-settings.png){ .center }

**Ride Thresholds** are the limits behind the [ride rating](ride-rating.md). All values are metric. They
only matter when a ride screen is in use.

| Field | Meaning |
|-------|---------|
| Max Rain (mm) | more rain than this over a ride window gives *don't ride* (0-100) |
| Max Wind (km/h) | gusts above this give *don't ride* (0-200) |
| Min Temp (°C) | below this the rating is *caution* (-50 to 50) |
| Warn Wind (km/h) | gusts above this give *caution*; it cannot exceed the maximum wind |
| Caution rain chance (%) | a chance of rain from this value on gives *caution*; 101 switches it off |

**Screens**: which screens the device shows and in which order, see [Your own display](screens.md).

| Field | Meaning |
|-------|---------|
| Preset | fills in both lists below with a [ready-made combination](screens.md#ready-made-combinations), *Rider* or *Kids* (save afterwards) |
| Tap | the screens a tap steps through; the first is the home screen. *Add screen*, move with the arrows, remove with the cross; up to 6 |
| Long press | the screens a long press steps through, then home; empty = a long press works like a tap |
| Back to home after (s) | other screens go back to the home screen after this many seconds; 0 = never |
| Cycle screens every (s) | the device steps through the tap list by itself, this many seconds each; 0 = off (see [without a touch sensor](screens.md#without-a-touch-sensor)) |

**Display** (see also [screen power](screens.md#screen-power)):

| Field | Meaning |
|-------|---------|
| Tomorrow from (hour) | from this local hour the `ride` screen shows tomorrow; 24 = never |
| Dim at night | lower the brightness at night; a touch gives full brightness for 30 s |
| Night brightness (%) | the brightness when dimmed (1-100). Raise it if the screen looks blank at night |
| Sleep at night after (min) | switch the panel off after this many idle minutes at night; 0 = never |
| Always sleep | the screen stays off; a touch wakes it for 30 s (needs the touch sensor) |
| Touch sensor | the sensor on GPIO3 is read. Switch it off when none is connected; switching it on takes effect at once |
| Language | `en` or `nl`: the language of the [weather report](report.md) |
| Screen off from / until (hour) | quiet hours, for example 23 and 6. Set both, or both to -1 for none |

Dimming and sleeping need the clock to be synced, which happens after the first weather update.

**Demo**: *Play demo* shows about a minute of every screen and animation with made-up weather; with
*Repeat* it starts over until *Stop demo* or a touch. See [Demo](screens.md#demo).

**Clothing**: the limits of the outfits on the [picture screens](kids.md), in °C, and the wind limit
for the wind picture and the [weather report](report.md#blown-away). The card shows while the village,
weather, clothes or report screen is in one of the screen lists.

| Field | Meaning |
|-------|---------|
| Sun cap, t-shirt and shorts from | the sun cap on sunny days from this temperature on (default 25) |
| T-shirt and shorts from | shorts from here on (default 20) |
| Sweater below | a sweater below this, a t-shirt above (default 15) |
| Winter coat and hat below | default 5 |
| Scarf and mittens below | default 0 |
| Wind picture above (km/h) | gusts above this show the wind picture in dry weather, and blow the weather report away (default 50) |

**Countdowns**: the [countdown screen](kids.md) to birthdays and holidays. The card shows while the
countdown screen is in one of the screen lists.

| Field | Meaning |
|-------|---------|
| Birthday 1, Birthday 2 | the date of birth (the cake gets a candle for every year) and one letter for the cake; leave the date empty for none |
| Halloween, Sinterklaas, Christmas | count down to 31 October, 5 December (pakjesavond) and 25 December |
| Show from (sleeps before) | how many sleeps before the day the countdown starts, 1-60 (default 14) |

Each limit must be equal to or below the one above it; the page refuses an order that does not go from warm
to cold. A child who feels the cold sooner can get the winter coat from 8 °C instead of 5 °C, for example.

**Weather API Config**: the forecast address (default Open-Meteo), the units (*Metric* or *Imperial*;
imperial only changes the temperature on the display) and *Verbose Debug*, which logs extra detail.
The address must start with `http://`, as the device cannot do TLS.

## Admin password

The *Admin Password* card protects the page, firmware updates and the WiFi setup network. Enter the
current password and the new one twice (8-63 characters). **The device restarts** after a change, and
you sign in again with the new password.

## Debug info and firmware updates

![Debug info with the verbose log open, and the firmware update card offering version 0.3.0](images/webui-update.png){ .center }

- **Debug Info** shows the firmware version, whether the weather data is valid, how old it is and
  whether mDNS (the `dayspeck.local` name) started. *Show Verbose Logs* prints the device log in
  the page. The device has no serial output because the touch sensor uses the RX pin, so this is
  where to look when something does not work.
- **Firmware Update**: *Check now* asks the update server for the latest release. When a newer one is
  out, *Install X.Y.Z* appears; nothing installs by itself. The install takes about a minute and
  settings are kept. The *Update server* address and *Check daily* are described under
  [over-the-air updates](ota.md), which also covers the *Manual upload* form.
