// Dayspeck Bedside Display — Embedded Web Server Implementation
//
// Security model:
//  * every route requires the admin password (HTTP Basic auth, user "admin"); repeated failures
//    lock the device out for a minute
//  * state-changing routes must be POST and carry the per-boot token in the X-Dayspeck header.
//    The token can only be read by same-origin script (/api/token), which defeats cross-site
//    request forgery against a browser that has cached the Basic credentials
//  * OTA uploads live on /update-<token> behind the same password; with a key compiled in, every
//    update (uploaded or pulled from the relay, see ota.h) must carry a valid signature
//  * secrets (WiFi password, API key) are never sent back to the browser
//  * Basic auth is not encrypted: anyone who can sniff the LAN can read the password.
#include "webserver.h"
#include "config.h"
#include "weather.h"
#include "app_state.h"
#include "security.h"
#include "version.h"
#include "ota.h"
#include "touch.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <functional>
#include <stdlib.h>
#include <vector>

ESP8266WebServer server(80);

#define MAX_SSID_LOCATIONS   10
#define MAX_SSID_LEN         32
#define MAX_PLACE_NAME_LEN   64
#define MAX_API_URL_LEN      128
#define AUTH_MAX_FAILURES    10
#define AUTH_LOCKOUT_MS      60000UL

static String csrfToken;
static String otaPath = "/update";
static unsigned long rebootAtMs = 0;

static uint8_t authFailures = 0;
static unsigned long authLockUntilMs = 0;

