# Weather report

The `report` screen sums up the day in a few short sentences, in English or Dutch (*Language* under
*Display* in the web UI). It is made for the grown-ups next to the [picture screens](kids.md), one long press
away, but any [screen list](screens.md) can have it, also as the home screen.

![Four weather reports: a sunny day with a fresh morning and rain from 19:00; at 20:00, a dry evening and night, then tomorrow with a fresh morning and strong gusts; in English, cloudy with rain until 15:00; a frosty sunny day, roads may be icy](images/report.png){ .center }

## What it says

During the day it is about the rest of today, until 22:00. From 18:00 on it starts with the evening and the
night and then goes on with tomorrow, 07:00 to 22:00; from 22:00 on it starts with the night. The device
writes it itself from the hourly forecast, by fixed rules:

1. **The evening and the night** (from 18:00): their rain, and the lowest temperature until 07:00, for
   example *Vanavond regen, vannacht droog, minimaal 3°.*
2. **The day:** the sky (sunny, sun and clouds, cloudy, foggy), the rain (dry, mostly dry, a shower at times,
   showers, rain, heavy rain, drizzle, snow, thunderstorms) and the temperature, from lowest to highest, or
   *around* one number when it hardly changes. A morning at least 5 degrees colder than the afternoon gets
   its own sentence, with the coldest morning hour and the warmest afternoon hour.
3. **A change:** a shower around an hour, rain from an hour on, or rain until an hour and then dry.
4. **One thing to watch out for**, the first that applies: frost tonight or during the day (roads may be
   icy), gusts of 60 km/h or more, 10 mm of rain or more, gusts of 45 km/h or more.

An hour counts as wet from 0.2 mm, as on the picture screens. When the sentences do not fit on the screen
(7 lines of 21 characters), the least important ones are left out first: the change and the fresh morning,
then the warning; the day and the night stay.

## Blown away

On a windy day (gusts above the *Wind picture above (km/h)* limit under *Clothing*, 50 km/h by default) the
report does not just make way for the next screen: its letters blow away first, from the right, faster in
stronger gusts. A touch during the animation goes straight on.

![An animation of the weather report blowing away letter by letter, after which the village appears](images/report-wind.gif){ .center }
