// WeatherWise Bedside Display — Embedded Web Server Implementation
//
// Security model:
//  * every route requires the admin password (HTTP Basic auth, user "admin"); repeated failures
//    lock the device out for a minute
//  * state-changing routes must be POST and carry the per-boot token in the X-WeatherWise header.
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
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <functional>
#include <stdlib.h>
#include <vector>

ESP8266WebServer server(80);

#define MAX_SSID_LOCATIONS   10
#define MAX_SSID_LEN         32
#define MAX_API_URL_LEN      128
#define AUTH_MAX_FAILURES    10
#define AUTH_LOCKOUT_MS      60000UL

static String csrfToken;
static String otaPath = "/update";
static unsigned long rebootAtMs = 0;

static uint8_t authFailures = 0;
static unsigned long authLockUntilMs = 0;

// Predefined locations: countries and cities with lat/lon
static const char locationsJson[] PROGMEM = R"JSON(
{
  "United States": {
    "New York": {"lat": 40.7128, "lon": -74.0060},
    "Los Angeles": {"lat": 34.0522, "lon": -118.2437},
    "Chicago": {"lat": 41.8781, "lon": -87.6298},
    "Houston": {"lat": 29.7604, "lon": -95.3698},
    "Phoenix": {"lat": 33.4484, "lon": -112.0740}
  },
  "United Kingdom": {
    "London": {"lat": 51.5074, "lon": -0.1278},
    "Manchester": {"lat": 53.4808, "lon": -2.2426},
    "Birmingham": {"lat": 52.4862, "lon": -1.8904},
    "Glasgow": {"lat": 55.8642, "lon": -4.2518},
    "Liverpool": {"lat": 53.4084, "lon": -2.9916}
  },
  "Germany": {
    "Berlin": {"lat": 52.5200, "lon": 13.4050},
    "Munich": {"lat": 48.1351, "lon": 11.5820},
    "Hamburg": {"lat": 53.5511, "lon": 9.9937},
    "Cologne": {"lat": 50.9375, "lon": 6.9603},
    "Frankfurt": {"lat": 50.1109, "lon": 8.6821}
  },
  "France": {
    "Paris": {"lat": 48.8566, "lon": 2.3522},
    "Marseille": {"lat": 43.2965, "lon": 5.3698},
    "Lyon": {"lat": 45.7640, "lon": 4.8357},
    "Toulouse": {"lat": 43.6047, "lon": 1.4442},
    "Nice": {"lat": 43.7102, "lon": 7.2620}
  },
  "Italy": {
    "Rome": {"lat": 41.9028, "lon": 12.4964},
    "Milan": {"lat": 45.4642, "lon": 9.1900},
    "Naples": {"lat": 40.8518, "lon": 14.2681},
    "Turin": {"lat": 45.0703, "lon": 7.6869},
    "Palermo": {"lat": 38.1157, "lon": 13.3615}
  },
  "Spain": {
    "Madrid": {"lat": 40.4168, "lon": -3.7038},
    "Barcelona": {"lat": 41.3851, "lon": 2.1734},
    "Valencia": {"lat": 39.4699, "lon": -0.3763},
    "Seville": {"lat": 37.3886, "lon": -5.9823},
    "Zaragoza": {"lat": 41.6488, "lon": -0.8891}
  },
  "Netherlands": {
    "Amsterdam": {"lat": 52.3676, "lon": 4.9041},
    "Rotterdam": {"lat": 51.9244, "lon": 4.4777},
    "The Hague": {"lat": 52.0705, "lon": 4.3007},
    "Utrecht": {"lat": 52.0907, "lon": 5.1214},
    "Eindhoven": {"lat": 51.4416, "lon": 5.4697}
  },
  "Canada": {
    "Toronto": {"lat": 43.6532, "lon": -79.3832},
    "Vancouver": {"lat": 49.2827, "lon": -123.1207},
    "Montreal": {"lat": 45.5017, "lon": -73.5673},
    "Calgary": {"lat": 51.0447, "lon": -114.0719},
    "Ottawa": {"lat": 45.4215, "lon": -75.6972}
  },
  "Australia": {
    "Sydney": {"lat": -33.8688, "lon": 151.2093},
    "Melbourne": {"lat": -37.8136, "lon": 144.9631},
    "Brisbane": {"lat": -27.4698, "lon": 153.0251},
    "Perth": {"lat": -31.9505, "lon": 115.8605},
    "Adelaide": {"lat": -34.9285, "lon": 138.6007}
  },
  "Japan": {
    "Tokyo": {"lat": 35.6762, "lon": 139.6503},
    "Osaka": {"lat": 34.6937, "lon": 135.5023},
    "Nagoya": {"lat": 35.1815, "lon": 136.9066},
    "Sapporo": {"lat": 43.0618, "lon": 141.3545},
    "Fukuoka": {"lat": 33.5904, "lon": 130.4017}
  }
}
)JSON";


