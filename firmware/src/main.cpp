// Dayspeck Bedside Display — Main Entry Point
// PlatformIO project for ESP-01 (ESP8266) + SSD1306 OLED
// Rewritten: Event driven architecture, non-blocking, reliable timing

#include <Arduino.h>
#include <time.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPUpdateServer.h>

#include "config.h"
#include "app_state.h"
#include "motologic.h"   // direct include so PlatformIO links the library (headers in firmware/include are not scanned)
#include "bitmaps.h"
#include "display.h"
#include "weather.h"
#include "touch.h"
#include "webserver.h"
#include "security.h"
#include "ota.h"

// Display object
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT);

// HTTP Update Server
ESP8266HTTPUpdateServer httpUpdater;

// System state - single source of truth (shared via app_state.h)
SystemState state = {};

std::vector<SsidLocation> ssidLocations;

// Configuration from JSON
float configLat = 51.5074;  // Default: London
float configLon = -0.1278;

// Location source flags
bool manualConfigPresent = false;   // config.json defines a location
bool manualLocation = false;        // user picked a location in the web UI
bool ssidBasedLocation = false;

// Display options (loaded from config.json)
int  previewHr = DEFAULT_PREVIEW_HR;
bool displayDimAtNight = true;
int  displayNightBrightness = DEFAULT_NIGHT_BRIGHTNESS_PCT;
int  displaySleepMinutes = 0;
int  quietStartHr = -1;
int  quietEndHr = -1;
bool displayAlwaysSleep = false;
bool displayTouchEnabled = true;
int  displayCycleSeconds = 0;
// Screens: the rider set by default (the web UI has a kids preset)
ScreenList screensTap  = { { SCREEN_RIDE, SCREEN_RIDE_OTHER }, 2 };
ScreenList screensHold = { { SCREEN_WEEK, SCREEN_HOURS, SCREEN_CLOCK }, 3 };
int  screensReturnSeconds = DEFAULT_SCREENS_RETURN_SEC;
String displayLanguage = "en";
String locationName;

// WiFi configuration (loaded from config.json)
String wifiSsid = WIFI_SSID;  // Default to compile-time values
String wifiPassword = WIFI_PASS;

// Weather API configuration (loaded from config.json)
String weatherApiUrl = DEFAULT_WEATHER_API_URL;
String weatherUnits = DEFAULT_WEATHER_UNITS;
bool weatherDebug = DEFAULT_WEATHER_DEBUG;

#define WIFI_AP_DELAY_FIRST_MS    30000UL    // never connected: start the setup AP after 30 s
#define WIFI_AP_DELAY_OUTAGE_MS   300000UL   // lost a working connection: AP only after 5 min
#define AP_SSID                   "Dayspeck"
#define MIN_VALID_EPOCH           1600000000L // anything earlier means NTP has not synced


/**
 * Rollover-safe timing check (unsigned subtraction handles millis() wrap at 49 days)
 */
bool intervalPassed(unsigned long lastRun, unsigned long interval) {
    return intervalElapsed(millis(), lastRun, interval);
}


/**
 * Read a [startHour, hours] ride window from config, ignoring invalid values
 */
static void loadWindow(JsonVariant v, RideWindow &w) {
    if (!v.is<JsonArray>() || v.size() < 2) return;
    int start = v[0] | -1;
    int hours = v[1] | 0;
    if (start >= 0 && start < 24 && hours >= 1 && hours <= 12) {
        w.startHour = (uint8_t)start;
        w.hours = (uint8_t)hours;
    }
}


/**
 * Load config from LittleFS. Missing or invalid files leave the defaults in place.
 */
