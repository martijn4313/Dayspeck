# How the ride rating works

For each day the forecast is split into a morning (AM) and an evening (PM) ride window. A window is
rated by its **worst hour**:

| Rating | Condition (defaults) |
|--------|----------------------|
| ✗ don't ride | rain over the window > 2.0 mm, or gusts > 60 km/h |
| ! caution | any rain, temperature < 5 °C, gusts > 40 km/h, or a chance of rain of 50 % or more |
| ✓ good | none of the above |

*Today* shows the morning ride until its window is over, then the evening ride. *Tomorrow* shows
tomorrow's morning. The week grid shows both rides for seven days, starting at today. With
`previewHr` set, the main screen shows tomorrow by default from that hour on (a tap then shows today).

## Ride score and best day

Each window also gets a score from 0 to 100: 100 points, minus 3 per degree away from 20 °C, minus 20
per mm of rain, minus 2 per km/h of gusts above 20 km/h. A day scores as its better window, plus 15 on
Saturday and Sunday so that weekend rides are preferred. Windows rated "don't ride" do not count. The
highest-scoring day is highlighted in the week grid.

The limits are yours to set: *Ride Thresholds* in the [web UI](web-ui.md), and the ride windows in
[`config.json`](configuration.md) (`wd_am`, `wd_pm`, `we_am`, `we_pm`).
