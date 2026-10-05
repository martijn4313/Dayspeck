# Kids screens: what do I wear today?

A second firmware build for children of about 4-5 who are starting to read. Instead of the ride
rating it shows what to wear and what the weather is like, with pictures and numbers and no words, and it
counts the sleeps to birthdays and holidays. The
weather and clothes screens show **the day in three parts**: morning, afternoon and evening, read from left to right. The
weather screen is the main screen; a tap on the touch sensor shows the clothes screen, and another tap (or
30 s) goes back to the weather. When a birthday or holiday is near, a tap on the clothes screen first shows
the **countdown** (see below).

![Kids clothes screens. A summer afternoon: sun cap, t-shirt and shorts now, a rain coat for the evening, sun cap again tomorrow morning. An autumn morning: a sweater now, a t-shirt this afternoon, a rain coat this evening. An autumn evening: a sweater now, a rain coat tomorrow morning, a t-shirt tomorrow afternoon](images/kids-clothes.png){ .center }

**Clothes screen.** One outfit per part of the day. The outfits, from warm to cold (the limits are the
defaults, see *Settings* below):

| When | Picture |
|------|---------|
| 25 °C and up, sunny, daytime | sun cap, t-shirt and shorts |
| 20 °C and up | t-shirt and shorts |
| 15 to under 20 °C | t-shirt |
| 5 to under 15 °C | sweater |
| rain or thunderstorm (5 °C and up) | hooded rain coat and boots |
| 0 to under 5 °C | winter coat and hat |
| below 0 °C, or snow | winter coat, hat, scarf and mittens |

**One number per part.** Each part of the day has one temperature that the weather screen shows and the
outfit goes by, so the number and the picture always match:

| Part | Its temperature | Why |
|------|-----------------|-----|
| morning (7-12) | the **lowest** | that is the walk to school, usually right at the start |
| afternoon (12-18) | the **highest** | how people talk about the day: "this afternoon it gets 17" |
| evening (18-22) | the temperature at **18:00** | when the kids may still play outside; 22:00 is bedtime |

For the current part only the hours still to come count (with what is measured right now for this hour).
A morning of 10° at 7:30 and 17° from 11:00 shows **10 and a sweater**, not the 16 of late morning.

The weather picture goes by the wettest weather of the part: one hour with 0.2 mm of rain or more (or a
thunderstorm) makes it rain, and the outfit a rain coat (unless it is cold enough for the winter coat).

**The three parts.** The parts of the day are morning (7-12), afternoon (12-18) and evening (18-22). The
screen always shows the **next three**, starting with the current one, so it never shows what is past:

| Time | Columns |
|------|---------|
| 10:00 | the rest of this morning, this afternoon, this evening |
| 14:00 | the rest of this afternoon, this evening, tomorrow morning |
| 19:00 | the rest of this evening, tomorrow morning, tomorrow afternoon |
| at night | tomorrow morning, afternoon and evening |

The current part has **three dots** underneath. Where a night lies between two columns there is a dotted
line with a **bed** at the top: that part comes after sleeping. The current part also uses what is
measured right now, not only the forecast.

The symbol at the top of a column says which part it is:

| Symbol | Meaning |
|--------|---------|
| half sun on the horizon, arrow up | morning (the sun comes up) |
| small sun | afternoon (the sun is high) |
| half sun on the horizon, arrow down | evening (the sun goes down) |

![Kids weather screens: 10:00 sun 12, partly cloudy 19, rain 16; a winter day with snow -3, cloud 1, moon -2; a summer afternoon sun 28, thunderstorm 22, sun 28 tomorrow; an evening moon 15, rain 9 and wind 17 tomorrow](images/kids-weather.png){ .center }

**Weather screen:** the same three parts, each with a weather picture (sun or moon, partly cloudy, cloud,
rain, thunderstorm, snow, wind) and its temperature (see *One number per part* above): a number to read,
no unit.

**Without a touch sensor.** The touch sensor is optional here too (web UI, *Display*). With *Cycle screens
every (s)* under [Screens](screens.md) the screens of the tap list follow each other by themselves: with the
kids preset the weather, the clothes and, while a birthday or holiday is near, the countdown. A tap still
steps on and the screen stays for a full cycle time.

**Autumn leaves.** In autumn leaves blow across both screens when the wind is up (gusts from 20 km/h on,
and not while it rains, storms or snows). They stay in the upper part of the screen, above the temperature
numbers, and show as inverted dots so the pictures stay readable. Autumn is September to November, and
March to May in the southern hemisphere; it needs the network time, so the leaves appear once the clock is
synced.

![An animation of the kids weather screen with autumn leaves blowing across it](images/kids-wind.gif){ .center }

**Countdown.** From 14 sleeps before a birthday or holiday (adjustable), a third screen comes after the clothes
screen: the picture of the day on the left, and on the right the number of sleeps (nights) still to go. With ten
sleeps or fewer there is also a row of beds to count, one for every night, so one goes away each morning. On the day
itself the picture stands in the middle with confetti falling around it.

![Kids countdown screens: a birthday cake with five candles and the letter E, six sleeps to go; a pumpkin, twelve sleeps to Halloween; Sinterklaas' mitre and staff, three sleeps; Christmas day, a tree with confetti](images/kids-countdown.png){ .center }

| Day | Picture |
|-----|---------|
| birthday (two can be set) | a cake with a candle for every year it turns (10 and older: the age as a number) and the child's letter on it |
| Halloween, 31 October | a pumpkin |
| Sinterklaas (pakjesavond), 5 December | the mitre and staff |
| Christmas, 25 December | a Christmas tree |

When two are in range, the nearest one is shown (a birthday wins a tie). The screen only exists while a
countdown is running; for the rest of the year a tap goes from the clothes straight back to the weather. It
needs the network time, like the autumn leaves. A birthday on 29 February counts down to 28 February in
other years.

**Settings.** In the web UI under *Display*: *Always sleep*: the screen stays off and a touch wakes it for
30 s (the first touch only wakes it). It works for every screen. *Language* is the language of the
[weather report](screens.md#the-weather-report); the kids screens themselves show no words.

Under *Clothing (kids)* you can change the temperature limits of the outfits table above (and the gust speed
for the wind picture), for a child who feels the cold sooner or later than the defaults. The limits must go
from warm to cold; an equal pair skips that outfit, for example a sweater limit equal to the shorts limit has
no t-shirt step. They are saved in `config.json` (the `kids` keys, see the
[configuration reference](configuration.md)) and are always in °C.

Under *Countdowns (kids)* you enter the two birthdays (the date of birth, for the number of candles, and a
letter for the cake), switch Halloween, Sinterklaas and Christmas on or off, and set from how many sleeps
before the day the countdown starts.

The hours of the parts of the day are compile-time settings (`KIDS_*_HR` in
`firmware/lib/motologic/motologic.h`). The kids screens have no rain animation and no `OLD` or `UPD` mark
(the autumn leaves do blow across them).

**Getting the kids screens.** They are part of the one firmware: in the web UI under [Screens](screens.md)
choose the *Kids* preset (weather, clothes, countdown on a tap; the weather report for the parents on a long
press) and press *Save Settings*, or mix them with the rider screens.