void loadConfig() {
    // Recover from an interrupted atomic save (see webserver.cpp: updateConfig)
    if (!LittleFS.exists("/config.json") && LittleFS.exists("/config.tmp")) {
        LittleFS.rename("/config.tmp", "/config.json");
    }

    File file = LittleFS.open("/config.json", "r");
    if (!file) {
        logMessage("config.json not found, using defaults");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        char buf[64];
        snprintf(buf, sizeof(buf), "config.json invalid (%s), using defaults", error.c_str());
        logMessage(buf);
        return;
    }

    // WiFi credentials
    if (doc["wifi"]["ssid"].is<String>() && doc["wifi"]["ssid"].as<String>().length() > 0) {
        wifiSsid = doc["wifi"]["ssid"].as<String>();
        wifiPassword = doc["wifi"]["password"].as<String>();
    }

    // Admin / setup AP password (empty or invalid = device default)
    if (doc["auth"]["password"].is<String>()) {
        String pw = doc["auth"]["password"].as<String>();
        if (pw.length() >= PASSWORD_MIN_LEN && pw.length() <= PASSWORD_MAX_LEN) {
            adminPassword = pw;
        }
    }

    // Location
    if (doc["lat"].is<float>() && doc["lon"].is<float>()) {
        configLat = doc["lat"];
        configLon = doc["lon"];
        manualConfigPresent = true;
    }
    if (doc["manualLocation"].is<bool>()) manualLocation = doc["manualLocation"];
    if (doc["locationName"].is<String>()) locationName = doc["locationName"].as<String>();

    // Config schema version (see CONFIG_VERSION in config.h)
    int version = doc["version"] | 0;
    if (version > CONFIG_VERSION) {
        logMessage("config.json is from a newer firmware, some settings may be ignored");
    }

    // Thresholds
    if (doc["thresholds"].is<JsonObject>()) {
        if (doc["thresholds"]["maxRainMm"].is<float>()) maxRainMm = doc["thresholds"]["maxRainMm"];
        if (doc["thresholds"]["maxWindKmh"].is<float>()) maxWindKmh = doc["thresholds"]["maxWindKmh"];
        if (doc["thresholds"]["minTempC"].is<float>()) minTempC = doc["thresholds"]["minTempC"];
        if (doc["thresholds"]["warnWindKmh"].is<float>()) warnWindKmh = doc["thresholds"]["warnWindKmh"];
        if (doc["thresholds"]["rainProbPct"].is<float>()) rainProbPct = doc["thresholds"]["rainProbPct"];
    }

    // Kids outfit limits: all or nothing, and only when they go from warm to cold
    if (doc["kids"].is<JsonObject>()) {
        JsonObject o = doc["kids"];
        KidsLimits k = kidsLimits;
        if (o["hotFromC"].is<float>()) k.hotFromC = o["hotFromC"];
        if (o["shortsFromC"].is<float>()) k.shortsFromC = o["shortsFromC"];
        if (o["sweaterBelowC"].is<float>()) k.sweaterBelowC = o["sweaterBelowC"];
        if (o["coatBelowC"].is<float>()) k.coatBelowC = o["coatBelowC"];
        if (o["freezeBelowC"].is<float>()) k.freezeBelowC = o["freezeBelowC"];
        if (o["windyGustKmh"].is<float>()) k.windyGustKmh = o["windyGustKmh"];
        if (kidsLimitsValid(k)) kidsLimits = k;
        else logMessage("config.json: the kids limits are not ordered from warm to cold, using the defaults");

        // Countdowns: [{"date": "YYYY-MM-DD", "initial": "A"}], the holidays and the range in sleeps
        if (o["birthdays"].is<JsonArray>()) {
            size_t i = 0;
            for (JsonObject b : o["birthdays"].as<JsonArray>()) {
                if (i >= KIDS_MAX_BIRTHDAYS) break;
                KidsBirthday kb = {};
                if (parseIsoDate(b["date"] | "", kb.year, kb.month, kb.day)) {
                    kb.initial = kidsInitial(b["initial"] | "");
                    kidsBirthdays[i++] = kb;
                }
            }
        }
        const char* const HOLIDAY_KEYS[] = { "halloween", "sinterklaas", "christmas" };
        const int HOLIDAY_KINDS[] = { KIDS_EVENT_HALLOWEEN, KIDS_EVENT_SINTERKLAAS, KIDS_EVENT_CHRISTMAS };
        for (int i = 0; i < 3; i++) {
            if (!o[HOLIDAY_KEYS[i]].is<bool>()) continue;
            if (o[HOLIDAY_KEYS[i]]) kidsHolidays |= KIDS_HOLIDAY(HOLIDAY_KINDS[i]);
            else kidsHolidays &= ~KIDS_HOLIDAY(HOLIDAY_KINDS[i]);
        }
        if (o["countdownDays"].is<int>()) kidsCountdownDays = constrain((int)o["countdownDays"], 1, KIDS_MAX_COUNTDOWN_DAYS);
    }

    // Ride windows: [start hour, length in hours]
    loadWindow(doc["wd_am"], weekdayAM);
    loadWindow(doc["wd_pm"], weekdayPM);
    loadWindow(doc["we_am"], weekendAM);
    loadWindow(doc["we_pm"], weekendPM);

    // Display options
    if (doc["previewHr"].is<int>()) previewHr = constrain((int)doc["previewHr"], 0, 24);
    if (doc["display"]["dimAtNight"].is<bool>()) displayDimAtNight = doc["display"]["dimAtNight"];
    if (doc["display"]["nightBrightness"].is<int>()) displayNightBrightness = constrain((int)doc["display"]["nightBrightness"], 1, 100);
    if (doc["display"]["sleepMinutes"].is<int>()) displaySleepMinutes = constrain((int)doc["display"]["sleepMinutes"], 0, 600);
    if (doc["display"]["quietStart"].is<int>()) quietStartHr = constrain((int)doc["display"]["quietStart"], -1, 23);
    if (doc["display"]["quietEnd"].is<int>()) quietEndHr = constrain((int)doc["display"]["quietEnd"], -1, 23);

    if (doc["display"]["alwaysSleep"].is<bool>()) displayAlwaysSleep = doc["display"]["alwaysSleep"];
    if (doc["display"]["touchEnabled"].is<bool>()) displayTouchEnabled = doc["display"]["touchEnabled"];
    if (doc["display"]["cycleSeconds"].is<int>()) {
        int secs = (int)doc["display"]["cycleSeconds"];
        displayCycleSeconds = (secs >= 2) ? constrain(secs, 2, 3600) : 0;   // 0 = off; 1 s would be a flicker
    }
    // Screens: both lists are replaced together, and only when they make sense
    JsonObject screens = doc["display"]["screens"];
    if (!screens.isNull()) {
        auto readList = [](JsonVariant v, ScreenList &out) {
            out.count = 0;
            if (v.isNull()) return true;                  // no hold list: a long press acts as a tap
            if (!v.is<JsonArray>()) return false;
            for (JsonVariant e : v.as<JsonArray>()) {
                int id = screenFromName(e | "");
                if (id < 0 || out.count >= MAX_SCREEN_SLOTS) return false;
                out.ids[out.count++] = (uint8_t)id;
            }
            return true;
        };
        ScreenList tap = {}, hold = {};
        if (readList(screens["tap"], tap) && readList(screens["hold"], hold) && screenListsValid(tap, hold)) {
            screensTap = tap;
            screensHold = hold;
        } else {
            logMessage("config.json: display.screens is not valid, using the default screens");
        }
        if (screens["returnSeconds"].is<int>()) screensReturnSeconds = constrain((int)screens["returnSeconds"], 0, 3600);
    } else if (doc["kids"].is<JsonObject>()) {
        // A config from the former kids firmware (only it wrote "kids"): keep showing the kids screens
        screensTap = ScreenList{ { SCREEN_WEATHER, SCREEN_CLOTHES, SCREEN_COUNTDOWN }, 3 };
        screensHold = ScreenList{ { SCREEN_REPORT }, 1 };
    }
    if (!displayTouchEnabled && displayAlwaysSleep) {
        displayAlwaysSleep = false;   // nothing could wake the screen again
        logMessage("config.json: display.alwaysSleep needs the touch sensor, ignored");
    }
    if (doc["display"]["language"].is<String>()) {
        String lang = doc["display"]["language"].as<String>();
        if (lang == "en" || lang == "nl") displayLanguage = lang;
    }

    // Weather API config
    if (doc["weatherApiUrl"].is<String>()) weatherApiUrl = doc["weatherApiUrl"].as<String>();
    if (doc["weatherUnits"].is<String>()) weatherUnits = doc["weatherUnits"].as<String>();
    if (doc["weatherDebug"].is<bool>()) weatherDebug = doc["weatherDebug"].as<bool>();

    // Pull updates
    if (doc["ota"]["url"].is<String>()) otaServerUrl = doc["ota"]["url"].as<String>();
    if (doc["ota"]["autoCheck"].is<bool>()) otaAutoCheck = doc["ota"]["autoCheck"];

    // SSID locations
    if (doc["ssidLocations"].is<JsonArray>()) {
        ssidLocations.clear();
        for (JsonObject item : doc["ssidLocations"].as<JsonArray>()) {
            SsidLocation loc;
            loc.ssid = item["ssid"].as<String>();
            loc.lat = item["lat"];
            loc.lon = item["lon"];
            ssidLocations.push_back(loc);
        }
    }
}


