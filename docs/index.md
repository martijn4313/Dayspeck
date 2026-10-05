# Dayspeck

A small weather display for a bedside table, a hallway or a kitchen wall: an ESP8266 with a 0.96" OLED
(128×64 pixels) and, if you like, a touch button.

![Four Dayspeck screens: the ride rating with its village, the picture screen with a sweater and the village, the weather of the day in pictures, and the weather report in words](images/hero.png)

You put it together yourself, like building blocks. There are ten screens; you pick the ones you want,
put them in the order you like and choose what a tap and a long press show. It is all one firmware and it
is all set in the web page of the device, so changing your mind needs no new build.

## The blocks

| Screens | What they tell you |
|---------|--------------------|
| [Ride rating](rider.md) | *Can I ride today?* A big check mark, exclamation mark or cross next to an animated village, with the other day, a week grid and the next hours |
| [Picture screens](kids.md) | *What do I wear today?* Outfits, weather pictures and numbers for the morning, afternoon and evening, and a countdown in sleeps to birthdays and holidays. No words, so children who cannot read yet can use them |
| [Weather report](report.md) | The day in a few short sentences, in English or Dutch |
| [Clock](screens.md#the-clock) | The time and the date |

A few combinations, to give you ideas (see [Your own display](screens.md) for how to set them up):

- **By the bed of a rider:** the ride rating as the home screen; a long press shows the week, the next
  hours and the clock.
- **In the hallway of a family:** the outfit for now as the home screen, a tap for the weather and the
  clothes of the day, and a long press for the weather report for the grown-ups.
- **On the wall, without a button:** the weather report, the next hours and the clock, each shown for 20
  seconds in turn.

## Alive with the weather

The screens move with the weather outside: rain falls and splashes in the village, snow drifts, gusts sweep
across it and in autumn leaves tumble along. On a windy day the weather report blows away when you leave it.
Want to see it all without waiting for a storm? The **demo** in the web page plays every screen and
animation in a minute.

![An animation of the ride screen on a windy autumn day: gusts and leaves blow across the village](images/rider-wind.gif){ .center }

## Where to start

1. Check the [hardware](hardware.md) and wire it up.
2. [Build and flash](build.md) the firmware.
3. Follow the [first boot](first-boot.md) steps to connect it to your WiFi.
4. Pick your screens under [Your own display](screens.md) and tune the rest in the [web UI](web-ui.md).

The forecast comes from [Open-Meteo](https://open-meteo.com): free, without an account or a key, and the
only thing the device sends is the location you set. Updates come [over the air](ota.md).

*The pictures in this manual are renders of the firmware's own drawing code (see
[contributing](contributing.md#the-pictures-in-this-manual)), not photos of the device.*
