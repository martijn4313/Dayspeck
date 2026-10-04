# Kids variant: what do I wear today?

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

The outfit of a part goes by its average temperature and its wettest weather: one hour with 0.2 mm of rain
or more (or a thunderstorm) makes it a rain coat.

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

![Kids weather screens: 10:00 sun 13, partly cloudy 19, rain 16; a winter day with snow -3, cloud 1, moon -2; a summer afternoon sun 28, thunderstorm 22, sun 28 tomorrow; an evening moon 15, rain 12 and wind 17 tomorrow](images/kids-weather.png){ .center }

**Weather screen:** the same three parts, each with a weather picture (sun or moon, partly cloudy, cloud,
rain, thunderstorm, snow, wind) and its **highest temperature**, like the forecast on the news: a number to
read, no unit.

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
30 s (the first touch only wakes it). It also works in the normal build. *Language* has no visible effect
since the screens no longer show words.

Under *Clothing (kids)* you can change the temperature limits of the outfits table above (and the gust speed
for the wind picture), for a child who feels the cold sooner or later than the defaults. The limits must go
from warm to cold; an equal pair skips that outfit, for example a sweater limit equal to the shorts limit has
no t-shirt step. They are saved in `config.json` (the `kids` keys, see the
[configuration reference](configuration.md)) and are always in °C.

Under *Countdowns (kids)* you enter the two birthdays (the date of birth, for the number of candles, and a
letter for the cake), switch Halloween, Sinterklaas and Christmas on or off, and set from how many sleeps
before the day the countdown starts.

The hours of the parts of the day are compile-time settings (`KIDS_*_HR` in
`firmware/lib/motologic/motologic.h`). The rain animation and the `OLD` tag are left out in this build (the autumn
leaves are not); WiFi, location, the web UI and OTA work as before.

```sh
pio run -e esp01_1m_kids -t upload
```