/**
 * Initialize display with graceful failure
 */
bool initDisplay() {
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        display.setRotation(2);  // Flip display 180 degrees since screen is mounted upside down
        display.clearDisplay();
        display.display();
        return true;
    }
    return false;
}


/**
 * Update day/night state from NTP time and the forecast's sunrise/sunset.
 * Without synced time it assumes day rather than guessing from a bogus clock.
 */
void updateDayNight() {
    static unsigned long lastCheckMs = 0;
    if (!intervalPassed(lastCheckMs, 1000)) return;
    lastCheckMs = millis();

    time_t now = time(nullptr);
    state.timeSynced = (now > MIN_VALID_EPOCH);

    bool night = false;
    if (state.timeSynced) {
        if (sunriseTime > 0 && sunsetTime > 0) {
            night = isNightAt((long)now, (long)sunriseTime, (long)sunsetTime);
        } else {
            int hour = localHour();
            night = (hour >= 21 || hour < 6);
        }
    }

    if (night != state.isNight) {
        state.isNight = night;
        state.displayDirty = true;
    }

    // Dimmer at night (this is a bedside display), but full brightness for 30 s after a touch.
    // Not the library's dim(): that sets contrast 0, which is completely dark on some panels.
    static int applied = -1;
    bool touched = !intervalPassed(state.lastActivityMs, 30000UL);
    int contrast = (state.isNight && displayDimAtNight && !touched)
                   ? contrastForPercent(displayNightBrightness) : DAY_CONTRAST;
    if (contrast != applied) {
        display.ssd1306_command(SSD1306_SETCONTRAST);
        display.ssd1306_command((uint8_t)contrast);
        applied = contrast;
    }

    // After previewHr the default view is tomorrow; a tap flips it relative to that default
    bool preview = state.timeSynced && previewHr < 24 && localHour() >= previewHr;
    if (preview != state.previewActive) {
        state.previewActive = preview;
        state.displayDirty = true;
    }
}


/**
 * Main render function - draws the primary view into the buffer
 */
static int currentScreen() {
    return screenAt(state.screen, screensTap, screensHold);
}

// The ride screens: the default day is today, or tomorrow after previewHr; "rideOther" is the other one
static bool rideShowsTomorrow() {
    return (currentScreen() == SCREEN_RIDE_OTHER) != state.previewActive;
}

