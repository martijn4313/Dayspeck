---
description: "Choose which of Dayspeck's ten weather screens your ESP8266 OLED display shows, in which order, and what a tap or long press does."
---

# Your own display

Dayspeck has ten screens. Which of them your device shows, in which order, and what a tap or a long press
does, you choose in the web UI under **Screens** (or in `config.json`, see [the configuration reference](configuration.md#screens)). Nothing needs to be
rebuilt or reflashed.

![The Screens card: a preset choice, the tap list (village, weather in pictures, clothes, countdown), the long-press list (ride rating, week grid, weather report), back to home after 30 s and cycle off](images/webui-screens.png){ .center }

## The screens

| Name | Screen |
|------|--------|
| `ride` | [Ride rating](rider.md) of today (tomorrow from the *Tomorrow from (hour)* setting on), with the animated village |
| `rideOther` | The ride rating of the other day |
| `week` | The [7-day ride grid](rider.md#the-week-and-the-next-hours), morning and evening |
| `hours` | The [next hours](rider.md#the-week-and-the-next-hours): temperature, rain, gusts, the best time to leave |
| `rideHours` | The [ride rating of the next hours](rider.md#ride-hours) and when to go |
| `village` | [What to wear now](kids.md#village), big, next to the animated village |
| `weather` | [The weather in pictures](kids.md) for the next three parts of the day |
| `clothes` | [What to wear](kids.md) for the next three parts of the day |
| `countdown` | [The sleeps](kids.md#countdown) to a birthday or holiday (skipped while none is near) |
| `report` | [The weather report](report.md): the day in a few short sentences |
| `clock` | [The clock](clock-and-cat.md#the-clock-clock) (skipped until the time is known) |
| `cat` | [The lucky cat](clock-and-cat.md#the-lucky-cat-cat), beckoning, with the temperature |

The screens with the village (`ride`, `rideOther` and `village`) have the rain, snow, gusts and autumn
leaves; the leaves also blow across `weather` and `clothes`.

## Tap, long press, back home

- **Tap list.** A tap steps through it and wraps around at the end. Its **first screen is the home screen**,
  so it cannot be the clock or the countdown (they are not always there).
- **Long-press list.** A long press (1 s) steps through it; after its last screen the next long press goes
  home. A tap in this list also goes home. Leave it empty and a long press works like a tap.
- **Back to home after (s).** Any other screen returns to the home screen after this many seconds (default
  30); 0 keeps it until you tap.
- **Cycle screens every (s).** The device steps through the tap list by itself, this many seconds per screen;
  0 (default) is off. See [without a touch sensor](#without-a-touch-sensor).
- The clock and the countdown are skipped while they have nothing to show.
- Up to 6 screens per list; a screen may appear in both.

## Ready-made combinations

The *Preset* choice fills in both lists in one go. Use one as it is, or as a start and change the lists
before you press *Save Settings*:

| Preset | Tap | Long press |
|--------|-----|------------|
| Rider (the default) | ride rating, the other day | week grid, next hours, clock |
| Kids | village, weather, clothes, countdown | weather report |

Some other ideas:

| For | Tap | Long press |
|-----|-----|------------|
| a rider with children | village, weather, clothes | ride rating, week grid, weather report |
| the grown-ups only | weather report, next hours, clock | week grid |
| a wall without a button (cycle every 20 s) | weather report, ride rating, clock | (none) |

The cards for the outfits (*Clothing*) and the countdowns (*Countdowns*) only show in the web UI while a
screen that uses them is in one of the lists.

## Without a touch sensor

The touch sensor is optional. Switch it off in the web UI under *Display* (or with `display.touchEnabled` in
`config.json`) and set *Cycle screens every (s)* (`display.cycleSeconds`): the device then shows the screens
of its tap list in turn, each for that many seconds, so put everything you want to see in the tap list.

A touch sensor keeps working while the screens cycle: a tap picks a screen and it stays for a full cycle
time. While they cycle, nothing goes back to the home screen by itself, because every screen already stays
for the cycle time. *Always sleep* needs the touch sensor, because a touch is the only way to wake a
switched-off screen; the quiet hours and the night sleep timer work without one.

## When the screens change

Every screen redraws after each weather update: every 15-17 minutes by day, and about once an hour at night,
when the device fetches less often. The weather report and the village also redraw on the hour, so the
report turns to tomorrow at 18:00 and the outfit follows the part of the day. The animations run at 15 frames per second while there is something
to animate.

## See also

- [Screen power](web-ui.md#screen-power): dimming, night sleep and quiet hours.
- [Demo](web-ui.md#demo): every screen and animation with made-up weather.
- [Configuration reference](configuration.md#screens): the `display.screens` keys.
