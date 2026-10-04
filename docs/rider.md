# Rider variant

![Three rider screens: good (check mark, sun), caution (exclamation mark, rain, tomorrow) and don't ride (cross, night, wind and autumn leaves)](images/rider-today.png){ .center }

- **Left half:** a big ride badge — ✓ good, ! caution, X don't ride.
- **Right half:** a little village with temperature and trend arrow and the sun or moon above it. The
  street lamp is lit at night. The weather is shown in the scene itself: rain falls and splashes on the
  pavement, snow drifts, and wind blows (see below). Wind speed and precipitation are written on the street
  at the bottom.
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
- **Web UI** at `http://dayspeck.local` (or the device IP): location, WiFi, thresholds, logs and
  firmware updates (checked daily, installed with one click; a `UPD` mark shows when one is ready).

## Without a touch sensor

The touch sensor is optional. In the web UI under *Display* (or with `display.touchEnabled` in
`config.json`) it can be switched off, and with *Cycle screens every (s)* (`display.cycleSeconds`) the device
shows its screens in turn by itself, each for that many seconds: the main view, the other day (today or
tomorrow), the week grid, the next hours and the clock (once the time is known). A touch sensor keeps working
while it cycles: a tap picks a screen and it stays for a full cycle time. While the screens cycle, the week and
hours views do not close after 30 s, because every screen already stays for the cycle time.

*Always sleep* needs the touch sensor, because it is the only way to wake a switched-off screen; the quiet
hours and the night sleep timer work without one.

## Wind and autumn leaves

When it is windy (gusts above the wind threshold, and dry) curled gusts sweep across the village from left
to right, faster and more of them the harder it blows.

![An animation of the rider screen on a windy autumn day: gusts and leaves blow across the village](images/rider-wind.gif){ .center }

In autumn leaves tumble along with the wind: from a wind speed of 20 km/h on, even on a day that is not
windy enough for gusts, as long as it is not raining or snowing. Autumn is September to November, and March
to May if the location is in the southern hemisphere. It needs the network time and the time zone, so the
leaves only appear once the clock is synced.

## The other screens

The other screens, in order: the week grid, the next hours and the clock.

![Week grid with the best day highlighted, next-hours view and clock](images/rider-views.png){ .center }

Weather data comes from [Open-Meteo](https://open-meteo.com) (free, no account or API key).

*The screenshots in this manual are renders of the firmware's drawing code (the host simulator in
[`tools/`](https://github.com/martijn4313/Dayspeck/tree/main/tools/README.md) and a host build of the kids screens), not photos of the device.*