void renderDisplay() {
    WeatherData weather = getCurrentWeather();

    char rating = rideShowsTomorrow() ? getTomorrowRating() : getTodayRating();
    char badgeType = 0;   // unknown rating: ring only
    if (rating == RIDE_GOOD) badgeType = BADGE_CHECK;
    else if (rating == RIDE_CAUTION) badgeType = BADGE_WARN;
    else if (rating == RIDE_DONT) badgeType = BADGE_X;

    // Temperature (the API is always metric; convert for display)
    char tempStr[8];
    if (weatherUnits == "imperial") {
        snprintf(tempStr, sizeof(tempStr), "%.0fF", weather.tempC * 9.0f / 5.0f + 32.0f);
    } else {
        snprintf(tempStr, sizeof(tempStr), "%.0fC", weather.tempC);
    }

    renderPrimaryView(display, badgeType, state.isNight, weather.condition,
                     (weather.condition == WEATHER_RAIN ? 2 :
                      weather.condition == WEATHER_SNOW ? 1 : 0),
                     (int)weather.windKmh, tempStr, weather.trend, weather.precipMm);
}

// Temperature as shown on the kids screens (the API is always metric)
static int kidsShownTemp(float tempC) {
    if (isnan(tempC)) return 0;
    return (int)lroundf(weatherUnits == "imperial" ? tempC * 9.0f / 5.0f + 32.0f : tempC);
}

static bool nightAt(time_t t) {
    if (sunriseTime > 0 && sunsetTime > 0) return isNightAt((long)t, (long)sunriseTime, (long)sunsetTime);
    int h = localHourOf(t);
    return h >= 21 || h < 6;
}

/**
 * Kids variant: the nearest birthday or holiday within range, from the local date (none until the clock is set)
 */
static KidsCountdown kidsCountdownNow() {
    KidsCountdown none = {};
    if (!state.timeSynced || !timezoneKnown()) return none;
    struct tm t = {};
    time_t local = time(nullptr) + utcOffsetSeconds;
    gmtime_r(&local, &t);
    return nextKidsCountdown(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, kidsBirthdays, KIDS_MAX_BIRTHDAYS,
                             kidsHolidays, kidsCountdownDays);
}

/**
 * Kids screens: the next parts of the day (morning, afternoon, evening; at most `max`, the current one first)
 * with their outlook. Returns how many.
 */
static size_t kidsColumns(KidsColumn* cols, KidsPart* parts, size_t max) {
    // Forecast hours from the current one on; the current hour uses what is measured now
    const HourSlice* slices = nullptr;
    time_t firstEpoch = 0;
    size_t n = getUpcomingHours(slices, firstEpoch);
    KidsHour hours[24];
    int localHours[24];
    size_t count = 0;
    for (; count < n && count < 24; count++) {
        const HourSlice& s = slices[count];
        time_t t = firstEpoch + (time_t)count * 3600;
        hours[count] = KidsHour{ (float)s.tempC, s.rainTenthMm / 10.0f, (float)s.gustKmh,
                                 s.code == 255 ? -1 : (int)s.code, nightAt(t + 1800), s.valid };
        localHours[count] = localHourOf(t);
    }
    if (count > 0) {
        WeatherData w = getCurrentWeather();
        hours[0] = KidsHour{ w.tempC, w.precipMm, w.gustKmh, w.code, state.isNight, true };
    }

    size_t np = kidsDayParts(localHours, count, parts, max);
    for (size_t i = 0; i < np; i++) {
        // One number per part, shown on the weather screen and the one the outfit goes by
        KidsOutlook o = kidsPartOutlook(hours, parts[i], kidsLimits);
        cols[i] = KidsColumn{ parts[i].part, o.valid, o.outfit, o.weather, o.night, kidsShownTemp((float)o.tempC) };
    }
    return np;
}

/**
 * Kids variant: the next three parts of the day, as outfits or as weather
 */
void renderKids(bool weather) {
    KidsPart parts[3];
    KidsColumn cols[3];
    size_t np = kidsColumns(cols, parts, 3);
    int nowColumn = -1, nightBefore = -1;
    for (size_t i = 0; i < np; i++) {
        if (parts[i].now) nowColumn = (int)i;
        if (parts[i].afterSleep && i > 0 && nightBefore < 0) nightBefore = (int)i;
    }
    renderKidsDayStrip(display, cols, np, nowColumn, nightBefore, weather);
}

/**
 * Kids home screen: the outfit for now (the first column of the clothes screen) next to the village of the ride
 * screen, with its sun or moon, rain, snow, gusts and leaves
 */
void renderKidsVillage() {
    KidsPart part;
    KidsColumn col = {};
    bool any = kidsColumns(&col, &part, 1) > 0;
    WeatherData weather = getCurrentWeather();
    char tempStr[8];
    snprintf(tempStr, sizeof(tempStr), "%d%c", kidsShownTemp(weather.tempC), weatherUnits == "imperial" ? 'F' : 'C');
    renderKidsVillageView(display, col, any, any && part.afterSleep, state.isNight, weather.condition,
                          weather.condition == WEATHER_RAIN ? 2 : weather.condition == WEATHER_SNOW ? 1 : 0,
                          (int)weather.windKmh, tempStr, weather.trend);
}

/**
 * Get initialization status text for loading screen
 */
void getInitStatus(const char* &line1, const char* &line2) {
    if (!state.wifiConnected) {
        if (state.apModeStarted) {
            line1 = "AP Mode";
            line2 = "192.168.4.1";
        } else {
            line1 = "Connecting";
            line2 = "WiFi...";
        }
    } else if (!state.weatherValid) {
        line1 = "Fetching";
        line2 = "Weather...";
    } else {
        line1 = "Ready";
        line2 = "";
    }
}


/**
 * WiFi signal as 0-4 bars, -1 when not connected
 */
