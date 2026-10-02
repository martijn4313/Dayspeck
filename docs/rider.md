# Rider variant

<p align="center"><img src="images/rider-today.png" alt="Three rider screens: good (check mark), caution (exclamation mark, rain, tomorrow) and don't ride (cross, night, strong wind)" width="860"></p>

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
- **Web UI** at `http://weatherwise.local` (or the device IP): location, WiFi, thresholds, logs and
  firmware updates (checked daily, installed with one click; a `UPD` mark shows when one is ready).

The other screens, in order: the week grid, the next hours and the clock.

<p align="center"><img src="images/rider-views.png" alt="Week grid with the best day highlighted, next-hours view and clock" width="860"></p>

Weather data comes from [Open-Meteo](https://open-meteo.com) (free, no account or API key).

*The screenshots in this README are renders of the firmware's drawing code (the host simulator in
[`tools/`](https://github.com/martijn4313/WeatherWise/tree/main/tools/README.md) and a host build of the kids screens), not photos of the device.*
