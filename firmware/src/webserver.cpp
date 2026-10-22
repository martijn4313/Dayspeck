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
<html lang="en">
<head>
<meta charset="utf-8">
<title>Dayspeck settings</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root{color-scheme:light;--ink:#203438;--muted:#53676a;--accent:#126b62;--line:#d8e2df;}
*{box-sizing:border-box;}
html{scroll-padding-top:6rem;}
body{font:16px/1.5 system-ui,sans-serif;color:var(--ink);background:#f4f7f5;max-width:880px;margin:0 auto;padding:1rem;}
h1,h2,h3,h4,p{margin:0 0 .8rem;}
h1{font-size:1.8rem;}
h2{font-size:1.4rem;}
h3{font-size:1.15rem;}
h4{font-size:1rem;margin-top:1rem;}
a{color:var(--accent);overflow-wrap:anywhere;}
header{display:flex;align-items:baseline;justify-content:space-between;gap:1rem;padding:.5rem 0;}
header p{color:var(--muted);}
.jump-nav{position:sticky;top:0;display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:.25rem;padding:.4rem;border:1px solid var(--line);border-radius:12px;background:#fff;z-index:4;}
.jump-nav a{display:flex;align-items:center;justify-content:center;text-align:center;min-height:44px;padding:.4rem;text-decoration:none;font-weight:600;font-size:.9rem;border-radius:8px;}
.jump-nav a:hover,.jump-nav a:focus-visible{background:#e8f2ed;}
section{margin-top:2rem;}
section,.card,details{scroll-margin-top:6rem;}
.card{border:1px solid var(--line);border-radius:12px;background:#fff;padding:1.25rem;margin:1rem 0;}
.overview-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1rem;}
.overview-grid dt{color:var(--muted);font-size:.85rem;}
.overview-grid dd{margin:.15rem 0 0;font-weight:600;overflow-wrap:anywhere;}
.overview-grid .wide{grid-column:1 / -1;}
.label{font-weight:600;}
.stat-row{display:flex;flex-wrap:wrap;gap:.3rem 1rem;margin:.3rem 0;overflow-wrap:anywhere;}
.stat-row .label{min-width:10rem;}
.field{display:grid;grid-template-columns:14rem minmax(0,1fr);align-items:start;gap:.5rem 1rem;margin:1rem 0;}
.field>label{font-weight:600;padding-top:.6rem;}
.control{min-width:0;display:flex;align-items:center;flex-wrap:wrap;gap:.5rem;}
input,select,button{font:inherit;}
input,select{color:var(--ink);background:#fff;border:1px solid #97aaa5;border-radius:6px;min-height:44px;padding:.5rem;max-width:100%;min-width:0;}
input[type=text],input[type=password],input[type=url],input[type=file]{width:100%;}
input[type=number]{width:8rem;}
input[type=date]{width:11rem;}
input.letter{width:3rem;}
input[type=checkbox]{width:22px;min-height:22px;height:22px;accent-color:var(--accent);flex-shrink:0;}
.field.check{display:block;}
.check>label{display:flex;align-items:center;gap:.75rem;min-height:44px;padding:0;cursor:pointer;}
small,.help{color:var(--muted);font-size:.88rem;}
.control small{display:block;flex-basis:100%;}
.check small{display:block;margin-left:2.15rem;}
button{min-height:44px;padding:.55rem .9rem;border:1px solid #97aaa5;border-radius:6px;background:#f5f8f6;color:var(--ink);cursor:pointer;}
button:hover{background:#e8f2ed;}
button:disabled{opacity:.5;cursor:default;}
form>button[type=submit],#otaInstallBtn{background:var(--accent);color:white;border-color:var(--accent);margin-top:.5rem;}
:focus-visible{outline:3px solid #b77315;outline-offset:3px;}
details{margin:1rem 0;}
summary{cursor:pointer;font-weight:600;min-height:44px;padding:.6rem 0;}
details[open]>summary{margin-bottom:.8rem;}
.slots{margin:.5rem 0;}
.slot-row{display:grid;grid-template-columns:4.5rem minmax(0,1fr) auto;align-items:center;gap:.5rem;padding:.6rem;margin:.5rem 0;background:#f4f7f5;border-radius:8px;}
.slot-row select{font-size:.9rem;}
.slot-label{font-size:.85rem;font-weight:600;}
.slot-actions{display:flex;gap:.25rem;}
.slot-actions button{min-width:44px;padding:.5rem;}
.form-feedback{min-height:1.5rem;margin:.6rem 0 0;font-size:.9rem;color:var(--muted);}
.form-feedback.ok{color:#226c38;}
.form-feedback.err{color:#ac261f;}
#toast{display:none;position:fixed;bottom:1rem;right:1rem;max-width:min(28rem,calc(100vw - 2rem));padding:.8rem 1rem;border-radius:8px;color:#fff;z-index:5;}
#toast.ok{background:#226c38;}
#toast.err{background:#ac261f;}
#placeResults button,#networksList button{display:block;width:100%;text-align:left;margin:.5rem 0;overflow-wrap:anywhere;}
.banner{background:#fff3cd;border:1px solid #d8b349;border-radius:8px;padding:.85rem 1rem;margin:1rem 0;}
#verboseLogs{display:none;margin-top:10px;font-family:monospace;font-size:12px;background:#182626;color:#bce6cc;padding:.8rem;max-height:300px;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere;}
@media (max-width:600px){body{padding:.75rem;}
header{display:block;}
header p{font-size:.9rem;}
.card{padding:1rem;}
.field{grid-template-columns:minmax(0,1fr);gap:.3rem;}
.field>label{padding-top:0;}
.slot-row{grid-template-columns:4rem minmax(0,1fr);}
.slot-actions{grid-column:2;}
.jump-nav a{font-size:.8rem;}
.stat-row .label{min-width:0;}
}
</style>
</head>
<body>
<header><h1>Dayspeck</h1><p>Device settings</p></header>
<nav class="jump-nav" aria-label="Settings sections">
<a href="#overview">Overview</a><a href="#display">Display</a><a href="#connection">Location &amp; WiFi</a><a href="#device">Device</a>
</nav>
<div id="toast" role="status" aria-live="polite"></div>
<div id="pwBanner" class="banner" style="display:none">
This device uses its default password. <a href="#passwordCard">Set your own admin password</a>.
The current password is shown on the display during setup and after connecting.
</div>
<main>
<section id="overview" aria-labelledby="overviewHeading">
<h2 id="overviewHeading">Overview</h2>
<div class="card">
<dl class="overview-grid">
<div class="wide"><dt>Device address</dt><dd><a id="overviewHost" href="#"></a></dd></div>
<div><dt>WiFi</dt><dd id="overviewWifi">Loading...</dd></div>
<div><dt>Weather</dt><dd id="overviewWeather">Loading...</dd></div>
<div class="wide"><dt>Location</dt><dd id="overviewLocation">Loading...</dd></div>
<div class="wide"><dt>Firmware</dt><dd id="overviewFirmware">Loading...</dd></div>
</dl>
<a href="#screensCard">Choose your screens</a> &middot; <a href="#wifiCard">Configure WiFi</a>
</div>
</section>
<section id="display" aria-labelledby="displayHeading">
<h2 id="displayHeading">Display</h2>
<div class="card" id="screensCard">        <h3>Screens</h3>
<p><small>Choose a preset or build your own lists. Tap cycles through the first list; its first screen is Home.
Long press cycles through the second list, then returns Home. With an empty second list, long press acts like a tap.</small></p>
<form id="screensForm">
<div class="field"><label for="screenPreset">Preset</label><div class="control"><select id="screenPreset" aria-describedby="screenPreset-help"><option value="">choose...</option><option value="rider">Rider</option><option value="kids">Kids</option></select><small id="screenPreset-help">fills in the lists below</small></div></div>
<h4>Tap screens</h4><div id="tapSlots" class="slots"></div><button type="button" id="addTap">Add screen</button><br>
<h4>Long press screens</h4><div id="holdSlots" class="slots"></div><button type="button" id="addHold">Add screen</button><br>
<div class="field"><label for="f34">Back to home after (s)</label><div class="control"><input id="f34" aria-describedby="f34-help" name="returnSeconds" type="number" min="0" max="3600"><small id="f34-help">0 = never</small></div></div>
<div class="field"><label for="f35">Cycle screens every (s)</label><div class="control"><input id="f35" aria-describedby="f35-help" name="cycleSeconds" type="number" min="0" max="3600"><small id="f35-help">0 = off; steps through the tap list, also without a touch sensor</small></div></div>
<button type="submit">Save screens</button>
</form></div><div class="card" id="displayCard">        <h3>Display</h3>
<form id="displayForm">
<div class="field"><label for="f8">Tomorrow from (hour)</label><div class="control"><input id="f8" aria-describedby="f8-help" name="previewHr" type="number" min="0" max="24" title="24 = never"><small id="f8-help">24 = never</small></div></div>
<div class="field check"><label for="f9"><input id="f9" aria-describedby="f9-help" name="dimAtNight" type="checkbox"><span>Dim at night</span></label><small id="f9-help">a touch gives full brightness for 30 s</small></div>
<div class="field"><label for="f10">Night brightness (%)</label><div class="control"><input id="f10" aria-describedby="f10-help" name="nightBrightness" type="number" min="1" max="100"><small id="f10-help">raise it if the screen looks blank at night</small></div></div>
<div class="field"><label for="f11">Sleep at night after (min)</label><div class="control"><input id="f11" aria-describedby="f11-help" name="sleepMinutes" type="number" min="0" max="600"><small id="f11-help">0 = never; a touch wakes it</small></div></div>
<div class="field check"><label for="f12"><input id="f12" aria-describedby="f12-help" name="alwaysSleep" type="checkbox"><span>Always sleep</span></label><small id="f12-help">screen off; a touch wakes it for 30 s</small></div>
<div class="field check"><label for="f13"><input id="f13" aria-describedby="f13-help" name="touchEnabled" type="checkbox"><span>Touch sensor</span></label><small id="f13-help">GPIO3; off if none is connected</small></div>
<div class="field"><label for="f14">Language</label><div class="control"><select id="f14" name="language"><option value="en">English</option><option value="nl">Nederlands</option></select></div></div>
<div class="field"><label for="f15">Screen off from (hour)</label><div class="control"><input id="f15" aria-describedby="f15-help" name="quietStart" type="number" min="-1" max="23"><small id="f15-help">-1 = off</small></div></div>
<div class="field"><label for="f16">Screen off until (hour)</label><div class="control"><input id="f16" name="quietEnd" type="number" min="-1" max="23"></div></div>
<button type="submit">Save display settings</button>
</form>
<p><small>Demo: about a minute and a half of every screen and animation (rain, snow, gusts, leaves, confetti, the
report blowing away) with made-up weather. A touch on the sensor stops it.</small></p>
<div class="field check"><label for="demoRepeat"><input id="demoRepeat" type="checkbox"><span>Repeat demo until stopped</span></label></div>
<button type="button" id="demoStart">Play demo</button> <button type="button" id="demoStop">Stop demo</button></div><div class="card" id="kidsCard" style="display:none">        <h3>Clothing</h3>
<p><small>The outfits of the picture screens, by temperature (&deg;C, always metric). Each limit must be equal to or below the one above it. The wind limit also blows the weather report away.</small></p>
<form id="kidsForm">
<div class="field"><label for="f17">Sun cap, t-shirt and shorts from</label><div class="control"><input id="f17" aria-describedby="f17-help" name="hot" type="number" step="1" min="-30" max="50"><small id="f17-help">sunny daytime only</small></div></div>
<div class="field"><label for="f19">T-shirt and shorts from</label><div class="control"><input id="f19" name="shorts" type="number" step="1" min="-30" max="50"></div></div>
<div class="field"><label for="f20">Sweater below</label><div class="control"><input id="f20" aria-describedby="f20-help" name="sweater" type="number" step="1" min="-30" max="50"><small id="f20-help">t-shirt above</small></div></div>
<div class="field"><label for="f21">Winter coat and scarf below</label><div class="control"><input id="f21" name="coat" type="number" step="1" min="-30" max="50"></div></div>
<div class="field"><label for="f22">Hat and mittens below</label><div class="control"><input id="f22" name="freeze" type="number" step="1" min="-30" max="50"></div></div>
<div class="field"><label for="f23">Wind picture above (km/h)</label><div class="control"><input id="f23" aria-describedby="f23-help" name="gust" type="number" step="1" min="1" max="150"><small id="f23-help">gusts, dry weather</small></div></div>
<div class="field"><label for="f24">Evening column</label><div class="control"><select id="f24" name="evening"><option value="dinner">Dinner (plate)</option><option value="sunset">Sunset (setting sun, 18-22)</option></select></div></div>
<div class="field"><label for="f25">Dinner time (hour)</label><div class="control"><input id="f25" aria-describedby="f25-help" name="dinner" type="number" step="1" min="15" max="21"><small id="f25-help">dinner only: its temperature, and sun, sunset or moon</small></div></div>
<div class="field check"><label for="f26"><input id="f26" aria-describedby="f26-help" name="nightColumn" type="checkbox"><span>Night column</span></label><small id="f26-help">after dinner: dinner, night, tomorrow morning</small></div>
<div class="field check"><label for="f18"><input id="f18" aria-describedby="f18-help" name="umbrella" type="checkbox"><span>Umbrella for showers</span></label><small id="f18-help">beside the outfit when it rains less than 70% of the time</small></div>
<button type="submit">Save clothing settings</button>
</form></div><div class="card" id="countdownCard" style="display:none">        <h3>Countdowns</h3>
<p><small>The countdown screen counts the sleeps to a birthday or holiday when it is near (put it in a list under Screens). The cake has a candle for every year and the letter on it.</small></p>
<form id="countdownForm">
<div class="field"><label for="f1">Birthday 1</label><div class="control"><input id="f1" aria-describedby="f1-help" name="birthday1" type="date"> <input name="initial1" class="letter" aria-label="Birthday 1 initial" maxlength="1" pattern="[A-Za-z]?" title="one letter"><small id="f1-help">date of birth, letter</small></div></div>
<div class="field"><label for="f2">Birthday 2</label><div class="control"><input id="f2" aria-describedby="f2-help" name="birthday2" type="date"> <input name="initial2" class="letter" aria-label="Birthday 2 initial" maxlength="1" pattern="[A-Za-z]?" title="one letter"><small id="f2-help">no date = none</small></div></div>
<div class="field check"><label for="f3"><input id="f3" name="halloween" type="checkbox"><span>Halloween (31 Oct)</span></label></div>
<div class="field check"><label for="f4"><input id="f4" aria-describedby="f4-help" name="intocht" type="checkbox"><span>Sinterklaas arrives</span></label><small id="f4-help">the intocht, a Saturday mid-November</small></div>
<div class="field"><label for="journaalInput">Sinterklaasjournaal</label><div class="control"><input id="journaalInput" aria-describedby="journaalInput-help" name="journaal" type="date"><small id="journaalInput-help">first broadcast; no date = none</small></div></div>
<div class="field check"><label for="f5"><input id="f5" name="sinterklaas" type="checkbox"><span>Sinterklaas (5 Dec)</span></label></div>
<div class="field check"><label for="f6"><input id="f6" name="christmas" type="checkbox"><span>Christmas (25 Dec)</span></label></div>
<div class="field"><label for="f7">Show from (sleeps before)</label><div class="control"><input id="f7" name="countdownDays" type="number" step="1" min="1" max="60"></div></div>
<button type="submit">Save countdowns</button>
</form></div><div class="card" id="thresholdsCard">        <h3>Ride Thresholds</h3>
<form id="thresholdsForm">
<div class="field"><label for="f38">Max Rain (mm)</label><div class="control"><input id="f38" name="maxRain" type="number" step="0.1" min="0" max="100"></div></div>
<div class="field"><label for="f39">Max Wind (km/h)</label><div class="control"><input id="f39" name="maxWind" type="number" step="1" min="0" max="200"></div></div>
<div class="field"><label for="f40">Min Temp (°C)</label><div class="control"><input id="f40" name="minTemp" type="number" step="1" min="-50" max="50"></div></div>
<div class="field"><label for="f41">Warn Wind (km/h)</label><div class="control"><input id="f41" name="warnWind" type="number" step="1" min="0" max="200"></div></div>
<div class="field"><label for="f42">Caution rain chance (%)</label><div class="control"><input id="f42" name="rainProb" type="number" step="5" min="0" max="101" title="101 = off"></div></div>
<button type="submit">Save ride limits</button>
</form></div>
</section>
<section id="connection" aria-labelledby="connectionHeading">
<h2 id="connectionHeading">Location &amp; WiFi</h2>
<div class="card" id="locationCard">        <h3>Location</h3>
<div class="stat-row"><span class="label">Place:</span> <span id="locationName"></span></div>
<div class="stat-row"><span class="label">Latitude:</span> <span id="lat"></span></div>
<div class="stat-row"><span class="label">Longitude:</span> <span id="lon"></span></div>
<div class="stat-row"><span class="label">Source:</span> <span id="locationSource"></span></div>
<a id="mapLink" target="_blank" rel="noopener">Show on the map</a>
<h4>Change location</h4>
<form id="placeSearch">
<label class="label" for="placeQuery">Search a place</label> <input id="placeQuery" type="text" maxlength="64" placeholder="town or village" required>
<button type="submit">Search</button>
</form>
<div id="placeResults"></div>
<form id="locationForm"><details id="manualLocation"><summary>Selected place / manual coordinates</summary>
<div class="field"><label for="f27">Name</label><div class="control"><input id="f27" name="place" type="text" maxlength="64"></div></div>
<div class="field"><label for="f28">Latitude</label><div class="control"><input id="f28" name="lat" type="number" step="any" min="-90" max="90" required></div></div>
<div class="field"><label for="f29">Longitude</label><div class="control"><input id="f29" name="lon" type="number" step="any" min="-180" max="180" required></div></div>
<small>A search fills these in. Without internet (setup mode) type the coordinates, e.g. from a map app.
They are rounded to 2 decimals (about 1 km).</small><br></details>
<button type="submit">Set Location</button>
</form></div><div class="card" id="wifiCard">        <h3>WiFi connection</h3>
<div class="stat-row"><span class="label">Connected:</span> <span id="wifiConnected"></span></div>
<div class="stat-row"><span class="label">Signal Strength:</span> <span id="wifiSignal"></span></div>
<div class="stat-row"><span class="label">Address:</span> <a id="hostLink" href="#"></a></div>
<button id="scanBtn">Scan Networks</button>
<div id="networksList"></div>

<form id="wifiForm">
<div class="field"><label for="f45">Network SSID</label><div class="control"><input id="f45" name="ssid" type="text" maxlength="32" required></div></div>
<div class="field"><label for="f46">Password</label><div class="control"><input id="f46" name="password" type="password" maxlength="63" autocomplete="new-password" placeholder="(unchanged)"></div></div>
<button type="submit">Save WiFi Settings</button>
</form>
<details><summary>Rename this device</summary>
<form id="hostnameForm">
<div class="field"><label for="hostnameInput">Device name</label><div class="control"><input id="hostnameInput" aria-describedby="hostnameInput-help" name="hostname" type="text" maxlength="24" pattern="[a-z0-9]([a-z0-9-]*[a-z0-9])?" required><small id="hostnameInput-help">.local; restarts</small></div></div>
<button type="submit">Rename</button>
</form></details></div><details class="card advanced" id="ssidCard"><summary>Per-network locations</summary>
<form id="ssidLocationForm">
<div class="field"><label for="ssidSelect">SSID</label><div class="control"><select name="ssid" id="ssidSelect" required>
<option value="">Select Network</option>
</select></div></div>
<div class="field"><label for="f36">Latitude</label><div class="control"><input id="f36" name="lat" type="number" step="0.0001" min="-90" max="90" required></div></div>
<div class="field"><label for="f37">Longitude</label><div class="control"><input id="f37" name="lon" type="number" step="0.0001" min="-180" max="180" required></div></div>
<button type="submit">Add Location</button>
</form>
<div id="ssidLocationsList"></div></details>
</section>
<section id="device" aria-labelledby="deviceHeading">
<h2 id="deviceHeading">Device</h2>
<div class="card" id="passwordCard">        <h3>Admin Password</h3>
<p>Protects this page, firmware updates and the WiFi setup network. The device restarts after a change.</p>
<form id="passwordForm">
<div class="field"><label for="f32">Current password</label><div class="control"><input id="f32" name="current" type="password" autocomplete="current-password" required></div></div>
<div class="field"><label for="f33">New password</label><div class="control"><input id="f33" name="newPassword" type="password" minlength="8" maxlength="63" autocomplete="new-password" required></div></div>
<div class="field"><label for="newPassword2">Repeat new password</label><div class="control"><input id="newPassword2" type="password" minlength="8" maxlength="63" autocomplete="new-password" required></div></div>
<button type="submit">Change Password</button>
</form></div><div class="card" id="updateCard">        <h3>Firmware Update</h3>
<div class="stat-row"><span class="label">Installed:</span> <span id="otaCurrent"></span></div>
<div class="stat-row"><span class="label">Latest release:</span> <span id="otaLatest"></span></div>
<div class="stat-row"><span class="label">Last check:</span> <span id="otaChecked"></span></div>
<div id="otaNotes"></div>
<div id="otaError" style="color:#b3261e"></div>
<button id="otaCheckBtn">Check now</button>
<button id="otaInstallBtn" style="display:none"></button>
<div id="otaFeedback"></div>
<details><summary>Update server settings</summary><form id="otaSettingsForm">
<div class="field"><label for="f30">Update server</label><div class="control"><input id="f30" name="url" type="url" maxlength="128" placeholder="http://dayspeck-ota.you.workers.dev"></div></div>
<div class="field check"><label for="f31"><input id="f31" name="autoCheck" type="checkbox"><span>Check daily</span></label></div>
<button type="submit">Save update settings</button>
</form></details>
<details><summary>Manual firmware upload</summary>
<p><small>A signed <code>dayspeck.bin.gz</code> from a release.</small></p>
<form id="otaForm">
<input type="file" name="update" aria-label="Signed firmware file" accept=".gz,.bin" required><br>
<button type="submit">Upload and Update</button>
</form>
<div id="otaUploadFeedback"></div>
</details></div><details class="card advanced" id="weatherApiCard"><summary>Advanced weather settings</summary>
<form id="weatherApiForm">
<div class="field"><label for="weatherModel">Forecast model</label><div class="control"><select id="weatherModel" name="weatherModel">
<option value="best_match">Automatic (Open-Meteo best match)</option>
<option value="knmi_seamless">Netherlands (KNMI + ECMWF)</option>
</select><small>KNMI for the first 2.5 days, then ECMWF for the rest of the week. Use Automatic outside the Netherlands and surrounding region.</small></div></div>
<div class="field"><label for="f43">API URL</label><div class="control"><input id="f43" name="apiUrl" type="url" maxlength="128" value="http://api.open-meteo.com/v1/forecast"></div></div>
<div class="field"><label for="f44">Units</label><div class="control"><select id="f44" name="units">
<option value="metric">Metric</option>
<option value="imperial">Imperial</option>
</select></div></div>
<div class="field check"><label for="weatherDebug"><input name="weatherDebug" type="checkbox" id="weatherDebug"><span>Verbose Debug</span></label></div>
<button type="submit">Save weather settings</button>
</form></details><details class="card advanced" id="debugCard"><summary>Diagnostics and logs</summary>
<div class="stat-row"><span class="label">Firmware:</span> <span id="firmware"></span></div>
<div class="stat-row"><span class="label">Weather Valid:</span> <span id="weatherValid"></span></div>
<div class="stat-row"><span class="label">Weather Age:</span> <span id="weatherAge"></span> minutes</div>
<div class="stat-row"><span class="label">mDNS Started:</span> <span id="mdnsStarted"></span></div>
<br>
<button id="toggleLogs">Show Verbose Logs</button>
<div id="verboseLogs"></div></details>
</section>
</main>

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

function formFeedback(form, message, state = '') {
  let feedback = form.querySelector('.form-feedback');
  if (!feedback) {
    feedback = document.createElement('p');
    feedback.className = 'form-feedback';
    feedback.setAttribute('role', 'status');
    form.appendChild(feedback);
  }
  feedback.textContent = message;
  feedback.className = 'form-feedback ' + state;
}

function markDirty(form) {
  form.dataset.dirty = '1';
  form.dataset.revision = String(Number(form.dataset.revision || 0) + 1);
  formFeedback(form, 'Unsaved changes');
}
document.querySelectorAll('form').forEach(form => {
  if (['placeSearch', 'otaForm'].includes(form.id)) return;
  form.addEventListener('input', () => markDirty(form));
  form.addEventListener('change', () => markDirty(form));
});
// Reveal required manual fields before the browser focuses an invalid input.
document.addEventListener('invalid', e => {
  for (let parent = e.target.parentElement; parent; parent = parent.parentElement) {
    if (parent.tagName === 'DETAILS') parent.open = true;
  }
}, true);

function saveForm(form, path, body, okMessage, after) {
  if (form.dataset.saving) return;
  const revision = form.dataset.revision;
  form.dataset.saving = '1';
  const submit = form.querySelector('button[type=submit]');
  submit.disabled = true;
  formFeedback(form, 'Saving...');
  return post(path, body).then(res => {
    let message = res.message || (res.ok ? okMessage : 'Error ' + res.status);
    if (res.ok) {
      if (form.dataset.revision === revision) delete form.dataset.dirty;
      else message += ' You have newer unsaved changes.';
    }
    formFeedback(form, message, res.ok ? 'ok' : 'err');
    if (res.ok && after) return after();
  }).catch(() => formFeedback(form, 'Request failed. Your edits are kept; try saving again.', 'err'))
   .finally(() => { delete form.dataset.saving; submit.disabled = false; });
}

function submitForm(id, path, okMessage, after) {
  const form = document.getElementById(id);
  form.addEventListener('submit', e => {
    e.preventDefault();
    saveForm(form, path, new URLSearchParams(new FormData(form)), okMessage, after);
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
          document.getElementById('manualLocation').open = true;
          markDirty(f);
          placeResults.textContent = 'Selected ' + label + '. Press Set Location to use it.';
        });
        placeResults.appendChild(b);
      });
    })
    .catch(() => {
      placeResults.textContent = 'The search needs an internet connection. Enter the coordinates below instead.';
      document.getElementById('manualLocation').open = true;
    });
});

function fillForm(id, values) {
  const form = document.getElementById(id);
  if (form.dataset.dirty) return;
  Object.entries(values).forEach(([key, value]) => {
    const input = form.elements.namedItem(key);
    if (input.type === 'checkbox') input.checked = !!value;
    else input.value = value == null ? '' : value;
  });
}

function updateScreenCards() {
  const used = Array.from(document.querySelectorAll('.slots select')).map(s => s.value);
  document.getElementById('kidsCard').style.display =
    ['weather', 'clothes', 'village', 'report'].some(n => used.includes(n)) ? '' : 'none';
  document.getElementById('countdownCard').style.display = used.includes('countdown') ? '' : 'none';
}

function loadStatus(full) {
  return fetch('/api/status').then(r => { if (!r.ok) throw new Error(r.status); return r.json(); }).then(s => {
    const text = (id, value) => { document.getElementById(id).textContent = value; };
    text('lat', String(+s.lat.toFixed(4))); text('lon', String(+s.lon.toFixed(4)));
    text('locationSource', s.locationSource); text('locationName', s.locationName || '(no name)');
    document.getElementById('mapLink').href = 'https://www.openstreetmap.org/?mlat=' + s.lat +
      '&mlon=' + s.lon + '#map=12/' + s.lat + '/' + s.lon;
    text('wifiConnected', s.wifi.connected ? 'Yes' : 'No'); text('wifiSignal', s.wifi.signalStrength + ' dBm');
    ['hostLink', 'overviewHost'].forEach(id => {
      const host = document.getElementById(id);
      host.textContent = 'http://' + s.wifi.hostname + '.local'; host.href = host.textContent;
    });
    text('overviewWifi', s.wifi.connected ? 'Connected to ' + s.wifi.ssid : 'Not connected');
    text('overviewWeather', s.debug.weatherValid ? 'Updated ' + s.debug.weatherAge + ' min ago' : 'Waiting for forecast');
    text('overviewLocation', s.locationName || s.lat.toFixed(2) + ', ' + s.lon.toFixed(2));
    text('overviewFirmware', s.firmware); text('firmware', s.firmware);
    text('weatherValid', s.debug.weatherValid ? 'Yes' : 'No'); text('weatherAge', s.debug.weatherAge);
    text('mdnsStarted', s.debug.mdnsStarted ? 'Yes' : 'No');
    renderSsidLocations(s.ssidLocations || []);
    if (!full) return;
    const d = s.display, k = s.kids;
    fillForm('thresholdsForm', {maxRain: s.thresholds.maxRainMm, maxWind: s.thresholds.maxWindKmh,
      minTemp: s.thresholds.minTempC, warnWind: s.thresholds.warnWindKmh, rainProb: s.thresholds.rainProbPct});
    fillForm('displayForm', {previewHr: d.previewHr, dimAtNight: d.dimAtNight, nightBrightness: d.nightBrightness,
      sleepMinutes: d.sleepMinutes, alwaysSleep: d.alwaysSleep, touchEnabled: d.touchEnabled,
      language: d.language, quietStart: d.quietStart, quietEnd: d.quietEnd});
    if (!document.forms.screensForm.dataset.dirty) {
      setSlots('tapSlots', d.screens.tap); setSlots('holdSlots', d.screens.hold);
      fillForm('screensForm', {returnSeconds: d.screens.returnSeconds, cycleSeconds: d.cycleSeconds});
    }
    updateScreenCards();
    fillForm('kidsForm', {hot: k.hotFromC, shorts: k.shortsFromC, sweater: k.sweaterBelowC, coat: k.coatBelowC,
      freeze: k.freezeBelowC, gust: k.windyGustKmh, evening: k.evening, dinner: k.dinnerHour,
      nightColumn: k.nightColumn, umbrella: k.umbrella});
    const birthdays = k.birthdays || [];
    fillForm('countdownForm', {birthday1: (birthdays[0] || {}).date, initial1: (birthdays[0] || {}).initial,
      birthday2: (birthdays[1] || {}).date, initial2: (birthdays[1] || {}).initial,
      halloween: k.halloween, sinterklaas: k.sinterklaas, intocht: k.intocht,
      journaal: k.journaal, christmas: k.christmas, countdownDays: k.countdownDays});
    fillForm('weatherApiForm', {apiUrl: s.weatherApi.url, weatherModel: s.weatherApi.model, units: s.weatherApi.units, weatherDebug: s.weatherApi.debug});
    fillForm('hostnameForm', {hostname: s.wifi.hostname}); fillForm('wifiForm', {ssid: s.wifi.ssid});
    document.forms.wifiForm.password.placeholder = s.wifi.passwordSet ? '(unchanged)' : '(none)';
  }).catch(() => toast('Could not load the device status', false));
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
  ['hours', 'Next hours'], ['rideHours', 'Ride hours'], ['clock', 'Clock'], ['village', 'Village with outfit'],
  ['weather', 'Weather in pictures'], ['clothes', 'Clothes'], ['countdown', 'Countdown'], ['report', 'Weather report'],
  ['cat', 'Lucky cat']
];
const PRESETS = {
  rider: { tap: ['ride', 'rideOther'], hold: ['week', 'hours', 'clock'] },
  kids: { tap: ['village', 'weather', 'clothes', 'countdown'], hold: ['report'] }
};
const MAX_SLOTS = 6;
function refreshSlots(boxId, edited = false) {
  const rows = Array.from(document.getElementById(boxId).children);
  rows.forEach((row, i) => {
    row.querySelector('.slot-label').textContent = String(i + 1) + (boxId === 'tapSlots' && i === 0 ? ' Home' : '');
    row.querySelector('select').setAttribute('aria-label', (boxId === 'tapSlots' ? 'Tap' : 'Long press') + ' screen ' + (i + 1));
    const buttons = row.querySelectorAll('button');
    buttons[0].disabled = i === 0; buttons[1].disabled = i === rows.length - 1;
  });
  document.getElementById(boxId === 'tapSlots' ? 'addTap' : 'addHold').disabled = rows.length >= MAX_SLOTS;
  updateScreenCards();
  if (edited) markDirty(document.forms.screensForm);
}
function addSlot(boxId, value, edited = false) {
  const box = document.getElementById(boxId);
  if (box.children.length >= MAX_SLOTS) return;
  const row = document.createElement('div'); row.className = 'slot-row';
  const label = document.createElement('span'); label.className = 'slot-label';
  const sel = document.createElement('select');
  SCREENS.forEach(([v, t]) => sel.appendChild(option(v, t)));
  sel.value = value || SCREENS[0][0];
  sel.addEventListener('change', updateScreenCards);
  const button = (text, title, fn) => {
    const b = document.createElement('button');
    b.type = 'button'; b.textContent = text; b.title = title; b.setAttribute('aria-label', title);
    b.addEventListener('click', () => { fn(); refreshSlots(boxId, true); });
    return b;
  };
  const actions = document.createElement('div'); actions.className = 'slot-actions';
  actions.append(button('\u2191', 'Move up', () => { if (row.previousElementSibling) box.insertBefore(row, row.previousElementSibling); }),
    button('\u2193', 'Move down', () => { if (row.nextElementSibling) box.insertBefore(row.nextElementSibling, row); }),
    button('\u2715', 'Remove', () => row.remove()));
  row.append(label, sel, actions); box.appendChild(row); refreshSlots(boxId, edited);
}
function setSlots(boxId, names) {
  document.getElementById(boxId).replaceChildren();
  (names || []).forEach(n => addSlot(boxId, n));
  refreshSlots(boxId);
}
const slotNames = boxId => Array.from(document.querySelectorAll('#' + boxId + ' select')).map(s => s.value);
document.getElementById('addTap').addEventListener('click', () => addSlot('tapSlots', null, true));
document.getElementById('addHold').addEventListener('click', () => addSlot('holdSlots', null, true));
document.getElementById('screenPreset').addEventListener('change', function() {
  const p = PRESETS[this.value];
  if (p) { setSlots('tapSlots', p.tap); setSlots('holdSlots', p.hold); }
  this.value = '';
});
document.getElementById('screensForm').addEventListener('submit', e => {
  e.preventDefault();
  const f = document.forms.screensForm;
  saveForm(f, '/api/screens', new URLSearchParams({
    tap: slotNames('tapSlots').join(','), hold: slotNames('holdSlots').join(','),
    returnSeconds: f.returnSeconds.value, cycleSeconds: f.cycleSeconds.value
  }), 'Screens saved', () => loadStatus(true));
});

submitForm('displayForm', '/api/display', 'Display settings saved');
const demo = action => post('/api/demo', new URLSearchParams({
    action: action, repeat: document.getElementById('demoRepeat').checked ? '1' : '0' }))
  .then(res => toast(res.message || (res.ok ? 'OK' : 'Error ' + res.status), res.ok))
  .catch(() => toast('Request failed', false));
document.getElementById('demoStart').addEventListener('click', () => demo('start'));
document.getElementById('demoStop').addEventListener('click', () => demo('stop'));
submitForm('kidsForm', '/api/kids', 'Clothing settings saved');
submitForm('countdownForm', '/api/countdown', 'Countdowns saved');
submitForm('weatherApiForm', '/api/weatherconfig', 'Weather API configuration saved', () => loadStatus(true));
submitForm('wifiForm', '/api/wifi/config', 'WiFi settings saved');
submitForm('hostnameForm', '/api/hostname', 'Renamed');
submitForm('locationForm', '/api/location', 'Location set', () => loadStatus(false));
submitForm('ssidLocationForm', '/api/ssidlocation', 'Location added', () => {
  if (!document.forms.ssidLocationForm.dataset.dirty) document.forms.ssidLocationForm.reset();
  loadStatus(false);
});

// Password change: confirm client-side, then the device restarts
document.getElementById('passwordForm').addEventListener('submit', e => {
  e.preventDefault();
  const form = e.target;
  if (form.newPassword.value !== document.getElementById('newPassword2').value) {
    formFeedback(form, 'The new passwords do not match', 'err');
    return;
  }
  saveForm(form, '/api/password', new URLSearchParams(new FormData(form)), 'Password changed', () => {
    if (!form.dataset.dirty) form.reset();
  });
});

document.getElementById('scanBtn').addEventListener('click', () => {
  fetch('/api/wifi/scan')
    .then(r => r.json())
    .then(s => {
      const list = document.getElementById('networksList');
      list.replaceChildren();
      const title = document.createElement('b');
      title.textContent = 'Available Networks:';
      list.appendChild(title);

      const ssidSelect = document.getElementById('ssidSelect');
      ssidSelect.replaceChildren(option('', 'Select Network'));

      s.networks.forEach(net => {
        const label = net.ssid + ' (' + net.signalStrength + ' dBm)';
        const b = document.createElement('button');
        b.textContent = label;
        b.addEventListener('click', () => { document.forms.wifiForm.ssid.value = net.ssid; markDirty(document.forms.wifiForm); });
        list.appendChild(b);
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
    if (authLockUntilMs != 0) {
        if ((long)(now - authLockUntilMs) < 0) {
            sendMessage(429, "Too many failed attempts, try again in a minute");
            return false;
        }
        authLockUntilMs = 0;   // expired: forget it, or ~25 days on it would look like a future deadline again
    }
    if (server.authenticate(ADMIN_USER, effectivePassword().c_str())) {
        authFailures = 0;
        authLockUntilMs = 0;
        return true;
    }
    if (++authFailures >= AUTH_MAX_FAILURES) {
        authFailures = 0;
        authLockUntilMs = (now + AUTH_LOCKOUT_MS) | 1;   // | 1: never 0, which means unlocked
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
    kids["evening"] = kidsSunsetColumn ? "sunset" : "dinner";
    kids["dinnerHour"] = kidsDinnerHour;
    kids["nightColumn"] = kidsNightColumn;
    kids["umbrella"] = kidsUmbrella;
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
    kids["intocht"] = (kidsHolidays & KIDS_HOLIDAY(KIDS_EVENT_INTOCHT)) != 0;
    kids["christmas"] = (kidsHolidays & KIDS_HOLIDAY(KIDS_EVENT_CHRISTMAS)) != 0;
    kids["countdownDays"] = kidsCountdownDays;
    if (kidsJournaal.month != 0) {
        char date[11];
        snprintf(date, sizeof(date), "%04d-%02d-%02d", kidsJournaal.year, kidsJournaal.month, kidsJournaal.day);
        kids["journaal"] = date;
    } else {
        kids["journaal"] = "";
    }

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
    doc["wifi"]["hostname"] = deviceName;

    doc["weatherApi"]["url"] = weatherApiUrl;
    doc["weatherApi"]["model"] = weatherModel;
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

static bool argInt(const char* name, int lo, int hi, int& out);

static void handleApiKids() {
    float hot, shorts, sweater, coat, freeze, gust;
    int dinner;
    if (!argFloat("hot", -30, 50, hot) || !argFloat("shorts", -30, 50, shorts) ||
        !argFloat("sweater", -30, 50, sweater) || !argFloat("coat", -30, 50, coat) ||
        !argFloat("freeze", -30, 50, freeze) || !argFloat("gust", 1, 150, gust) ||
        !argInt("dinner", KIDS_DINNER_MIN_HR, KIDS_DINNER_MAX_HR, dinner)) {
        sendMessage(400, "Invalid value: temperatures -30 to 50 C, wind 1-150 km/h, dinner time 15-21");
        return;
    }
    bool nightColumn = server.hasArg("nightColumn");
    bool umbrella = server.hasArg("umbrella");
    String evening = server.arg("evening");
    if (evening != "dinner" && evening != "sunset") {
        sendMessage(400, "The evening column must be dinner or sunset");
        return;
    }
    KidsLimits k = { hot, shorts, sweater, coat, freeze, gust };
    if (!kidsLimitsValid(k)) {
        sendMessage(400, "The limits must go from warm to cold: sun cap from >= shorts from >= sweater below >= winter coat below >= hat below");
        return;
    }

    bool saved = updateConfig([&](JsonDocument& doc) {
        doc["kids"]["hotFromC"] = hot;
        doc["kids"]["shortsFromC"] = shorts;
        doc["kids"]["sweaterBelowC"] = sweater;
        doc["kids"]["coatBelowC"] = coat;
        doc["kids"]["freezeBelowC"] = freeze;
        doc["kids"]["windyGustKmh"] = gust;
        doc["kids"]["evening"] = evening;
        doc["kids"]["dinnerHour"] = dinner;
        doc["kids"]["nightColumn"] = nightColumn;
        doc["kids"]["umbrella"] = umbrella;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    kidsLimits = k;
    kidsDinnerHour = dinner;
    kidsSunsetColumn = evening == "sunset";
    kidsNightColumn = nightColumn;
    kidsUmbrella = umbrella;
    state.displayDirty = true;
    sendMessage(200, "Clothing settings saved");
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
    KidsDate journaal = {};
    String journaalDate = server.arg("journaal");
    journaalDate.trim();
    if (journaalDate.length() != 0 && !parseIsoDate(journaalDate.c_str(), journaal.year, journaal.month, journaal.day)) {
        sendMessage(400, "Invalid Sinterklaasjournaal date (YYYY-MM-DD)");
        return;
    }
    unsigned holidays = 0;
    if (server.hasArg("halloween")) holidays |= KIDS_HOLIDAY(KIDS_EVENT_HALLOWEEN);
    if (server.hasArg("sinterklaas")) holidays |= KIDS_HOLIDAY(KIDS_EVENT_SINTERKLAAS);
    if (server.hasArg("intocht")) holidays |= KIDS_HOLIDAY(KIDS_EVENT_INTOCHT);
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
        doc["kids"]["intocht"] = (holidays & KIDS_HOLIDAY(KIDS_EVENT_INTOCHT)) != 0;
        doc["kids"]["christmas"] = (holidays & KIDS_HOLIDAY(KIDS_EVENT_CHRISTMAS)) != 0;
        doc["kids"]["countdownDays"] = days;
        doc["kids"]["journaal"] = journaalDate;   // "" = none
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    memcpy(kidsBirthdays, b, sizeof(kidsBirthdays));
    kidsHolidays = holidays;
    kidsCountdownDays = days;
    kidsJournaal = journaal;
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
    if (state.apModeStarted) {
        unsigned long now = millis();
        state.staRetryStartMs = now ? now : now - 1;   // give this attempt a full window, then go idle again
    }

    sendMessage(200, ("WiFi settings saved, connecting... Then open http://" + deviceName + ".local").c_str());
}

static void handleApiWeatherConfig() {
    String apiUrl = server.arg("apiUrl");
    String model = server.hasArg("weatherModel") ? server.arg("weatherModel") : weatherModel;
    if (model != "best_match" && model != "knmi_seamless") {
        sendMessage(400, "Choose Automatic or Netherlands for the forecast model");
        return;
    }
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
        doc["weatherModel"] = model;
        doc["weatherUnits"] = units;
        doc["weatherDebug"] = debug;
    });
    if (!saved) {
        sendMessage(500, "Could not save configuration");
        return;
    }

    weatherApiUrl = apiUrl;
    weatherModel = model;
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

bool saveHostname(const String& name) {
    return updateConfig([&](JsonDocument& doc) { doc["hostname"] = name; });
}

// Rename the device on the network; it restarts to announce the new name
static void handleApiHostname() {
    String name = server.arg("hostname");
    name.trim();
    name.toLowerCase();
    if (name.endsWith(".local")) name.remove(name.length() - 6);
    if (!hostnameValid(name.c_str())) {
        sendMessage(400, "A device name is 1-24 letters, digits or hyphens (not at the start or end)");
        return;
    }
    if (!saveHostname(name)) {
        sendMessage(500, "Could not save configuration");
        return;
    }
    deviceName = name;
    rebootAtMs = (millis() + 1500) | 1;
    sendMessage(200, ("Renamed. The device restarts now; then open http://" + name + ".local").c_str());
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
    server.on("/api/hostname", guardedPost(handleApiHostname));
    server.on("/api/ota/check", guardedPost(handleApiOtaCheck));
    server.on("/api/ota/install", guardedPost(handleApiOtaInstall));
    server.on("/api/ota/settings", guardedPost(handleApiOtaSettings));

    server.onNotFound([]() { sendMessage(404, "Not found"); });
    server.begin();
}

void handleWebServer() {
    server.handleClient();
    if (rebootAtMs != 0 && (long)(millis() - rebootAtMs) >= 0) {
        WiFi.disconnect();   // leave the network cleanly, so it lets us back in at once
        ESP.restart();
    }
}