int wifiBars() {
    if (!state.wifiConnected) return -1;
    int rssi = state.wifiSignal;
    return rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
}


/**
 * Autumn at the location (the leaves in the wind animation). Needs the synced time and the time zone;
 * the southern hemisphere has its autumn in March-May.
 */
static bool autumnNow() {
    if (!state.timeSynced || !timezoneKnown()) return false;
    struct tm t = {};
    time_t local = time(nullptr) + utcOffsetSeconds;
    gmtime_r(&local, &t);
    return isAutumn(t.tm_mon + 1, configLat < 0);
}


/**
 * Draw the clock screen from NTP time and the location's UTC offset
 */
void renderClock() {
    bool valid = state.timeSynced && timezoneKnown();
    struct tm t = {};
    if (valid) {
        time_t local = time(nullptr) + utcOffsetSeconds;
        gmtime_r(&local, &t);
    }
    renderClockView(display, valid, t.tm_hour, t.tm_min, (t.tm_sec % 2) == 0,
                    t.tm_wday, t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
}


/**
 * Draw the hourly view from the stored forecast
 */
void renderHourly() {
    const HourSlice* hours = nullptr;
    time_t firstEpoch = 0;
    size_t count = getUpcomingHours(hours, firstEpoch);
    if (count > 6) count = 6;

    int leaveHour = 0;
    bool leaveNow = false;
    bool hasLeave = state.timeSynced && getBestLeave(leaveHour, leaveNow);

    int updHour = -1, updMin = 0;
    if (lastUpdateEpoch > 0) {
        time_t local = lastUpdateEpoch + utcOffsetSeconds;
        updHour = (int)((local / 3600) % 24);
        updMin = (int)((local / 60) % 60);
    }
    renderHourlyView(display, hours, count, localHourOf(firstEpoch), hasLeave, leaveHour, leaveNow, updHour, updMin);
}


/**
 * The weather report: a few sentences in the display language, word wrapped. The least important ones are left
 * out when they do not fit.
 */
#define REPORT_MAX_LINES 7
void renderReport() {
    const HourSlice* slices = nullptr;
    time_t firstEpoch = 0;
    size_t n = getUpcomingHours(slices, firstEpoch);
    ReportHour hours[36];
    size_t count = 0;
    for (; count < n && count < 36; count++) {
        const HourSlice& s = slices[count];
        hours[count] = ReportHour{ localHourOf(firstEpoch + (time_t)count * 3600), (float)s.tempC, s.rainTenthMm / 10.0f,
                                   (float)s.gustKmh, s.rainProb == 255 ? -1 : (int)s.rainProb,
                                   s.code == 255 ? -1 : (int)s.code, s.valid };
    }
    if (count > 0) {   // the current hour as measured now
        WeatherData w = getCurrentWeather();
        hours[0].tempC = w.tempC;
        hours[0].rainMm = w.precipMm;
        hours[0].code = w.code;
        hours[0].valid = true;
    }

    bool nl = displayLanguage == "nl";
    static WeatherReport report;   // static: about 400 bytes off the stack
    weatherReport(hours, count, nl ? REPORT_LANG_NL : REPORT_LANG_EN, report);
    if (report.count == 0) {
        report.count = 1;
        strcpy(report.sentences[0], nl ? "Nog geen verwachting." : "No forecast yet.");
        report.priority[0] = 4;
    }
    char lines[REPORT_MAX_LINES][REPORT_COLS + 1];
    renderReportView(display, lines, fitReport(report, lines, REPORT_MAX_LINES));
}


/**
 * Screens that can be shown right now (bit 1 << id): the clock needs the time, the countdown a birthday or
 * holiday within range
 */
static uint16_t availableScreens() {
    uint16_t available = 0xFFFF;
    if (!state.timeSynced || !timezoneKnown()) available &= ~(1u << SCREEN_CLOCK);
    if (!kidsCountdownNow().active) available &= ~(1u << SCREEN_COUNTDOWN);
    return available;
}

static const WindArea *windAnimationArea = nullptr;   // where the running gusts and leaves blow
static void stopWindAnimation();

// The area gusts and leaves blow in on a screen; nullptr for the screens without them
static const WindArea *windAreaFor(int screen) {
    if (screen == SCREEN_RIDE || screen == SCREEN_RIDE_OTHER || screen == SCREEN_VILLAGE) return &WIND_AREA_RIDE;
    if (screen == SCREEN_WEATHER || screen == SCREEN_CLOTHES) return &WIND_AREA_KIDS;
    return nullptr;
}

static void showScreen(ScreenNav nav) {
    // Between screens with the same area the leaves fly on where they were (a seamless switch); on a screen
    // without them they pause. Only a screen with the other area starts them afresh, so leaves from one
    // area are never drawn in the other.
    const WindArea *next = windAreaFor(screenAt(nav, screensTap, screensHold));
    if (state.windAnimationActive && next && next != windAnimationArea) stopWindAnimation();
    state.screen = nav;
    state.screenEnteredMs = millis();
    state.displayDirty = true;
}


/**
 * Flush the right view to the display
 */
void render() {
    int screen = currentScreen();
    KidsCountdown countdown = {};
    if (screen == SCREEN_COUNTDOWN) {
        countdown = kidsCountdownNow();
        if (!countdown.active) {               // the countdown ended (midnight) while it was shown
            state.screen = ScreenNav{ 0, 0 };
            screen = currentScreen();
        }
    }

    if (!state.wifiConnected && state.apModeStarted && !state.weatherValid) {
        renderApInfoView(display, AP_SSID, effectivePassword().c_str(), "192.168.4.1");
    } else if (screen == SCREEN_CLOCK) {
        renderClock();
    } else if (!state.weatherValid) {
        const char* line1;
        const char* line2;
        getInitStatus(line1, line2);
        renderLoadingView(display, line1, line2, millis());
    } else {
        switch (screen) {
        case SCREEN_WEATHER:   renderKids(true); break;
        case SCREEN_CLOTHES:   renderKids(false); break;
        case SCREEN_COUNTDOWN: renderKidsCountdown(display, countdown, millis()); break;
        case SCREEN_WEEK:      renderWeeklyMatrix(display, weekAM, weekPM, weekStartDow, weekBestDay); break;
        case SCREEN_HOURS:     renderHourly(); break;
        case SCREEN_REPORT:    renderReport(); break;
        case SCREEN_VILLAGE:   renderKidsVillage(); break;
        default:
            renderDisplay();
            renderStatusMarks(display, rideShowsTomorrow(), wifiBars());
        }
    }

    // Status marks in the free top-left corner of the rider screens (the kids screens have no room for them)
    bool riderScreen = screen == SCREEN_RIDE || screen == SCREEN_RIDE_OTHER || screen == SCREEN_WEEK || screen == SCREEN_HOURS;
    if (riderScreen && state.weatherValid && state.weatherStale) {
        // Data is old (offline or the API keeps failing)
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.print("OLD");
    } else if (state.weatherValid && otaStatus.available && (screen == SCREEN_RIDE || screen == SCREEN_RIDE_OTHER)) {
        // A firmware update is waiting in the web UI (the OLD mark wins the corner)
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.print("UPD");
    }
    display.display();
    state.displayDirty = false;
}


/**
 * Automatic screen cycling (display.cycleSeconds): steps through the tap list like a tap, skipping the screens
 * that cannot be shown right now
 */
static void cycleScreens() {
    // Nothing to cycle while it is off, the panel is off or there is no forecast yet: hold the timer
    if (displayCycleSeconds <= 0 || state.displayOff || !state.weatherValid) {
        state.lastCycleMs = millis();
        return;
    }
    if (!intervalPassed(state.lastCycleMs, (unsigned long)displayCycleSeconds * 1000UL)) return;
    state.lastCycleMs = millis();
    showScreen(screenNextTap(state.screen, screensTap, availableScreens()));
}


/**
 * Touch: a tap steps through the tap list (from the hold list it goes home), a long press through the hold
 * list (after its last screen home; without a hold list it is a tap).
 * touch_get_event() consumes the event, so it must be read exactly once per iteration.
 */
void handleTouch() {
    int event = touch_get_event();

    if (event != TOUCH_NONE) {
        state.lastActivityMs = millis();
        state.lastCycleMs = millis();   // the screen you just chose stays for a full cycle time
        if (state.displayOff) {
            // The first touch only wakes the panel
            state.displayOff = false;
            display.ssd1306_command(SSD1306_DISPLAYON);
            state.displayDirty = true;
            return;
        }
    }

    if (event == TOUCH_SHORT) {
        showScreen(screenNextTap(state.screen, screensTap, availableScreens()));
    } else if (event == TOUCH_LONG) {
        showScreen(screenNextHold(state.screen, screensTap, screensHold, availableScreens()));
    }

    // Back to the home screen after screensReturnSeconds. While the screens cycle by themselves, every screen
    // stays for the cycle time instead.
    bool home = state.screen.list == 0 && state.screen.slot == 0;
    if (!home && displayCycleSeconds == 0 && screensReturnSeconds > 0 &&
        intervalPassed(state.screenEnteredMs, (unsigned long)screensReturnSeconds * 1000UL)) {
        showScreen(ScreenNav{ 0, 0 });
    }
}


/**
 * Switch the panel off during quiet hours and after an idle period at night
 */
void managePower() {
    static unsigned long lastCheckMs = 0;
    if (!intervalPassed(lastCheckMs, 1000)) return;
    lastCheckMs = millis();

    bool awake = !intervalPassed(state.lastActivityMs, 30000UL);   // 30 s after a touch it stays on
    bool quiet = false;
    if (state.timeSynced && quietStartHr >= 0 && quietEndHr >= 0) {
        int h = localHour();
        quiet = (quietStartHr <= quietEndHr) ? (h >= quietStartHr && h < quietEndHr)
                                             : (h >= quietStartHr || h < quietEndHr);
    }
    bool sleepy = displaySleepMinutes > 0 && state.isNight &&
                  intervalPassed(state.lastActivityMs, (unsigned long)displaySleepMinutes * 60000UL);
    bool off = ((quiet || displayAlwaysSleep) && !awake) || sleepy;

    if (off != state.displayOff) {
        state.displayOff = off;
        display.ssd1306_command(off ? SSD1306_DISPLAYOFF : SSD1306_DISPLAYON);
        if (!off) state.displayDirty = true;
    }
}


/**
 * One-time work when a WiFi connection is (re)established
 */
void onWifiConnected() {
    state.everConnected = true;
    state.disconnectedSinceMs = 0;

    // Setup AP is no longer needed
    if (state.apModeStarted) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        state.apModeStarted = false;
    }

    // Network time (needed for sunrise/sunset and ride windows); runs in the background
    if (!state.ntpStarted) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        state.ntpStarted = true;
    }

    if (!state.mdnsStarted) {
        MDNS.begin("dayspeck");
        MDNS.addService("http", "tcp", 80);
        state.mdnsStarted = true;
    }

    // Pick the location for this network
    if (!manualLocation) {
        String currentSsid = WiFi.SSID();
        ssidBasedLocation = false;
        for (const auto& loc : ssidLocations) {
            if (loc.ssid == currentSsid) {
                configLat = loc.lat;
                configLon = loc.lon;
                ssidBasedLocation = true;
                break;
            }
        }

    }

    state.fetchNow = true;   // location may have changed
}