static const char index_html[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Dayspeck Status</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: system-ui; max-width: 600px; margin: 0 auto; padding: 1rem; }
        .card { border: 1px solid #ddd; border-radius: 8px; padding: 1rem; margin: 1rem 0; }
        .label { font-weight: 600; display: inline-block; width: 180px; }
        input { width: 80px; }
        input[type=text], input[type=password], input[type=url] { width: 200px; }
        input[type=date] { width: 140px; }
        input.letter { width: 30px; }
        .slots { margin: 0.3rem 0 0 1rem; }
        .slots div { margin: 0.2rem 0; }
        .slots button { margin: 0 0 0 0.3rem; padding: 0.1rem 0.5rem; }
        button { padding: 0.5rem 1rem; margin-top: 1rem; }
        #toast { display: none; position: sticky; top: 0; padding: 0.6rem 1rem; border-radius: 6px; color: #fff; z-index: 5; }
        #toast.ok { background: #2a7a3b; }
        #toast.err { background: #b3261e; }
        #placeResults { margin-bottom: 1rem; }
        #placeResults button { display: block; width: 100%; text-align: left; margin-top: 0.4rem; }
        .banner { background: #fff3cd; border: 1px solid #e0b100; border-radius: 8px; padding: 0.75rem 1rem; margin: 1rem 0; }
        #verboseLogs { display: none; margin-top: 10px; font-family: monospace; font-size: 12px; background: #222; color: #0f0; padding: 8px; max-height: 300px; overflow-y: auto; white-space: pre-wrap; }
    </style>
</head>
<body>
    <div id="toast"></div>
    <h1>Dayspeck</h1>
    <div id="pwBanner" class="banner" style="display:none">
        This device still uses its default password (shown on the display during WiFi setup).
        Please set your own below.
    </div>

    <div class="card">
        <h3>Location</h3>
        <span class="label">Place:</span> <span id="locationName"></span><br>
        <span class="label">Latitude:</span> <span id="lat"></span><br>
        <span class="label">Longitude:</span> <span id="lon"></span><br>
        <span class="label">Source:</span> <span id="locationSource"></span><br>
        <a id="mapLink" target="_blank" rel="noopener">Show on the map</a>
    </div>

    <div class="card">
        <h3>Set Location</h3>
        <form id="placeSearch">
            <span class="label">Search a place:</span> <input id="placeQuery" type="text" maxlength="64" placeholder="town or village" required>
            <button type="submit">Search</button>
        </form>
        <div id="placeResults"></div>
        <form id="locationForm">
            <span class="label">Name:</span> <input name="place" type="text" maxlength="64"><br>
            <span class="label">Latitude:</span> <input name="lat" type="number" step="any" min="-90" max="90" required><br>
            <span class="label">Longitude:</span> <input name="lon" type="number" step="any" min="-180" max="180" required><br>
            <small>A search fills these in. Without internet (setup mode) type the coordinates, e.g. from a map app.
            They are rounded to 2 decimals (about 1 km).</small><br>
            <button type="submit">Set Location</button>
        </form>
    </div>

    <div class="card">
        <h3>WiFi Status</h3>
        <span class="label">Connected:</span> <span id="wifiConnected"></span><br>
        <span class="label">Signal Strength:</span> <span id="wifiSignal"></span><br>
        <button id="scanBtn">Scan Networks</button>
        <div id="networksList"></div>
    </div>

    <div class="card">
        <h3>WiFi Configuration</h3>
        <form id="wifiForm">
            <span class="label">Network SSID:</span> <input name="ssid" type="text" maxlength="32" required><br>
            <span class="label">Password:</span> <input name="password" type="password" maxlength="63" autocomplete="new-password" placeholder="(unchanged)"><br>
            <button type="submit">Save WiFi Settings</button>
        </form>
    </div>

    <div class="card">
        <h3>SSID Location Settings</h3>
        <form id="ssidLocationForm">
            <span class="label">SSID:</span> <select name="ssid" id="ssidSelect" required>
                <option value="">Select Network</option>
            </select><br>
            <span class="label">Latitude:</span> <input name="lat" type="number" step="0.0001" min="-90" max="90" required><br>
            <span class="label">Longitude:</span> <input name="lon" type="number" step="0.0001" min="-180" max="180" required><br>
            <button type="submit">Add Location</button>
        </form>
        <div id="ssidLocationsList"></div>
    </div>

    <div class="card">
        <h3>Ride Thresholds</h3>
        <form id="thresholdsForm">
            <span class="label">Max Rain (mm):</span> <input name="maxRain" type="number" step="0.1" min="0" max="100"><br>
            <span class="label">Max Wind (km/h):</span> <input name="maxWind" type="number" step="1" min="0" max="200"><br>
            <span class="label">Min Temp (°C):</span> <input name="minTemp" type="number" step="1" min="-50" max="50"><br>
            <span class="label">Warn Wind (km/h):</span> <input name="warnWind" type="number" step="1" min="0" max="200"><br>
            <span class="label">Caution rain chance (%):</span> <input name="rainProb" type="number" step="5" min="0" max="101" title="101 = off"><br>
            <button type="submit">Save Settings</button>
        </form>
    </div>

    <div class="card">
        <h3>Screens</h3>
        <p><small>A tap steps through the first list; its first screen is the home screen. A long press steps through
        the second list and then goes home (no screens there: a long press is a tap). The clock and the countdown
        are skipped while they have nothing to show.</small></p>
        <form id="screensForm">
            <span class="label">Preset:</span> <select id="screenPreset"><option value="">choose...</option><option value="rider">Rider</option><option value="kids">Kids</option></select> <small>fills in the lists below</small><br>
            <span class="label">Tap:</span><div id="tapSlots" class="slots"></div><button type="button" id="addTap">Add screen</button><br>
            <span class="label">Long press:</span><div id="holdSlots" class="slots"></div><button type="button" id="addHold">Add screen</button><br>
            <span class="label">Back to home after (s):</span> <input name="returnSeconds" type="number" min="0" max="3600"> <small>0 = never</small><br>
            <span class="label">Cycle screens every (s):</span> <input name="cycleSeconds" type="number" min="0" max="3600"> <small>0 = off; steps through the tap list, also without a touch sensor</small><br>
            <button type="submit">Save Settings</button>
        </form>
    </div>

    <div class="card">
        <h3>Display</h3>
        <form id="displayForm">
            <span class="label">Tomorrow from (hour):</span> <input name="previewHr" type="number" min="0" max="24" title="24 = never"> <small>24 = never</small><br>
            <span class="label">Dim at night:</span> <input name="dimAtNight" type="checkbox"> <small>a touch gives full brightness for 30 s</small><br>
            <span class="label">Night brightness (%):</span> <input name="nightBrightness" type="number" min="1" max="100"> <small>raise it if the screen looks blank at night</small><br>
            <span class="label">Sleep at night after (min):</span> <input name="sleepMinutes" type="number" min="0" max="600"> <small>0 = never; a touch wakes it</small><br>
            <span class="label">Always sleep:</span> <input name="alwaysSleep" type="checkbox"> <small>screen off; a touch wakes it for 30 s</small><br>
            <span class="label">Touch sensor:</span> <input name="touchEnabled" type="checkbox"> <small>GPIO3; off if none is connected</small><br>
            <span class="label">Language:</span> <select name="language"><option value="en">English</option><option value="nl">Nederlands</option></select><br>
            <span class="label">Screen off from (hour):</span> <input name="quietStart" type="number" min="-1" max="23"> <small>-1 = off</small><br>
            <span class="label">Screen off until (hour):</span> <input name="quietEnd" type="number" min="-1" max="23"><br>
            <button type="submit">Save Settings</button>
        </form>
        <p><small>Demo: about a minute of every screen and animation (rain, snow, gusts, leaves, confetti, the
        report blowing away) with made-up weather. A touch on the sensor stops it.</small></p>
        <span class="label">Repeat:</span> <input id="demoRepeat" type="checkbox"> <small>until stopped</small><br>
        <button type="button" id="demoStart">Play demo</button> <button type="button" id="demoStop">Stop demo</button>
    </div>

    <div class="card" id="kidsCard" style="display:none">
        <h3>Clothing (kids)</h3>
        <p><small>What the kids screens show, by temperature (&deg;C, always metric). Each limit must be equal to or below the one above it.</small></p>
        <form id="kidsForm">
            <span class="label">Sun cap, t-shirt and shorts from:</span> <input name="hot" type="number" step="1" min="-30" max="50"> <small>sunny daytime only</small><br>
            <span class="label">T-shirt and shorts from:</span> <input name="shorts" type="number" step="1" min="-30" max="50"><br>
            <span class="label">Sweater below:</span> <input name="sweater" type="number" step="1" min="-30" max="50"> <small>t-shirt above</small><br>
            <span class="label">Winter coat and hat below:</span> <input name="coat" type="number" step="1" min="-30" max="50"><br>
            <span class="label">Scarf and mittens below:</span> <input name="freeze" type="number" step="1" min="-30" max="50"><br>
            <span class="label">Wind picture above (km/h):</span> <input name="gust" type="number" step="1" min="1" max="150"> <small>gusts, dry weather</small><br>
            <button type="submit">Save Settings</button>
        </form>
    </div>

    <div class="card" id="countdownCard" style="display:none">
        <h3>Countdowns (kids)</h3>
        <p><small>The countdown screen counts the sleeps to a birthday or holiday when it is near (put it in a list under Screens). The cake has a candle for every year and the letter on it.</small></p>
        <form id="countdownForm">
            <span class="label">Birthday 1:</span> <input name="birthday1" type="date"> <input name="initial1" class="letter" maxlength="1" pattern="[A-Za-z]?" title="one letter"> <small>date of birth, letter</small><br>
            <span class="label">Birthday 2:</span> <input name="birthday2" type="date"> <input name="initial2" class="letter" maxlength="1" pattern="[A-Za-z]?" title="one letter"> <small>no date = none</small><br>
            <span class="label">Halloween (31 Oct):</span> <input name="halloween" type="checkbox"><br>
            <span class="label">Sinterklaas (5 Dec):</span> <input name="sinterklaas" type="checkbox"><br>
            <span class="label">Christmas (25 Dec):</span> <input name="christmas" type="checkbox"><br>
            <span class="label">Show from (sleeps before):</span> <input name="countdownDays" type="number" step="1" min="1" max="60"><br>
            <button type="submit">Save Settings</button>
        </form>
    </div>

    <div class="card">
        <h3>Weather API Config</h3>
        <form id="weatherApiForm">
            <span class="label">API URL:</span> <input name="apiUrl" type="url" maxlength="128" value="http://api.open-meteo.com/v1/forecast"><br>
            <span class="label">Units:</span>
            <select name="units">
                <option value="metric">Metric</option>
                <option value="imperial">Imperial</option>
            </select><br>
            <span class="label">Verbose Debug:</span> <input name="weatherDebug" type="checkbox" id="weatherDebug"><br>
            <button type="submit">Save Settings</button>
        </form>
    </div>

    <div class="card">
        <h3>Admin Password</h3>
        <p>Protects this page, firmware updates and the WiFi setup network. The device restarts after a change.</p>
        <form id="passwordForm">
            <span class="label">Current password:</span> <input name="current" type="password" autocomplete="current-password" required><br>
            <span class="label">New password:</span> <input name="newPassword" type="password" minlength="8" maxlength="63" autocomplete="new-password" required><br>
            <span class="label">Repeat new password:</span> <input id="newPassword2" type="password" minlength="8" maxlength="63" autocomplete="new-password" required><br>
            <button type="submit">Change Password</button>
        </form>
    </div>

    <div class="card">
        <h3>Debug Info</h3>
        <span class="label">Firmware:</span> <span id="firmware"></span><br>
        <span class="label">Weather Valid:</span> <span id="weatherValid"></span><br>
        <span class="label">Weather Age:</span> <span id="weatherAge"></span> minutes<br>
        <span class="label">mDNS Started:</span> <span id="mdnsStarted"></span><br>
        <br>
        <button id="toggleLogs">Show Verbose Logs</button>
        <div id="verboseLogs"></div>
    </div>

    <div class="card">
        <h3>Firmware Update</h3>
        <span class="label">Installed:</span> <span id="otaCurrent"></span><br>
        <span class="label">Latest release:</span> <span id="otaLatest"></span><br>
        <span class="label">Last check:</span> <span id="otaChecked"></span><br>
        <div id="otaNotes"></div>
        <div id="otaError" style="color:#b3261e"></div>
        <button id="otaCheckBtn">Check now</button>
        <button id="otaInstallBtn" style="display:none"></button>
        <div id="otaFeedback"></div>
        <form id="otaSettingsForm">
            <span class="label">Update server:</span> <input name="url" type="url" maxlength="128" placeholder="http://dayspeck-ota.you.workers.dev"><br>
            <span class="label">Check daily:</span> <input name="autoCheck" type="checkbox"><br>
            <button type="submit">Save Settings</button>
        </form>
        <h4>Manual upload</h4>
        <p><small>A signed <code>dayspeck.bin.gz</code> from a release.</small></p>
        <form id="otaForm">
            <input type="file" name="update" accept=".gz,.bin" required><br>
            <button type="submit">Upload and Update</button>
        </form>
        <div id="otaUploadFeedback"></div>
    </div>

    <script>
        // Request token: only same-origin script can read it, and every POST must send it back
        let csrf = '';
        let otaPath = '';
        const tokenReady = fetch('/api/token')
            .then(r => r.json())
            .then(t => {
                csrf = t.token;
                otaPath = t.otaPath;
                if (t.defaultPassword) document.getElementById('pwBanner').style.display = 'block';
            });

        function toast(message, ok) {
            const t = document.getElementById('toast');
            t.textContent = message;
            t.className = ok ? 'ok' : 'err';
            t.style.display = 'block';
            clearTimeout(window.toastTimer);
            window.toastTimer = setTimeout(() => { t.style.display = 'none'; }, 6000);
        }

        // POST helper: adds the token, resolves to {ok, status, message}
        function post(path, body) {
            return tokenReady
                .then(() => fetch(path, { method: 'POST', headers: { 'X-Dayspeck': csrf }, body: body }))
                .then(r => r.text().then(text => {
                    let message = '';
                    try { message = JSON.parse(text).message || ''; } catch (e) { /* not JSON */ }
                    return { ok: r.ok, status: r.status, message: message };
                }));
        }

        // Wire a form to a POST route (urlencoded body)
        function submitForm(id, path, okMessage, after) {
            const form = document.getElementById(id);
            form.addEventListener('submit', e => {
                e.preventDefault();
                post(path, new URLSearchParams(new FormData(form)))
                    .then(res => {
                        toast(res.message || (res.ok ? okMessage : 'Error ' + res.status), res.ok);
                        if (res.ok && after) after();
                    })
                    .catch(() => toast('Request failed', false));
            });
        }

        function option(value, text) {
            const o = document.createElement('option');
            o.value = value;
            o.textContent = text;
            return o;
        }

        // Everything below inserts device or network supplied text with textContent, never innerHTML
        // Place search: the browser asks Open-Meteo's geocoding service directly (the device needs no internet
        // access or TLS for it). Only the chosen coordinates and name are sent to the device.
        const placeResults = document.getElementById('placeResults');
        document.getElementById('placeSearch').addEventListener('submit', e => {
            e.preventDefault();
            const query = document.getElementById('placeQuery').value.trim();
            if (!query) return;
            placeResults.textContent = 'Searching...';
            const lang = (navigator.language || 'en').slice(0, 2);
            fetch('https://geocoding-api.open-meteo.com/v1/search?count=8&format=json&language=' +
                  encodeURIComponent(lang) + '&name=' + encodeURIComponent(query))
                .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
                .then(data => {
                    const places = data.results || [];
                    placeResults.replaceChildren();
                    if (!places.length) {
                        placeResults.textContent = 'No places found. Try another spelling, or enter the coordinates below.';
                        return;
                    }
                    places.forEach(p => {
                        const parts = [p.name, p.admin2, p.admin1, p.country].filter((v, i, a) => v && a.indexOf(v) === i);
                        const label = parts.join(', ');
                        const b = document.createElement('button');
                        b.type = 'button';
                        b.textContent = label;
                        b.addEventListener('click', () => {
                            const f = document.forms.locationForm;
                            f.elements.place.value = label.slice(0, 64);
                            f.elements.lat.value = p.latitude.toFixed(2);
                            f.elements.lon.value = p.longitude.toFixed(2);
                            placeResults.textContent = 'Selected ' + label + '. Press Set Location to use it.';
                        });
                        placeResults.appendChild(b);
                    });
                })
                .catch(() => {
                    placeResults.textContent = 'The search needs an internet connection. Enter the coordinates below instead.';
                });
        });

        function loadStatus(full) {
            return fetch('/api/status')
                .then(r => r.json())
                .then(s => {
                    document.getElementById('lat').textContent = String(+s.lat.toFixed(4));
                    document.getElementById('lon').textContent = String(+s.lon.toFixed(4));
                    document.getElementById('locationSource').textContent = s.locationSource;
                    document.getElementById('locationName').textContent = s.locationName || '(no name)';
                    document.getElementById('mapLink').href = 'https://www.openstreetmap.org/?mlat=' + s.lat +
                        '&mlon=' + s.lon + '#map=12/' + s.lat + '/' + s.lon;
                    renderSsidLocations(s.ssidLocations || []);
                    if (!full) return;

                    const t = document.forms.thresholdsForm;
                    t.maxRain.value = s.thresholds.maxRainMm;
                    t.maxWind.value = s.thresholds.maxWindKmh;
                    t.minTemp.value = s.thresholds.minTempC;
                    t.warnWind.value = s.thresholds.warnWindKmh;
                    t.rainProb.value = s.thresholds.rainProbPct;

                    const d = document.forms.displayForm;
                    d.previewHr.value = s.display.previewHr;
                    d.dimAtNight.checked = s.display.dimAtNight;
                    d.nightBrightness.value = s.display.nightBrightness;
                    d.sleepMinutes.value = s.display.sleepMinutes;
                    d.alwaysSleep.checked = s.display.alwaysSleep;
                    d.touchEnabled.checked = s.display.touchEnabled;
                    d.language.value = s.display.language;
                    d.quietStart.value = s.display.quietStart;
                    d.quietEnd.value = s.display.quietEnd;

                    const sc = document.forms.screensForm;
                    setSlots('tapSlots', s.display.screens.tap);
                    setSlots('holdSlots', s.display.screens.hold);
                    sc.returnSeconds.value = s.display.screens.returnSeconds;
                    sc.cycleSeconds.value = s.display.cycleSeconds;

                    // The kids settings only matter when a kids screen is in use
                    const used = s.display.screens.tap.concat(s.display.screens.hold);
                    document.getElementById('kidsCard').style.display =
                        used.includes('weather') || used.includes('clothes') || used.includes('village') ? '' : 'none';
                    document.getElementById('countdownCard').style.display = used.includes('countdown') ? '' : 'none';
                    const k = document.forms.kidsForm;
                    k.hot.value = s.kids.hotFromC;
                    k.shorts.value = s.kids.shortsFromC;
                    k.sweater.value = s.kids.sweaterBelowC;
                    k.coat.value = s.kids.coatBelowC;
                    k.freeze.value = s.kids.freezeBelowC;
                    k.gust.value = s.kids.windyGustKmh;

                    const c = document.forms.countdownForm;
                    [1, 2].forEach(i => {
                        const b = s.kids.birthdays[i - 1] || {};
                        c['birthday' + i].value = b.date || '';
                        c['initial' + i].value = b.initial || '';
                    });
                    c.halloween.checked = s.kids.halloween;
                    c.sinterklaas.checked = s.kids.sinterklaas;
                    c.christmas.checked = s.kids.christmas;
                    c.countdownDays.value = s.kids.countdownDays;

                    const w = document.forms.weatherApiForm;
                    w.apiUrl.value = s.weatherApi.url;
                    w.units.value = s.weatherApi.units;
                    document.getElementById('weatherDebug').checked = s.weatherApi.debug;

                    document.getElementById('wifiConnected').textContent = s.wifi.connected ? 'Yes' : 'No';
                    document.getElementById('wifiSignal').textContent = s.wifi.signalStrength + ' dBm';
                    document.forms.wifiForm.ssid.value = s.wifi.ssid;
                    document.forms.wifiForm.password.placeholder = s.wifi.passwordSet ? '(unchanged)' : '(none)';

                    document.getElementById('firmware').textContent = s.firmware;
                    document.getElementById('weatherValid').textContent = s.debug.weatherValid ? 'Yes' : 'No';
                    document.getElementById('weatherAge').textContent = s.debug.weatherAge;
                    document.getElementById('mdnsStarted').textContent = s.debug.mdnsStarted ? 'Yes' : 'No';
                })
                .catch(() => toast('Could not load the device status', false));
        }

        function renderSsidLocations(list) {
            const box = document.getElementById('ssidLocationsList');
            box.replaceChildren();
            const title = document.createElement('b');
            title.textContent = 'Configured SSID Locations:';
            box.appendChild(document.createElement('br'));
            box.appendChild(title);
            box.appendChild(document.createElement('br'));
            if (list.length === 0) {
                box.appendChild(document.createTextNode('None configured'));
                return;
            }
            list.forEach((loc, idx) => {
                box.appendChild(document.createTextNode(loc.ssid + ': ' + loc.lat.toFixed(4) + ', ' + loc.lon.toFixed(4) + ' '));
                const del = document.createElement('button');
                del.textContent = 'Delete';
                del.addEventListener('click', () => {
                    post('/api/ssidlocation/delete', new URLSearchParams({ index: idx }))
                        .then(res => { toast(res.message || 'Done', res.ok); loadStatus(false); })
                        .catch(() => toast('Request failed', false));
                });
                box.appendChild(del);
                box.appendChild(document.createElement('br'));
            });
        }

        loadStatus(true);

        submitForm('thresholdsForm', '/api/thresholds', 'Thresholds saved');
        // Screens: two lists of slots, each a drop-down with move and remove buttons
        const SCREENS = [
            ['ride', 'Ride rating'], ['rideOther', 'Ride rating, other day'], ['week', 'Week grid'],
            ['hours', 'Next hours'], ['clock', 'Clock'], ['weather', 'Kids: weather'],
            ['clothes', 'Kids: clothes'], ['countdown', 'Kids: countdown'], ['report', 'Weather report'], ['village', 'Kids: village']
        ];
        const PRESETS = {
            rider: { tap: ['ride', 'rideOther'], hold: ['week', 'hours', 'clock'] },
            kids: { tap: ['village', 'weather', 'clothes', 'countdown'], hold: ['report'] }
        };
        const MAX_SLOTS = 6;
        function addSlot(boxId, value) {
            const box = document.getElementById(boxId);
            if (box.children.length >= MAX_SLOTS) { toast('At most ' + MAX_SLOTS + ' screens per list', false); return; }
            const row = document.createElement('div');
            const sel = document.createElement('select');
            SCREENS.forEach(([v, t]) => sel.appendChild(option(v, t)));
            sel.value = value || SCREENS[0][0];
            const button = (text, title, fn) => {
                const b = document.createElement('button');
                b.type = 'button'; b.textContent = text; b.title = title;
                b.addEventListener('click', fn);
                return b;
            };
            row.append(sel,
                button('\u2191', 'Move up', () => { if (row.previousElementSibling) box.insertBefore(row, row.previousElementSibling); }),
                button('\u2193', 'Move down', () => { if (row.nextElementSibling) box.insertBefore(row.nextElementSibling, row); }),
                button('\u2715', 'Remove', () => row.remove()));
            box.appendChild(row);
        }
        function setSlots(boxId, names) {
            document.getElementById(boxId).replaceChildren();
            (names || []).forEach(n => addSlot(boxId, n));
        }
        const slotNames = boxId => Array.from(document.querySelectorAll('#' + boxId + ' select')).map(s => s.value);
        document.getElementById('addTap').addEventListener('click', () => addSlot('tapSlots'));
        document.getElementById('addHold').addEventListener('click', () => addSlot('holdSlots'));
        document.getElementById('screenPreset').addEventListener('change', function() {
            const p = PRESETS[this.value];
            if (p) { setSlots('tapSlots', p.tap); setSlots('holdSlots', p.hold); }
            this.value = '';
        });
        document.getElementById('screensForm').addEventListener('submit', e => {
            e.preventDefault();
            const f = document.forms.screensForm;
            post('/api/screens', new URLSearchParams({
                tap: slotNames('tapSlots').join(','), hold: slotNames('holdSlots').join(','),
                returnSeconds: f.returnSeconds.value, cycleSeconds: f.cycleSeconds.value
            }))
                .then(res => {
                    toast(res.message || (res.ok ? 'Screens saved' : 'Error ' + res.status), res.ok);
                    if (res.ok) loadStatus(true);
                })
                .catch(() => toast('Request failed', false));
        });

        submitForm('displayForm', '/api/display', 'Display settings saved');
        const demo = action => post('/api/demo', new URLSearchParams({
                action: action, repeat: document.getElementById('demoRepeat').checked ? '1' : '0' }))
            .then(res => toast(res.message || (res.ok ? 'OK' : 'Error ' + res.status), res.ok))
            .catch(() => toast('Request failed', false));
        document.getElementById('demoStart').addEventListener('click', () => demo('start'));
        document.getElementById('demoStop').addEventListener('click', () => demo('stop'));
        submitForm('kidsForm', '/api/kids', 'Clothing limits saved');
        submitForm('countdownForm', '/api/countdown', 'Countdowns saved');
        submitForm('weatherApiForm', '/api/weatherconfig', 'Weather API configuration saved', () => loadStatus(true));
        submitForm('wifiForm', '/api/wifi/config', 'WiFi settings saved');
        submitForm('locationForm', '/api/location', 'Location set', () => loadStatus(false));
        submitForm('ssidLocationForm', '/api/ssidlocation', 'Location added', () => {
            document.getElementById('ssidLocationForm').reset();
            loadStatus(false);
        });

        // Password change: confirm client-side, then the device restarts
        document.getElementById('passwordForm').addEventListener('submit', e => {
            e.preventDefault();
            const form = e.target;
            if (form.newPassword.value !== document.getElementById('newPassword2').value) {
                toast('The new passwords do not match', false);
                return;
            }
            post('/api/password', new URLSearchParams(new FormData(form)))
                .then(res => {
                    toast(res.message || (res.ok ? 'Password changed' : 'Error ' + res.status), res.ok);
                    if (res.ok) form.reset();
                })
                .catch(() => toast('Request failed', false));
        });

        document.getElementById('scanBtn').addEventListener('click', () => {
            fetch('/api/wifi/scan')
                .then(r => r.json())
                .then(s => {
                    const list = document.getElementById('networksList');
                    list.replaceChildren();
                    const title = document.createElement('b');
                    title.textContent = 'Available Networks:';
                    list.appendChild(document.createElement('br'));
                    list.appendChild(title);
                    list.appendChild(document.createElement('br'));

                    const ssidSelect = document.getElementById('ssidSelect');
                    ssidSelect.replaceChildren(option('', 'Select Network'));

                    s.networks.forEach(net => {
                        const label = net.ssid + ' (' + net.signalStrength + ' dBm)';
                        const b = document.createElement('button');
                        b.textContent = label;
                        b.addEventListener('click', () => { document.forms.wifiForm.ssid.value = net.ssid; });
                        list.appendChild(b);
                        list.appendChild(document.createElement('br'));
                        ssidSelect.appendChild(option(net.ssid, label));
                    });
                })
                .catch(() => toast('Scan failed', false));
        });

        document.getElementById('toggleLogs').addEventListener('click', function() {
            const logDiv = document.getElementById('verboseLogs');
            if (logDiv.style.display === 'block') {
                logDiv.style.display = 'none';
                this.textContent = 'Show Verbose Logs';
                return;
            }
            logDiv.style.display = 'block';
            this.textContent = 'Hide Verbose Logs';
            fetch('/api/logs')
                .then(r => r.json())
                .then(data => { logDiv.textContent = data.logs.join('\n'); })
                .catch(() => toast('Could not load the logs', false));
        });

        // Pull updates from the release relay
        let otaInfo = {};
        function loadOta() {
            return fetch('/api/ota')
                .then(r => r.json())
                .then(o => {
                    otaInfo = o;
                    document.getElementById('otaCurrent').textContent = o.current + ' (' + o.build + ')';
                    document.getElementById('otaLatest').textContent = !o.latest ? 'unknown' :
                        o.latest + (o.available ? ' (new)' : ' (you are up to date)');
                    document.getElementById('otaChecked').textContent = o.checkedMinutesAgo < 0 ? 'not yet' :
                        o.checkedMinutesAgo + ' minutes ago';
                    document.getElementById('otaNotes').textContent = o.available ? o.notes : '';
                    let error = o.error ? 'Last error: ' + o.error : '';
                    if (!o.keySet) {
                        error = 'This build has no update key, so it cannot install updates (see the README).';
                    } else if (o.available && o.size > o.freeSpace) {
                        error = 'Version ' + o.latest + ' (' + o.size + ' bytes) does not fit in the free flash (' +
                            o.freeSpace + ' bytes): flash it over serial once.';
                    }
                    document.getElementById('otaError').textContent = error;
                    const install = document.getElementById('otaInstallBtn');
                    install.style.display = o.available && o.keySet ? 'inline-block' : 'none';
                    install.textContent = 'Install ' + o.latest;
                    const f = document.forms.otaSettingsForm;
                    if (!f.dataset.loaded) {
                        f.url.value = o.url;
                        f.autoCheck.checked = o.autoCheck;
                        f.dataset.loaded = '1';
                    }
                })
                .catch(() => toast('Could not load the update status', false));
        }
        loadOta();
        submitForm('otaSettingsForm', '/api/ota/settings', 'Update settings saved');

        document.getElementById('otaCheckBtn').addEventListener('click', () => {
            const feedback = document.getElementById('otaFeedback');
            feedback.textContent = 'Checking...';
            post('/api/ota/check', '')
                .then(res => {
                    if (!res.ok) { feedback.textContent = res.message || 'Error ' + res.status; return; }
                    // The device answers again once the check is done
                    setTimeout(() => loadOta().then(() => { feedback.textContent = ''; }), 1500);
                })
                .catch(() => { feedback.textContent = 'Request failed'; });
        });

        // After an install or upload: wait until the device is back with another version
        function waitForRestart(feedback) {
            const before = otaInfo.current;
            const poll = () => fetch('/api/ota')
                .then(r => r.json())
                .then(o => {
                    if (o.current !== before) { location.reload(); return; }
                    if (o.error) { feedback.textContent = 'Update failed: ' + o.error; loadOta(); return; }
                    setTimeout(poll, 5000);
                })
                .catch(() => setTimeout(poll, 5000));
            setTimeout(poll, 15000);
        }

        document.getElementById('otaInstallBtn').addEventListener('click', () => {
            if (!confirm('Install version ' + otaInfo.latest + '? The device restarts afterwards.')) return;
            const feedback = document.getElementById('otaFeedback');
            post('/api/ota/install', '')
                .then(res => {
                    feedback.textContent = res.message || 'Error ' + res.status;
                    if (res.ok) waitForRestart(feedback);
                })
                .catch(() => { feedback.textContent = 'Request failed'; });
        });

        // Manual upload: the upload path contains the per-boot token. The update server answers
        // 200 with "Update error: ..." when it rejects an image.
        document.getElementById('otaForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const feedback = document.getElementById('otaUploadFeedback');
            feedback.textContent = 'Uploading...';
            tokenReady
                .then(() => fetch(otaPath, { method: 'POST', headers: { 'X-Dayspeck': csrf }, body: new FormData(this) }))
                .then(r => r.text().then(text => {
                    if (r.ok && !text.startsWith('Update error')) {
                        feedback.textContent = 'Update successful. The device restarts, reload the page in a few seconds.';
                    } else {
                        feedback.textContent = 'Update failed: ' + (r.ok ? text : 'HTTP ' + r.status);
                    }
                }))
                .catch(err => { feedback.textContent = 'Error: ' + err.message; });
        });
    </script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void sendMessage(int code, const char* message) {
    JsonDocument doc;
    doc["message"] = message;
    String output;
    serializeJson(doc, output);
    server.send(code, "application/json", output);
}

static void sendMessage(int code, const String& message) {
    sendMessage(code, message.c_str());
}

// Basic auth with a failure lockout. Sends the 401/429 response itself when it returns false.
static bool authorized() {
    unsigned long now = millis();
    if (authLockUntilMs != 0 && (long)(now - authLockUntilMs) < 0) {
        sendMessage(429, "Too many failed attempts, try again in a minute");
        return false;
    }
    if (server.authenticate(ADMIN_USER, effectivePassword().c_str())) {
        authFailures = 0;
        authLockUntilMs = 0;
        return true;
    }
    if (++authFailures >= AUTH_MAX_FAILURES) {
        authFailures = 0;
        authLockUntilMs = now | 1;
        logMessage("Web UI locked for 60 s after repeated failed logins");
    }
    server.requestAuthentication(BASIC_AUTH, "Dayspeck");
    return false;
}

// Read-only route: authentication only
static ESP8266WebServer::THandlerFunction guarded(void (*handler)()) {
    return [handler]() {
        if (!authorized()) return;
        handler();
    };
}

// State-changing route: authentication, POST and the CSRF token
static ESP8266WebServer::THandlerFunction guardedPost(void (*handler)()) {
    return [handler]() {
        if (!authorized()) return;
        if (server.method() != HTTP_POST) {
            sendMessage(405, "POST required");
            return;
        }
        if (server.header("X-Dayspeck") != csrfToken) {
            sendMessage(403, "Missing or invalid request token, reload the page");
            return;
        }
        handler();
    };
}

// Parse a numeric form field strictly (no empty values, no trailing garbage) within [lo, hi]
static bool argFloat(const char* name, float lo, float hi, float& out) {
    String v = server.arg(name);
    v.trim();
    if (v.length() == 0) return false;
    char* end = nullptr;
    double d = strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0' || isnan(d) || d < lo || d > hi) return false;
    out = (float)d;
    return true;
}

// Read-modify-write config.json. Writes to a temp file first so that power loss during the
// save cannot leave a truncated config (loadConfig() recovers config.tmp if needed).
static bool updateConfig(const std::function<void(JsonDocument&)>& mutate) {
    JsonDocument doc;
    File file = LittleFS.open("/config.json", "r");
    if (file) {
        bool unreadable = (bool)deserializeJson(doc, file);
        file.close();
        if (unreadable) {
            // Keep the broken file for recovery instead of silently destroying it
            doc.clear();
            LittleFS.remove("/config.bad");
            LittleFS.rename("/config.json", "/config.bad");
            logMessage("config.json unreadable, saved as config.bad");
        }
    }

    mutate(doc);
    doc["version"] = CONFIG_VERSION;

    File out = LittleFS.open("/config.tmp", "w");
    if (!out) return false;
    size_t written = serializeJson(doc, out);
    out.close();
    if (written == 0) {
        LittleFS.remove("/config.tmp");
        return false;
    }
    LittleFS.remove("/config.json");
    return LittleFS.rename("/config.tmp", "/config.json");
}

static bool validUrl(const String& url) {
    if (url.length() == 0 || url.length() > MAX_API_URL_LEN) return false;
    if (!url.startsWith("http://")) return false;   // plain HTTP only (no TLS on the ESP8266)
    for (size_t i = 0; i < url.length(); i++) {
        char c = url[i];
        if (c <= ' ' || c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\') return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

static void handleRoot() {
    server.send_P(200, "text/html; charset=utf-8", index_html);
}

// Hands the page its CSRF token and the OTA path. Only same-origin script can read the reply.
static void handleApiToken() {
    JsonDocument doc;
    doc["token"] = csrfToken;
    doc["otaPath"] = otaPath;
    doc["defaultPassword"] = adminPasswordIsDefault();
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void handleApiStatus() {
    JsonDocument doc;

    doc["firmware"] = FW_VERSION " (" FW_GIT_HASH ")";
    doc["lat"] = configLat;
    doc["lon"] = configLon;

    doc["locationName"] = manualLocation ? locationName : String();
    if (manualLocation) {
        doc["locationSource"] = "Manual Selection";
    } else if (ssidBasedLocation) {
        doc["locationSource"] = "SSID-based";
    } else if (manualConfigPresent) {
        doc["locationSource"] = "Configuration file";
    } else {
        doc["locationSource"] = "Default Fallback";
    }

    JsonObject thresholds = doc["thresholds"].to<JsonObject>();
    thresholds["maxRainMm"] = maxRainMm;
    thresholds["maxWindKmh"] = maxWindKmh;
    thresholds["minTempC"] = minTempC;
    thresholds["warnWindKmh"] = warnWindKmh;
    thresholds["rainProbPct"] = rainProbPct;

    JsonObject kids = doc["kids"].to<JsonObject>();
    kids["hotFromC"] = kidsLimits.hotFromC;
    kids["shortsFromC"] = kidsLimits.shortsFromC;
    kids["sweaterBelowC"] = kidsLimits.sweaterBelowC;
    kids["coatBelowC"] = kidsLimits.coatBelowC;
    kids["freezeBelowC"] = kidsLimits.freezeBelowC;
    kids["windyGustKmh"] = kidsLimits.windyGustKmh;
    JsonArray birthdays = kids["birthdays"].to<JsonArray>();
    for (const KidsBirthday& b : kidsBirthdays) {
        if (b.month == 0) continue;
        JsonObject item = birthdays.add<JsonObject>();
        char date[11];
        snprintf(date, sizeof(date), "%04d-%02d-%02d", b.year, b.month, b.day);
        item["date"] = date;
        item["initial"] = b.initial ? String(b.initial) : String();
    }
    kids["halloween"] = (kidsHolidays & KIDS_HOLIDAY(KIDS_EVENT_HALLOWEEN)) != 0;
    kids["sinterklaas"] = (kidsHolidays & KIDS_HOLIDAY(KIDS_EVENT_SINTERKLAAS)) != 0;
    kids["christmas"] = (kidsHolidays & KIDS_HOLIDAY(KIDS_EVENT_CHRISTMAS)) != 0;
    kids["countdownDays"] = kidsCountdownDays;

    JsonObject display = doc["display"].to<JsonObject>();
    display["previewHr"] = previewHr;
    display["dimAtNight"] = displayDimAtNight;
    display["nightBrightness"] = displayNightBrightness;
    display["sleepMinutes"] = displaySleepMinutes;
    display["alwaysSleep"] = displayAlwaysSleep;
    display["touchEnabled"] = displayTouchEnabled;
    display["cycleSeconds"] = displayCycleSeconds;
    JsonObject screens = display["screens"].to<JsonObject>();
    JsonArray tapNames = screens["tap"].to<JsonArray>();
    for (int i = 0; i < screensTap.count; i++) tapNames.add(screenName(screensTap.ids[i]));
    JsonArray holdNames = screens["hold"].to<JsonArray>();
    for (int i = 0; i < screensHold.count; i++) holdNames.add(screenName(screensHold.ids[i]));
    screens["returnSeconds"] = screensReturnSeconds;
    display["language"] = displayLanguage;
    display["quietStart"] = quietStartHr;
    display["quietEnd"] = quietEndHr;

    WeatherData current = getCurrentWeather();
    doc["current"]["tempC"] = current.tempC;
    doc["current"]["windKmh"] = current.windKmh;
    doc["current"]["precipMm"] = current.precipMm;

    doc["wifi"]["connected"] = state.wifiConnected;
    doc["wifi"]["signalStrength"] = state.wifiSignal;
    doc["wifi"]["ssid"] = wifiSsid;
    doc["wifi"]["passwordSet"] = wifiPassword.length() > 0;   // the password itself is never sent

    doc["weatherApi"]["url"] = weatherApiUrl;
    doc["weatherApi"]["units"] = weatherUnits;
    doc["weatherApi"]["debug"] = weatherDebug;

    JsonArray ssidLocs = doc["ssidLocations"].to<JsonArray>();
    for (const auto& loc : ssidLocations) {
        JsonObject item = ssidLocs.add<JsonObject>();
        item["ssid"] = loc.ssid;
        item["lat"] = loc.lat;
        item["lon"] = loc.lon;
    }

    JsonObject debug = doc["debug"].to<JsonObject>();
    debug["weatherValid"] = state.weatherValid;
    debug["weatherAge"] = state.weatherAge;
    debug["mdnsStarted"] = state.mdnsStarted;

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void handleApiThresholds() {
    float rain, wind, temp, warn, prob;
    if (!argFloat("maxRain", 0, 100, rain) || !argFloat("maxWind", 0, 200, wind) ||
        !argFloat("minTemp", -50, 50, temp) || !argFloat("warnWind", 0, 200, warn) ||
        !argFloat("rainProb", 0, 101, prob)) {
        sendMessage(400, "Invalid value: rain 0-100 mm, winds 0-200 km/h, temperature -50 to 50 C, rain chance 0-101 %");
        return;
    }
    if (warn > wind) {
        sendMessage(400, "The warning wind speed cannot exceed the maximum wind speed");
        return;
    }

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["thresholds"]["maxRainMm"] = rain;
        doc["thresholds"]["maxWindKmh"] = wind;
        doc["thresholds"]["minTempC"] = temp;
        doc["thresholds"]["warnWindKmh"] = warn;
        doc["thresholds"]["rainProbPct"] = prob;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    maxRainMm = rain;
    maxWindKmh = wind;
    minTempC = temp;
    warnWindKmh = warn;
    rainProbPct = prob;
    state.fetchNow = true;   // ratings are computed from the forecast: refresh with the new limits
    sendMessage(200, "Thresholds saved");
}

static void handleApiKids() {
    float hot, shorts, sweater, coat, freeze, gust;
    if (!argFloat("hot", -30, 50, hot) || !argFloat("shorts", -30, 50, shorts) ||
        !argFloat("sweater", -30, 50, sweater) || !argFloat("coat", -30, 50, coat) ||
        !argFloat("freeze", -30, 50, freeze) || !argFloat("gust", 1, 150, gust)) {
        sendMessage(400, "Invalid value: temperatures -30 to 50 C, wind 1-150 km/h");
        return;
    }
    KidsLimits k = { hot, shorts, sweater, coat, freeze, gust };
    if (!kidsLimitsValid(k)) {
        sendMessage(400, "The limits must go from warm to cold: sun cap from >= shorts from >= sweater below >= winter coat below >= scarf below");
        return;
    }

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["kids"]["hotFromC"] = hot;
        doc["kids"]["shortsFromC"] = shorts;
        doc["kids"]["sweaterBelowC"] = sweater;
        doc["kids"]["coatBelowC"] = coat;
        doc["kids"]["freezeBelowC"] = freeze;
        doc["kids"]["windyGustKmh"] = gust;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    kidsLimits = k;
    state.displayDirty = true;
    sendMessage(200, "Clothing limits saved");
}

static bool argInt(const char* name, int lo, int hi, int& out) {
    float v;
    if (!argFloat(name, (float)lo, (float)hi, v) || v != (float)(int)v) return false;
    out = (int)v;
    return true;
}

static void handleApiCountdown() {
    int days;
    if (!argInt("countdownDays", 1, KIDS_MAX_COUNTDOWN_DAYS, days)) {
        sendMessage(400, "Invalid value: show from 1-60 sleeps before the day");
        return;
    }
    KidsBirthday b[KIDS_MAX_BIRTHDAYS] = {};
    String dates[KIDS_MAX_BIRTHDAYS];
    for (int i = 0; i < KIDS_MAX_BIRTHDAYS; i++) {
        String n = String(i + 1);
        dates[i] = server.arg("birthday" + n);
        dates[i].trim();
        if (dates[i].length() == 0) continue;   // not set
        if (!parseIsoDate(dates[i].c_str(), b[i].year, b[i].month, b[i].day)) {
            sendMessage(400, "Invalid birthday: use a date of birth (YYYY-MM-DD)");
            return;
        }
        b[i].initial = kidsInitial(server.arg("initial" + n).c_str());
    }
    unsigned holidays = 0;
    if (server.hasArg("halloween")) holidays |= KIDS_HOLIDAY(KIDS_EVENT_HALLOWEEN);
    if (server.hasArg("sinterklaas")) holidays |= KIDS_HOLIDAY(KIDS_EVENT_SINTERKLAAS);
    if (server.hasArg("christmas")) holidays |= KIDS_HOLIDAY(KIDS_EVENT_CHRISTMAS);

    bool saved = updateConfig([&](JsonDocument& doc) {
        JsonArray list = doc["kids"]["birthdays"].to<JsonArray>();
        for (int i = 0; i < KIDS_MAX_BIRTHDAYS; i++) {
            if (b[i].month == 0) continue;
            JsonObject item = list.add<JsonObject>();
            item["date"] = dates[i];
            item["initial"] = b[i].initial ? String(b[i].initial) : String();
        }
        doc["kids"]["halloween"] = (holidays & KIDS_HOLIDAY(KIDS_EVENT_HALLOWEEN)) != 0;
        doc["kids"]["sinterklaas"] = (holidays & KIDS_HOLIDAY(KIDS_EVENT_SINTERKLAAS)) != 0;
        doc["kids"]["christmas"] = (holidays & KIDS_HOLIDAY(KIDS_EVENT_CHRISTMAS)) != 0;
        doc["kids"]["countdownDays"] = days;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    memcpy(kidsBirthdays, b, sizeof(kidsBirthdays));
    kidsHolidays = holidays;
    kidsCountdownDays = days;
    state.displayDirty = true;
    sendMessage(200, "Countdowns saved");
}

static void handleApiDisplay() {
    int preview, sleepMin, qStart, qEnd, nightPct;
    if (!argInt("previewHr", 0, 24, preview) || !argInt("sleepMinutes", 0, 600, sleepMin) ||
        !argInt("quietStart", -1, 23, qStart) || !argInt("quietEnd", -1, 23, qEnd) ||
        !argInt("nightBrightness", 1, 100, nightPct)) {
        sendMessage(400, "Invalid value: hours 0-24 (quiet hours -1 to 23), sleep 0-600 minutes, night brightness 1-100 %");
        return;
    }
    if ((qStart < 0) != (qEnd < 0)) {
        sendMessage(400, "Set both the start and the end of the quiet hours, or neither (-1)");
        return;
    }
    bool dim = server.hasArg("dimAtNight");
    bool alwaysSleep = server.hasArg("alwaysSleep");
    bool touchOn = server.hasArg("touchEnabled");
    if (!touchOn && alwaysSleep) {
        sendMessage(400, "Always sleep needs the touch sensor: it is the only way to wake the screen");
        return;
    }
    String language = server.arg("language");
    if (language != "en" && language != "nl") language = "en";

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["previewHr"] = preview;
        doc["display"]["dimAtNight"] = dim;
        doc["display"]["nightBrightness"] = nightPct;
        doc["display"]["sleepMinutes"] = sleepMin;
        doc["display"]["alwaysSleep"] = alwaysSleep;
        doc["display"]["touchEnabled"] = touchOn;
        doc["display"]["language"] = language;
        doc["display"]["quietStart"] = qStart;
        doc["display"]["quietEnd"] = qEnd;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    previewHr = preview;
    displayDimAtNight = dim;
    displayNightBrightness = nightPct;
    displaySleepMinutes = sleepMin;
    displayAlwaysSleep = alwaysSleep;
    if (touchOn && !displayTouchEnabled) touch_init();   // switched on while running: set the pin up now
    displayTouchEnabled = touchOn;
    displayLanguage = language;
    quietStartHr = qStart;
    quietEndHr = qEnd;
    state.displayDirty = true;
    sendMessage(200, "Display settings saved");
}

// A comma-separated list of screen names ("" = empty), at most MAX_SCREEN_SLOTS
static bool parseScreenList(const String& text, ScreenList& out) {
    out.count = 0;
    int start = 0;
    while (start < (int)text.length()) {
        int comma = text.indexOf(',', start);
        if (comma < 0) comma = text.length();
        String name = text.substring(start, comma);
        name.trim();
        int id = screenFromName(name.c_str());
        if (id < 0 || out.count >= MAX_SCREEN_SLOTS) return false;
        out.ids[out.count++] = (uint8_t)id;
        start = comma + 1;
    }
    return true;
}

static void handleApiDemo() {
    String action = server.arg("action");
    if (action == "start") {
        startDemo(server.arg("repeat") == "1");
        sendMessage(200, "Demo started");
    } else if (action == "stop") {
        stopDemo();
        sendMessage(200, "Demo stopped");
    } else {
        sendMessage(400, "action must be start or stop");
    }
}

static void handleApiScreens() {
    ScreenList tap = {}, hold = {};
    int returnSecs, cycleSecs;
    if (!parseScreenList(server.arg("tap"), tap) || !parseScreenList(server.arg("hold"), hold)) {
        sendMessage(400, "Unknown screen, or more than 6 in a list");
        return;
    }
    if (!screenListsValid(tap, hold)) {
        sendMessage(400, "The tap list needs at least one screen, and its first one (the home screen) cannot be the clock or the countdown");
        return;
    }
    if (!argInt("returnSeconds", 0, 3600, returnSecs) || !argInt("cycleSeconds", 0, 3600, cycleSecs) || cycleSecs == 1) {
        sendMessage(400, "Back to home: 0-3600 seconds; cycle: 0 (off) or 2-3600 seconds");
        return;
    }

    bool saved = updateConfig([&](JsonDocument& doc) {
        JsonObject screens = doc["display"]["screens"].to<JsonObject>();
        JsonArray t = screens["tap"].to<JsonArray>();
        for (int i = 0; i < tap.count; i++) t.add(screenName(tap.ids[i]));
        JsonArray h = screens["hold"].to<JsonArray>();
        for (int i = 0; i < hold.count; i++) h.add(screenName(hold.ids[i]));
        screens["returnSeconds"] = returnSecs;
        doc["display"]["cycleSeconds"] = cycleSecs;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    screensTap = tap;
    screensHold = hold;
    screensReturnSeconds = returnSecs;
    displayCycleSeconds = cycleSecs;
    state.screen = ScreenNav{ 0, 0 };   // start at the (new) home screen
    state.screenEnteredMs = millis();
    state.lastCycleMs = millis();
    state.displayDirty = true;
    sendMessage(200, "Screens saved");
}

static void handleApiLocation() {
    float lat, lon;
    if (!argFloat("lat", -90, 90, lat) || !argFloat("lon", -180, 180, lon)) {
        sendMessage(400, "Latitude must be -90 to 90 and longitude -180 to 180");
        return;
    }
    // Two decimals (about 1 km) is plenty for a forecast grid of a few km, and does not pinpoint a house
    lat = roundf(lat * 100.0f) / 100.0f;
    lon = roundf(lon * 100.0f) / 100.0f;
    String name = server.arg("place");
    name.trim();
    if (name.length() > MAX_PLACE_NAME_LEN) name = name.substring(0, MAX_PLACE_NAME_LEN);

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["lat"] = lat;
        doc["lon"] = lon;
        doc["locationName"] = name;
        doc["manualLocation"] = true;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    configLat = lat;
    configLon = lon;
    locationName = name;
    manualLocation = true;
    state.fetchNow = true;
    char coords[32];
    snprintf(coords, sizeof(coords), "%.2f, %.2f", lat, lon);
    sendMessage(200, "Location set to " + (name.length() ? name + " (" + coords + ")" : String(coords)));
}

static void handleApiWifiScan() {
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    int apCount = WiFi.scanNetworks();
    for (int i = 0; i < apCount; i++) {
        JsonObject ap = networks.add<JsonObject>();
        ap["ssid"] = WiFi.SSID(i);
        ap["signalStrength"] = WiFi.RSSI(i);
        ap["encryptionType"] = WiFi.encryptionType(i);
    }
    WiFi.scanDelete();

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void handleApiLogs() {
    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();

    for (size_t i = 0; i < getLogCount(); i++) {
        logs.add(String(getLogEntry(i)));
    }

    logs.add("[SYS] Free heap: " + String(system_get_free_heap_size()) + " bytes");
    logs.add("[SYS] Weather valid: " + String(state.weatherValid ? "yes" : "no"));
    logs.add("[SYS] Weather age: " + String(state.weatherAge) + " min");
    logs.add("[SYS] mDNS: " + String(state.mdnsStarted ? "started" : "not started"));

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void handleApiWifiConfig() {
    String newSsid = server.arg("ssid");
    String newPassword = server.arg("password");

    if (newSsid.length() == 0 || newSsid.length() > MAX_SSID_LEN) {
        sendMessage(400, "The network name must be 1-32 characters");
        return;
    }
    // An empty password field means "unchanged" for the current network
    if (newPassword.length() == 0 && newSsid == wifiSsid) {
        newPassword = wifiPassword;
    }
    if (newPassword.length() != 0 && (newPassword.length() < PASSWORD_MIN_LEN || newPassword.length() > PASSWORD_MAX_LEN)) {
        sendMessage(400, "A WiFi password must be 8-63 characters (leave empty for an open network)");
        return;
    }

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["wifi"]["ssid"] = newSsid;
        doc["wifi"]["password"] = newPassword;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }

    wifiSsid = newSsid;
    wifiPassword = newPassword;

    // Reconnect with the new credentials; if they are wrong the setup AP comes back after 30 s
    state.everConnected = false;
    state.disconnectedSinceMs = 0;
    WiFi.disconnect();
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

    sendMessage(200, "WiFi settings saved, connecting...");
}

static void handleApiWeatherConfig() {
    String apiUrl = server.arg("apiUrl");
    String units = server.arg("units");
    bool debug = server.hasArg("weatherDebug");

    apiUrl.trim();
    if (!validUrl(apiUrl)) {
        sendMessage(400, "The API URL must start with http:// (max 128 characters; https is not supported)");
        return;
    }
    if (units != "metric" && units != "imperial") {
        sendMessage(400, "Units must be metric or imperial");
        return;
    }

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["weatherApiUrl"] = apiUrl;
        doc["weatherUnits"] = units;
        doc["weatherDebug"] = debug;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }

    weatherApiUrl = apiUrl;
    weatherUnits = units;
    weatherDebug = debug;
    state.fetchNow = true;
    sendMessage(200, "Weather API configuration saved");
}

// Persist the whole ssidLocations list
static bool saveSsidLocations() {
    return updateConfig([](JsonDocument& doc) {
        JsonArray arr = doc["ssidLocations"].to<JsonArray>();
        for (const auto& l : ssidLocations) {
            JsonObject item = arr.add<JsonObject>();
            item["ssid"] = l.ssid;
            item["lat"] = l.lat;
            item["lon"] = l.lon;
        }
    });
}

static void handleApiSsidLocation() {
    String ssid = server.arg("ssid");
    float lat, lon;
    if (ssid.length() == 0 || ssid.length() > MAX_SSID_LEN) {
        sendMessage(400, "The network name must be 1-32 characters");
        return;
    }
    if (!argFloat("lat", -90, 90, lat) || !argFloat("lon", -180, 180, lon)) {
        sendMessage(400, "Latitude must be -90 to 90 and longitude -180 to 180");
        return;
    }
    if (ssidLocations.size() >= MAX_SSID_LOCATIONS) {
        sendMessage(400, "At most 10 network locations can be stored");
        return;
    }

    SsidLocation loc;
    loc.ssid = ssid;
    loc.lat = lat;
    loc.lon = lon;
    ssidLocations.push_back(loc);
    if (!saveSsidLocations()) {
        ssidLocations.pop_back();
        sendMessage(500, "Could not save configuration");
        return;
    }
    sendMessage(200, "Location added for " + ssid);
}

static void handleApiSsidLocationDelete() {
    String arg = server.arg("index");
    int index = arg.toInt();
    if (arg.length() == 0 || index < 0 || index >= (int)ssidLocations.size()) {
        sendMessage(400, "Unknown entry");
        return;
    }

    SsidLocation removed = ssidLocations[index];
    ssidLocations.erase(ssidLocations.begin() + index);
    if (!saveSsidLocations()) {
        ssidLocations.insert(ssidLocations.begin() + index, removed);
        sendMessage(500, "Could not save configuration");
        return;
    }
    sendMessage(200, "Entry removed");
}

// Change the admin / setup-AP password. Takes effect after a reboot (OTA and the AP use it).
static void handleApiPassword() {
    String current = server.arg("current");
    String newPassword = server.arg("newPassword");

    if (current != effectivePassword()) {
        sendMessage(403, "The current password is incorrect");
        return;
    }
    if (newPassword.length() < PASSWORD_MIN_LEN || newPassword.length() > PASSWORD_MAX_LEN) {
        sendMessage(400, "The new password must be 8-63 characters");
        return;
    }

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["auth"]["password"] = newPassword;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }

    adminPassword = newPassword;
    rebootAtMs = (millis() + 1500) | 1;
    sendMessage(200, "Password changed. The device restarts now; sign in again with the new password.");
}

// Pull update status for the Firmware Update card
static void handleApiOta() {
    JsonDocument doc;
    doc["current"] = FW_VERSION;
    doc["build"] = FW_GIT_HASH;
    doc["variant"] = OTA_VARIANT;
    doc["keySet"] = otaStatus.keySet;
    doc["url"] = otaServerUrl;
    doc["autoCheck"] = otaAutoCheck;
    doc["checkedMinutesAgo"] = otaStatus.checked ? (long)((millis() - otaStatus.lastCheckMs) / 60000UL) : -1L;
    doc["latest"] = otaStatus.latestVersion;
    doc["notes"] = otaStatus.notes;
    doc["available"] = otaStatus.available;
    doc["size"] = otaStatus.size;
    doc["freeSpace"] = otaFreeSpace();
    doc["error"] = otaStatus.error;
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void handleApiOtaCheck() {
    if (!state.wifiConnected) {
        sendMessage(409, "Not connected to WiFi");
        return;
    }
    if (otaServerUrl.length() == 0) {
        sendMessage(409, "Set the update server first");
        return;
    }
    otaRequestCheck();
    sendMessage(202, "Checking for updates...");
}

static void handleApiOtaInstall() {
    if (!otaStatus.keySet) {
        sendMessage(409, "This build has no update key");
        return;
    }
    if (!otaStatus.available) {
        sendMessage(409, "No update available, check first");
        return;
    }
    if (otaStatus.size > otaFreeSpace()) {
        sendMessage(409, "The update does not fit in the free flash, flash it over serial once");
        return;
    }
    otaRequestInstall();
    sendMessage(202, "Installing " + otaStatus.latestVersion + ". The display shows the progress; the device "
                     "restarts when it is done (about a minute). This page reloads by itself.");
}

static void handleApiOtaSettings() {
    String url = server.arg("url");
    bool autoCheck = server.hasArg("autoCheck");
    url.trim();
    while (url.endsWith("/")) url.remove(url.length() - 1);
    if (url.length() > 0 && !validUrl(url)) {
        sendMessage(400, "The update server must start with http:// (max 128 characters; https is not supported)");
        return;
    }

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["ota"]["url"] = url;
        doc["ota"]["autoCheck"] = autoCheck;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    otaServerUrl = url;
    otaAutoCheck = autoCheck;
    sendMessage(200, "Update settings saved");
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

String webOtaPath() {
    return otaPath;
}

void initWebServer() {
    // 64 random bits from the hardware RNG, new on every boot
    char token[17];
    snprintf(token, sizeof(token), "%08x%08x", (unsigned int)RANDOM_REG32, (unsigned int)RANDOM_REG32);
    csrfToken = token;
    otaPath = "/update-" + csrfToken;

    server.collectHeaders("X-Dayspeck");

    server.on("/", guarded(handleRoot));
    server.on("/api/token", guarded(handleApiToken));
    server.on("/api/status", guarded(handleApiStatus));
    server.on("/api/logs", guarded(handleApiLogs));
    server.on("/api/wifi/scan", guarded(handleApiWifiScan));
    server.on("/api/ota", guarded(handleApiOta));

    server.on("/api/thresholds", guardedPost(handleApiThresholds));
    server.on("/api/display", guardedPost(handleApiDisplay));
    server.on("/api/screens", guardedPost(handleApiScreens));
    server.on("/api/demo", guardedPost(handleApiDemo));
    server.on("/api/kids", guardedPost(handleApiKids));
    server.on("/api/countdown", guardedPost(handleApiCountdown));
    server.on("/api/location", guardedPost(handleApiLocation));
    server.on("/api/wifi/config", guardedPost(handleApiWifiConfig));
    server.on("/api/weatherconfig", guardedPost(handleApiWeatherConfig));
    server.on("/api/ssidlocation", guardedPost(handleApiSsidLocation));
    server.on("/api/ssidlocation/delete", guardedPost(handleApiSsidLocationDelete));
    server.on("/api/password", guardedPost(handleApiPassword));
    server.on("/api/ota/check", guardedPost(handleApiOtaCheck));
    server.on("/api/ota/install", guardedPost(handleApiOtaInstall));
    server.on("/api/ota/settings", guardedPost(handleApiOtaSettings));

    server.onNotFound([]() { sendMessage(404, "Not found"); });
    server.begin();
}

void handleWebServer() {
    server.handleClient();
    if (rebootAtMs != 0 && (long)(millis() - rebootAtMs) >= 0) {
        ESP.restart();
    }
}
