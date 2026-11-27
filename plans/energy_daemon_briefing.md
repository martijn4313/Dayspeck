# Briefing: the energy monitor daemon on the Raspberry Pi

For the session that builds the C++ daemon on the Raspberry Pi. It reads the heat pump and the PV inverter and
serves one JSON document over HTTP to a Dayspeck display on the same network. This briefing is the display's
side of that contract: what it sends, what it accepts and what it does with each field. How the daemon reads the
heat pump and the inverter is up to you.

The display side is in the Dayspeck repository (PR #58): `docs/energy.md` for users,
`firmware/lib/energyjson/` for the parser, `test/test_energy/test_energy.cpp` for its tests, and
`firmware/src/energy.cpp` for the fetch. If anything here disagrees with that code, the code wins: tell the
user, so one of them gets fixed.

## What the display is

An ESP8266 with a 128×64 OLED. Three screens use this data:

- **heatpump:** what the heat pump does (mode), its electrical power, COP, flow and return temperature, outside
  temperature, hot water.
- **solar:** the panels' power now, today's total, power to or from the grid, a bar per hour of today.
- **energy:** sun → house → grid with the power of each, and the heat pump's power.

It has 80 KB of RAM (a few tens of KB free, shared with WiFi and its web server) and no TLS, so the contract
is deliberately small and plain.

## The request the display sends

Every 15 seconds while one of those screens is shown (30 s after a failed read), and never otherwise:

```
GET /energy.json HTTP/1.0
Host: 192.168.1.20:8080
User-Agent: ESP8266HTTPClient
Connection: close
```

The path, host and port are whatever the user types in the display's web UI (`http://<host>[:port]/<path>`).
The display sends nothing else: no query string, no authentication, no cookies, no `Accept` header.

## What the response must be

| Requirement | Why |
|-------------|-----|
| Plain HTTP, any port, any path; listen on IPv4 (`0.0.0.0`) | The ESP8266 cannot do TLS; it connects over IPv4 |
| Status **200** | Anything else is a failed read. **Redirects are not followed**: a 301 or 302 is a failure. |
| **No chunked transfer encoding.** Send `Content-Length`, or close the connection after the body. | The request is HTTP/1.0 and the body is parsed straight off the socket; chunk headers would break the JSON. Many C++ HTTP libraries do this right for HTTP/1.0, but check. |
| The body is one JSON **object**, UTF-8, under **2 KB** | Anything else (an array, a cut-off document) is a failed read |
| Answer within **2 seconds**; aim for well under 100 ms | The display gives up after 2 s without progress, and its screen freezes while it waits. Serve from memory; never read the heat pump or the inverter inside the request. |
| `Content-Type: application/json` | Not checked, but correct |
| Handle a client that disconnects early, and a few requests at once | The display gives up on timeouts; a curl or a browser may be open at the same time |

The display ignores every field it does not know, so you may add your own (a timestamp, a version, debug values)
without breaking it. Do not change the name, unit or meaning of an existing field: that breaks the display.

## The JSON document

```json
{
  "heatpump": {
    "mode": "heating",
    "powerW": 850,
    "cop": 3.8,
    "flowC": 35.2,
    "returnC": 30.1,
    "outsideC": 4.5,
    "hotWaterC": 48.2
  },
  "solar": {
    "powerW": 3240,
    "todayWh": 14800,
    "hourlyWh": [0, 0, 0, 0, 0, 0, 80, 420, 1100, 1900, 2600, 3100, 3350, 1080,
                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  },
  "grid": { "powerW": -2410 },
  "house": { "powerW": 830 }
}
```

**Every field is optional.** A missing field, `null`, or a value that is not a number shows as a dash. A whole
object can be left out (no heat pump, no panels). Numbers may be integers or have decimals; the display rounds.
Send numbers as JSON numbers, never as strings (`"850"` counts as missing).

| Field | Type, unit | Meaning on the display |
|-------|-----------|------------------------|
| `heatpump.mode` | string | One of `heating`, `hotwater`, `cooling`, `defrost`, `standby`, `off` (lowercase, exactly these). The display shows them in English or Dutch. Another string is shown as sent, cut to 11 characters; keep it ASCII (the font has no other letters). The fan on the screen turns in every mode but `standby` and `off`; `defrost` adds a snowflake. Missing: the fan turns only if `powerW` > 0. |
| `heatpump.powerW` | number, W | The **electrical** power the heat pump draws now, not the heat it delivers. Shown only while it runs. |
| `heatpump.cop` | number | The COP **now** (heat out ÷ electricity in), one decimal shown. Leave it out (or `null`) while it does not run; the display hides it then anyway. |
| `heatpump.flowC`, `returnC` | number, °C | Water temperature to and from the heating. Shown as whole degrees, "35°>30°". |
| `heatpump.outsideC` | number, °C | Outside, from the heat pump's own sensor |
| `heatpump.hotWaterC` | number, °C | The hot water tank |
| `solar.powerW` | number, W | What the panels make now. At night send `0`, not a missing field, so the screen shows 0 W. |
| `solar.todayWh` | number, Wh | Made today so far. Resets at local midnight. Shown with one decimal in kWh. |
| `solar.hourlyWh` | array of exactly **24** numbers, Wh | Energy per **local** hour of today: `[0]` is 00:00–01:00. The current hour holds what it made so far, the hours to come `0`. Reset at local midnight. Any other length and the display leaves the bars out. Values are clamped to 0–65535. The bars are scaled to the day's best hour, and only 05:00–21:00 are drawn. |
| `grid.powerW` | number, W | **Positive: power coming from the grid. Negative: power going to the grid.** Drives the arrow and the moving dots. |
| `house.powerW` | number, W | Everything the house uses now, **heat pump included**. If missing, the display uses `solar.powerW + grid.powerW`, so leave it out unless you measure it better. |

"Local" means the time zone the Pi runs in; the display uses its own clock in the same time zone to mark the
current hour. Keep the Pi's clock on NTP.

## Stale data

The display only knows whether its HTTP read worked: after 3 minutes without a good read it shows "No data from
<address>". It cannot tell whether your numbers are old. So:

- When a source has not answered for a while (say 60 seconds), **leave its fields out** instead of serving the
  last values. A dash is honest; a frozen "850W" is not.
- Keep serving the document (HTTP 200, a valid object) even when every source is down: `{}` is valid.
- `todayWh` and `hourlyWh` may stay when the inverter sleeps at night, as long as they are today's.
- An inverter that sleeps at night is not a failure: send `solar.powerW: 0`.

## Finding the Pi

The user types the address into the display's web UI. Two options:

- **An IP address** (the most reliable): give the Pi a fixed address, or a DHCP reservation in the router.
- **A `.local` name** such as `http://energypi.local:8080/energy.json`: the display resolves `.local` names
  with one multicast DNS query from a random port, so the Pi's mDNS responder (Avahi on Raspberry Pi OS) must
  answer such one-shot queries (it normally does). This is not tested yet, so make sure the IP address works
  as well.

Pick a port and path and document them; `/energy.json` on port 8080 is a fine default.

## How to check it before the display sees it

1. The exact request the display makes, including HTTP/1.0 (check there is no `Transfer-Encoding: chunked` in
   the response headers, and a status of 200):

   ```
   curl -0 -sS -D - --max-time 2 http://<pi>:<port>/energy.json
   ```

2. The body is a JSON object under 2 KB: `curl -0 -s http://<pi>:<port>/energy.json | python3 -m json.tool`
   and `| wc -c`.
3. Time it: `curl -0 -o /dev/null -s -w '%{time_total}\n' http://<pi>:<port>/energy.json` should print well under
   0.1.
4. Signs and units: in the sun with low use, `grid.powerW` is negative and `house.powerW` is about
   `solar.powerW + grid.powerW`. While the heat pump runs, `heatpump.powerW` is part of `house.powerW`.
5. `hourlyWh` has 24 entries, its sum is about `todayWh`, and both reset at midnight.
6. On the display: put the address in *Energy monitor* in its web UI and add a `heatpump`, `solar` or `energy`
   screen. *Last read* should say "OK, n s ago"; otherwise it names the error (no answer, HTTP status, JSON
   error).

## What I need back from you

- The address (IP, port, path) the daemon serves, and whether the `.local` name works.
- Which fields the user's heat pump and inverter can actually provide, and which stay missing.
- Any field you would like the display to show that is not in this contract: propose it, do not add it to
  the meaning of an existing field.
- Anything in this briefing that turned out wrong on the real hardware.