/**
 * WiFi supervision. The station keeps retrying in the background (auto-reconnect); the
 * setup AP is added alongside it (never replaces it) when there is no usable connection.
 */
void manageWifi() {
    bool connected = (WiFi.status() == WL_CONNECTED);
    unsigned long now = millis();

    if (connected) {
        if (!state.wifiConnected) {
            state.wifiConnected = true;
            onWifiConnected();
        }
        state.wifiSignal = WiFi.RSSI();
        return;
    }

    if (state.wifiConnected) {
        // Connection just dropped
        state.wifiConnected = false;
        if (state.mdnsStarted) {
            MDNS.close();
            state.mdnsStarted = false;
        }
    }

    if (state.disconnectedSinceMs == 0) {
        state.disconnectedSinceMs = now | 1;   // never 0 once set
    }

    unsigned long delayMs = state.everConnected ? WIFI_AP_DELAY_OUTAGE_MS : WIFI_AP_DELAY_FIRST_MS;
    if (!state.apModeStarted && (now - state.disconnectedSinceMs) > delayMs) {
        WiFi.mode(WIFI_AP_STA);   // keep the station side alive so it can still reconnect
        WiFi.softAP(AP_SSID, effectivePassword().c_str());
        state.apModeStarted = true;
        // AP IP will be 192.168.4.1
    }
}


