# Screens

There is one firmware with all screens. Which of them the device shows, and in which order, you choose in
the web UI under **Screens** (or in `config.json`, see below). So a household can have the kids' weather as
the home screen and the ride rating one long press away, or only the clock and the week grid.

![The Screens card: a preset choice, the tap list (kids weather, clothes, countdown), the long-press list (ride rating, week grid, clock), back to home after 30 s and cycle off](images/webui-screens.png){ .center }

## The screens

| Name | Screen |
|------|--------|
| `ride` | [Ride rating](rider.md) of today (tomorrow from the *Tomorrow from (hour)* setting on), with the animated village |
| `rideOther` | The ride rating of the other day |
| `week` | The 7-day AM/PM ride grid |
| `hours` | The next hours: temperature, rain, gusts, the best time to leave |
| `clock` | The clock (skipped until the time is known) |
| `weather` | [Kids](kids.md): the weather for the next three parts of the day |
| `clothes` | Kids: what to wear for the next three parts of the day |
| `countdown` | Kids: the sleeps to a birthday or holiday (skipped while none is near) |

## Tap, long press, back home

- **Tap list.** A tap steps through it and wraps around at the end. Its **first screen is the home screen**,
  so it cannot be the clock or the countdown (they are not always there).
- **Long-press list.** A long press (1 s) steps through it; after its last screen the next long press goes
  home. A tap in this list also goes home. Leave it empty and a long press works like a tap.
- **Back to home after (s).** Any other screen returns to the home screen after this many seconds (default
  30); 0 keeps it until you tap.
- **Cycle screens every (s).** The device steps through the tap list by itself, this many seconds per screen;
  0 (default) is off. For a device without a touch sensor (switch it off under *Display*), or to show
  everything in turn. A touch still works: the screen you pick stays for a full cycle time.
- The clock and the countdown are skipped while they have nothing to show.
- Up to 6 screens per list; a screen may appear in both.

**Presets** fill in both lists in one go:

| Preset | Tap | Long press |
|--------|-----|------------|
| Rider (the default) | ride rating, the other day | week grid, next hours, clock |
| Kids | kids weather, clothes, countdown | (none: a long press is a tap) |

After choosing a preset you can still change the lists, then press *Save Settings*.

The kids settings cards (*Clothing* and *Countdowns*) only show in the web UI while a kids screen is in one
of the lists.

## In config.json

```json
"display": {
  "screens": {
    "tap": ["weather", "clothes", "countdown"],
    "hold": ["ride", "week", "clock"],
    "returnSeconds": 30
  },
  "cycleSeconds": 0
}
```

Without a `display.screens` block (or with an invalid one) the device uses the rider preset. A device that
ran the former separate kids firmware (its `config.json` has a `kids` block but no screens) starts with the
kids preset, so it keeps showing the kids screens after the update.