static const char index_html[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>WeatherWise Status</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: system-ui; max-width: 600px; margin: 0 auto; padding: 1rem; }
        .card { border: 1px solid #ddd; border-radius: 8px; padding: 1rem; margin: 1rem 0; }
        .label { font-weight: 600; display: inline-block; width: 180px; }
        input { width: 80px; }
        input[type=text], input[type=password], input[type=url] { width: 200px; }
        button { padding: 0.5rem 1rem; margin-top: 1rem; }
        #toast { display: none; position: sticky; top: 0; padding: 0.6rem 1rem; border-radius: 6px; color: #fff; z-index: 5; }
        #toast.ok { background: #2a7a3b; }
        #toast.err { background: #b3261e; }
        .banner { background: #fff3cd; border: 1px solid #e0b100; border-radius: 8px; padding: 0.75rem 1rem; margin: 1rem 0; }
        #verboseLogs { display: none; margin-top: 10px; font-family: monospace; font-size: 12px; background: #222; color: #0f0; padding: 8px; max-height: 300px; overflow-y: auto; white-space: pre-wrap; }
    </style>
</head>
<body>
    <div id="toast"></div>
    <h1>WeatherWise</h1>
    <div id="pwBanner" class="banner" style="display:none">
        This device still uses its default password (shown on the display during WiFi setup).
        Please set your own below.
    </div>

    <div class="card">
        <h3>Location</h3>
        <span class="label">Latitude:</span> <span id="lat"></span><br>
        <span class="label">Longitude:</span> <span id="lon"></span><br>
        <span class="label">Source:</span> <span id="locationSource"></span>
    </div>

    <div class="card">
        <h3>Manual Location Selection</h3>
        <form id="locationForm">
            <span class="label">Country:</span>
            <select name="country" id="countrySelect" required>
                <option value="">Select Country</option>
            </select><br>
            <span class="label">City:</span>
            <select name="city" id="citySelect" required>
                <option value="">Select City</option>
            </select><br>
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
        <h3>Display</h3>
        <form id="displayForm">
            <span class="label">Tomorrow from (hour):</span> <input name="previewHr" type="number" min="0" max="24" title="24 = never"> <small>24 = never</small><br>
            <span class="label">Dim at night:</span> <input name="dimAtNight" type="checkbox"> <small>a touch gives full brightness for 30 s</small><br>
            <span class="label">Night brightness (%):</span> <input name="nightBrightness" type="number" min="1" max="100"> <small>raise it if the screen looks blank at night</small><br>
            <span class="label">Sleep at night after (min):</span> <input name="sleepMinutes" type="number" min="0" max="600"> <small>0 = never; a touch wakes it</small><br>
            <span class="label">Always sleep:</span> <input name="alwaysSleep" type="checkbox"> <small>screen off; a touch wakes it for 30 s</small><br>
            <span class="label">Language (kids build):</span> <select name="language"><option value="en">English</option><option value="nl">Nederlands</option></select><br>
            <span class="label">Screen off from (hour):</span> <input name="quietStart" type="number" min="-1" max="23"> <small>-1 = off</small><br>
            <span class="label">Screen off until (hour):</span> <input name="quietEnd" type="number" min="-1" max="23"><br>
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
            <span class="label">Update server:</span> <input name="url" type="url" maxlength="128" placeholder="http://motoclock-ota.you.workers.dev"><br>
            <span class="label">Check daily:</span> <input name="autoCheck" type="checkbox"><br>
            <button type="submit">Save Settings</button>
        </form>
        <h4>Manual upload</h4>
        <p><small>A signed <code>motoclock-rider.bin.gz</code> or <code>motoclock-kids.bin.gz</code> from a release.</small></p>
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
                .then(() => fetch(path, { method: 'POST', headers: { 'X-WeatherWise': csrf }, body: body }))
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
        let locations = {};
        const countrySelect = document.getElementById('countrySelect');
        const citySelect = document.getElementById('citySelect');

        fetch('/api/locations')
            .then(r => r.json())
            .then(data => {
                locations = data;
                Object.keys(locations).forEach(country => countrySelect.appendChild(option(country, country)));
            })
            .catch(() => toast('Could not load the location list', false));

        countrySelect.addEventListener('change', function() {
            citySelect.replaceChildren(option('', 'Select City'));
            if (locations[this.value]) {
                Object.keys(locations[this.value]).forEach(city => citySelect.appendChild(option(city, city)));
            }
        });

        function loadStatus(full) {
            return fetch('/api/status')
                .then(r => r.json())
                .then(s => {
                    document.getElementById('lat').textContent = s.lat.toFixed(4);
                    document.getElementById('lon').textContent = s.lon.toFixed(4);
                    document.getElementById('locationSource').textContent = s.locationSource;
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
                    d.language.value = s.display.language;
                    d.quietStart.value = s.display.quietStart;
                    d.quietEnd.value = s.display.quietEnd;

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
        submitForm('displayForm', '/api/display', 'Display settings saved');
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
                    document.getElementById('otaCurrent').textContent = o.current + ' (' + o.build + ', ' + o.variant + ' build)';
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
                .then(() => fetch(otaPath, { method: 'POST', headers: { 'X-WeatherWise': csrf }, body: new FormData(this) }))
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
    server.requestAuthentication(BASIC_AUTH, "WeatherWise");
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
        if (server.header("X-WeatherWise") != csrfToken) {
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

static void handleApiLocations() {
    server.send_P(200, "application/json", locationsJson);
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

    JsonObject display = doc["display"].to<JsonObject>();
    display["previewHr"] = previewHr;
    display["dimAtNight"] = displayDimAtNight;
    display["nightBrightness"] = displayNightBrightness;
    display["sleepMinutes"] = displaySleepMinutes;
    display["alwaysSleep"] = displayAlwaysSleep;
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

static bool argInt(const char* name, int lo, int hi, int& out) {
    float v;
    if (!argFloat(name, (float)lo, (float)hi, v) || v != (float)(int)v) return false;
    out = (int)v;
    return true;
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
    String language = server.arg("language");
    if (language != "en" && language != "nl") language = "en";

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["previewHr"] = preview;
        doc["display"]["dimAtNight"] = dim;
        doc["display"]["nightBrightness"] = nightPct;
        doc["display"]["sleepMinutes"] = sleepMin;
        doc["display"]["alwaysSleep"] = alwaysSleep;
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
    displayLanguage = language;
    quietStartHr = qStart;
    quietEndHr = qEnd;
    state.displayDirty = true;
    sendMessage(200, "Display settings saved");
}

static void handleApiLocation() {
    String country = server.arg("country");
    String city = server.arg("city");

    JsonDocument locations;
    if (deserializeJson(locations, (const __FlashStringHelper*)locationsJson) ||
        country.length() == 0 || city.length() == 0 || locations[country][city].isNull()) {
        sendMessage(400, "Invalid country or city selected");
        return;
    }
    float lat = locations[country][city]["lat"];
    float lon = locations[country][city]["lon"];

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["lat"] = lat;
        doc["lon"] = lon;
        doc["manualLocation"] = true;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    configLat = lat;
    configLon = lon;
    manualLocation = true;
    state.fetchNow = true;
    sendMessage(200, "Location set to " + city + ", " + country);
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

    server.collectHeaders("X-WeatherWise");

    server.on("/", guarded(handleRoot));
    server.on("/api/token", guarded(handleApiToken));
    server.on("/api/status", guarded(handleApiStatus));
    server.on("/api/locations", guarded(handleApiLocations));
    server.on("/api/logs", guarded(handleApiLogs));
    server.on("/api/wifi/scan", guarded(handleApiWifiScan));
    server.on("/api/ota", guarded(handleApiOta));

    server.on("/api/thresholds", guardedPost(handleApiThresholds));
    server.on("/api/display", guardedPost(handleApiDisplay));
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