/**
 * Fetch weather when due (blocking, but only once per interval)
 */
void handleWeatherFetch() {
    if (!state.wifiConnected) return;

    // After local midnight the cached week is shifted by a day: refresh
    if (state.weatherValid && forecastIsFromPastDay() && intervalPassed(state.lastFetchMs, 60000UL)) {
        state.fetchNow = true;
    }

    if (!state.fetchNow && !intervalPassed(state.lastFetchMs, state.nextFetchIntervalMs)) return;

    // The fetch blocks for up to a few seconds: show a dot in the corner while it runs
    if (state.weatherValid) {
        display.fillRect(124, 60, 3, 3, SSD1306_WHITE);
        display.display();
    }

    unsigned long serverInterval = fetchWeather(configLat, configLon);

    if (serverInterval > 0) {
        // 60-120 second jitter to avoid thundering herd
        state.nextFetchIntervalMs = serverInterval + random(60000, 120000);

        // At night run at 1/4 frequency
        if (state.isNight) {
            state.nextFetchIntervalMs *= 4;
        }

        // Cap maximum interval at 2 hours
        if (state.nextFetchIntervalMs > 7200000UL) {
            state.nextFetchIntervalMs = 7200000UL;
        }

        state.weatherValid = true;
        state.lastSuccessMs = millis();
        state.lastGoodIntervalMs = state.nextFetchIntervalMs;   // incl. jitter and the night multiplier
        state.weatherAge = 0;
        state.fetchFailures = 0;
    } else {
        // On failure keep showing the last good data and back off: 1, 2, 4, 8, then 15 minutes
        if (state.fetchFailures < 250) state.fetchFailures++;
        unsigned long backoff = 60000UL << min((int)state.fetchFailures - 1, 4);
        state.nextFetchIntervalMs = min(backoff, 900000UL);
    }

    state.fetchNow = false;
    state.lastFetchMs = millis();
    state.displayDirty = true;
    updateDayNight();   // sunrise/sunset may have changed
}


/**
 * Pull update progress: the install blocks the loop, so draw straight to the panel
 */
void drawOtaProgress(int percent) {
    if (state.displayOff) {
        state.displayOff = false;
        display.ssd1306_command(SSD1306_DISPLAYON);
    }
    char line[8];
    snprintf(line, sizeof(line), "%d%%", percent);
    renderLoadingView(display, "Updating", line, millis());
    display.display();
}


/**
 * Setup function - boot sequence, NO BLOCKING
 */
void setup() {
    LittleFS.begin();
    logMessage(("Boot: " + ESP.getResetReason()).c_str());
    loadConfig();

    // Initialize I2C with ESP01 pins; fast mode so a frame flush fits the animation budget
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000);

    initDisplay();
    renderLoadingView(display, "Dayspeck", "Booting...", 0);
    display.display();

    if (displayTouchEnabled) touch_init();
    initRainAnimation();
    randomSeed(ESP.getChipId() ^ micros());

    // Start WiFi connection in background - NO WAITING
    WiFi.persistent(false);          // don't wear the flash with credential writes
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);
    if (wifiSsid.length() > 0) {
        WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }

    initWebServer();
    // OTA lives on an unguessable path behind the admin password (see webserver.cpp)
    static String otaUrl = webOtaPath();
    static String otaPass = effectivePassword();
    httpUpdater.setup(&server, otaUrl.c_str(), ADMIN_USER, otaPass.c_str());
    // Every firmware update (pull or upload) must carry a valid signature, once a key is compiled in
    otaInit();
    otaProgressHook = drawOtaProgress;

    state.nextFetchIntervalMs = FETCH_INTERVAL_MS;
    state.fetchNow = true;           // fetch as soon as WiFi is up
    state.lastFrameMs = millis();
    state.lastActivityMs = millis();
    state.displayDirty = true;
}


