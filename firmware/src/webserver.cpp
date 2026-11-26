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

// The page, firmware/web/index.html, gzipped at build time by tools/pio_webui.py (index_html_gz[])
#include "index_html_gz.h"

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
    // Stored gzipped (a quarter of the size): every browser unpacks it
    server.sendHeader(F("Content-Encoding"), F("gzip"));
    server.send_P(200, "text/html; charset=utf-8", (PGM_P)index_html_gz, sizeof(index_html_gz));
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