/**
 * The wind animation runs in one area at a time (the ride screen's right half, or the whole kids screen):
 * coming from a screen with the other area starts it afresh
 */
static void startWindAnimation(const WindArea &area) {
    if (!state.windAnimationActive || windAnimationArea != &area) {
        initWindAnimation(area);
        windAnimationArea = &area;
        state.windAnimationActive = true;
    }
}

static void stopWindAnimation() {
    initWindAnimation(windAnimationArea ? *windAnimationArea : WIND_AREA_RIDE);
    state.windAnimationActive = false;
    state.displayDirty = true;
}


/**
 * Main loop - non-blocking apart from the periodic weather fetch
 */
void loop() {
    if (displayTouchEnabled) touch_update();
    handleTouch();
    cycleScreens();

    manageWifi();
    handleWebServer();
    if (state.mdnsStarted) {
        MDNS.update();
    }

    updateDayNight();
    managePower();
    handleWeatherFetch();
    otaLoop(intervalPassed(state.lastActivityMs, 60000UL));   // a check pauses the display: not while in use

    if (state.weatherValid) {
        unsigned long sinceSuccess = millis() - state.lastSuccessMs;
        state.weatherAge = sinceSuccess / 60000UL;
        bool stale = sinceSuccess > 2 * state.lastGoodIntervalMs;
        if (stale != state.weatherStale) {
            state.weatherStale = stale;
            state.displayDirty = true;
        }
    }

    int screen = currentScreen();
    // The report and the kids village change with the hour (from 18:00 the report is about tomorrow; the
    // outfit follows the part of the day)
    if ((screen == SCREEN_REPORT || screen == SCREEN_VILLAGE) && !state.displayOff && timezoneKnown()) {
        static int lastHour = -1;
        int hour = localHourOf(time(nullptr));
        if (hour != lastHour) {
            lastHour = hour;
            state.displayDirty = true;
        }
    }
    // The clock redraws every second (blinking colon)
    if (screen == SCREEN_CLOCK && !state.displayOff) {
        static time_t lastSecond = 0;
        time_t nowSecond = time(nullptr);
        if (nowSecond != lastSecond) {
            lastSecond = nowSecond;
            state.displayDirty = true;
        }
    }

    // Animation frame tick (15 FPS); nothing to draw while the panel is off
    if (!state.displayOff && intervalPassed(state.lastFrameMs, RAIN_FRAME_INTERVAL)) {
        if (!state.weatherValid) {
            state.displayDirty = true;   // loading animation
        } else if (screen == SCREEN_RIDE || screen == SCREEN_RIDE_OTHER || screen == SCREEN_VILLAGE) {
            WeatherData weather = getCurrentWeather();
            bool rain = weather.condition == WEATHER_RAIN;
            bool gustsOn = weather.condition == WEATHER_WIND;
            bool leavesOn = leavesBlowing(autumnNow(), weather.condition, weather.windKmh);
            if (rain) {
                float rainIntensity = min(weather.precipMm * 2.0f, 20.0f);
                updateRainAnimation((int)weather.windKmh, rainIntensity);
                state.rainAnimationActive = true;
                state.displayDirty = true;
            } else if (state.rainAnimationActive) {
                // Rain stopped - reset animation completely
                initRainAnimation();
                state.rainAnimationActive = false;
                state.displayDirty = true;
            }
            if (!rain && (gustsOn || leavesOn)) {
                startWindAnimation(WIND_AREA_RIDE);
                updateWindAnimation((int)weather.windKmh, gustsOn, leavesOn);
                state.displayDirty = true;
            } else if (state.windAnimationActive) {
                // The wind dropped, or it started to rain: stop the gusts and leaves
                stopWindAnimation();
            }
        } else if (screen == SCREEN_WEATHER || screen == SCREEN_CLOTHES) {
            // Autumn leaves blow across the kids screens
            WeatherData w = getCurrentWeather();
            int weatherNow = kidsWeatherFor(w.code, w.gustKmh, kidsLimits.windyGustKmh);
            if (kidsLeavesBlowing(autumnNow(), weatherNow, w.gustKmh)) {
                startWindAnimation(WIND_AREA_KIDS);
                updateWindAnimation((int)w.gustKmh, false, true);
                state.displayDirty = true;
            } else if (state.windAnimationActive) {
                stopWindAnimation();
            }
        } else if (screen == SCREEN_COUNTDOWN) {
            KidsCountdown countdown = kidsCountdownNow();
            if (countdown.active && countdown.sleeps == 0) state.displayDirty = true;   // confetti on the day itself
        }
        state.lastFrameMs = millis();
    }

    // Render ONLY if something actually changed
    if (state.displayDirty && !state.displayOff) {
        render();
    }

    delay(1);   // let the WiFi stack run
}
