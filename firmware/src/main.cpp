// MotoWeather Bedside Display — Main Entry Point
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
#include "geolocation.h"
#include "bitmaps.h"
#include "display.h"
#include "weather.h"
#include "touch.h"
#include "webserver.h"
#include "security.h"

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
bool geolocationActive = false;
bool manualConfigPresent = false;   // config.json defines a location
bool manualLocation = false;        // user picked a location in the web UI
bool ssidBasedLocation = false;

// WiFi configuration (loaded from config.json)
String wifiSsid = WIFI_SSID;  // Default to compile-time values
String wifiPassword = WIFI_PASS;

// Weather API configuration (loaded from config.json)
String weatherApiKey = DEFAULT_WEATHER_API_KEY;
String weatherApiUrl = DEFAULT_WEATHER_API_URL;
String weatherUnits = DEFAULT_WEATHER_UNITS;
bool weatherDebug = DEFAULT_WEATHER_DEBUG;

#define WIFI_AP_DELAY_FIRST_MS    30000UL    // never connected: start the setup AP after 30 s
#define WIFI_AP_DELAY_OUTAGE_MS   300000UL   // lost a working connection: AP only after 5 min
#define WEEKLY_VIEW_TIMEOUT_MS    30000UL
#define AP_SSID                   "MotoWeather"
#define MIN_VALID_EPOCH           1600000000L // anything earlier means NTP has not synced


/**
 * Rollover-safe timing check (unsigned subtraction handles millis() wrap at 49 days)
 */
bool intervalPassed(unsigned long lastRun, unsigned long interval) {
    return (millis() - lastRun) >= interval;
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

    // Thresholds
    if (doc["thresholds"].is<JsonObject>()) {
        if (doc["thresholds"]["maxRainMm"].is<float>()) maxRainMm = doc["thresholds"]["maxRainMm"];
        if (doc["thresholds"]["maxWindKmh"].is<float>()) maxWindKmh = doc["thresholds"]["maxWindKmh"];
        if (doc["thresholds"]["minTempC"].is<float>()) minTempC = doc["thresholds"]["minTempC"];
        if (doc["thresholds"]["warnWindKmh"].is<float>()) warnWindKmh = doc["thresholds"]["warnWindKmh"];
    }

    // Ride windows: [start hour, length in hours]
    loadWindow(doc["wd_am"], weekdayAM);
    loadWindow(doc["wd_pm"], weekdayPM);
    loadWindow(doc["we_am"], weekendAM);
    loadWindow(doc["we_pm"], weekendPM);

    // Weather API config
    if (doc["weatherApiKey"].is<String>()) weatherApiKey = doc["weatherApiKey"].as<String>();
    if (doc["weatherApiUrl"].is<String>()) weatherApiUrl = doc["weatherApiUrl"].as<String>();
    if (doc["weatherUnits"].is<String>()) weatherUnits = doc["weatherUnits"].as<String>();
    if (doc["weatherDebug"].is<bool>()) weatherDebug = doc["weatherDebug"].as<bool>();

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
            // Sunrise/sunset are for the forecast's first day; roll them forward by whole days
            long days = (now >= sunriseTime) ? (long)((now - sunriseTime) / 86400) : 0;
            time_t rise = sunriseTime + days * 86400;
            time_t set = sunsetTime + days * 86400;
            night = (now < rise || now > set);
        } else {
            int hour = localHour();
            night = (hour >= 21 || hour < 6);
        }
    }

    if (night != state.isNight) {
        state.isNight = night;
        state.displayDirty = true;
    }
}


/**
 * Main render function - draws the primary view into the buffer
 */
void renderDisplay() {
    WeatherData weather = getCurrentWeather();

    char rating = state.showTomorrow ? getTomorrowRating() : getTodayRating();
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
 * Flush the right view to the display
 */
void render() {
    if (!state.wifiConnected && state.apModeStarted && !state.weatherValid) {
        renderApInfoView(display, AP_SSID, effectivePassword().c_str(), "192.168.4.1");
    } else if (!state.weatherValid) {
        const char* line1;
        const char* line2;
        getInitStatus(line1, line2);
        renderLoadingView(display, line1, line2, millis());
    } else if (state.displayMode == 1) {
        renderWeeklyMatrix(display, weekAM, weekPM, weekStartDow);
    } else {
        renderDisplay();
    }
    display.display();
    state.displayDirty = false;
}


/**
 * Touch: short tap = today/tomorrow (or leave weekly view), long press = weekly view.
 * touch_get_event() consumes the event, so it must be read exactly once per iteration.
 */
void handleTouch() {
    int event = touch_get_event();

    if (event == TOUCH_SHORT) {
        if (state.displayMode == 1) {
            state.displayMode = 0;
        } else {
            state.showTomorrow = !state.showTomorrow;
        }
        state.displayDirty = true;
    } else if (event == TOUCH_LONG) {
        state.displayMode = (state.displayMode == 0) ? 1 : 0;
        if (state.displayMode == 1) {
            state.weeklyEnteredMs = millis();
        }
        state.displayDirty = true;
    }

    // The weekly view closes itself
    if (state.displayMode == 1 && intervalPassed(state.weeklyEnteredMs, WEEKLY_VIEW_TIMEOUT_MS)) {
        state.displayMode = 0;
        state.displayDirty = true;
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
        MDNS.begin("motoclock");
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

        // No better source: try automatic geolocation once per boot if a key is configured
        if (!ssidBasedLocation && !state.geolocationTried && geolocationConfigured()) {
            state.geolocationTried = true;
            float lat, lon;
            if (geolocateDevice(lat, lon)) {
                configLat = lat;
                configLon = lon;
                geolocationActive = true;
            } else {
                logMessage("Geolocation failed, using configured location");
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
        state.weatherAge = 0;
    } else {
        // On failure: retry in 1 minute; keep showing the last good data
        state.nextFetchIntervalMs = 60000UL;
    }

    state.fetchNow = false;
    state.lastFetchMs = millis();
    state.displayDirty = true;
    updateDayNight();   // sunrise/sunset may have changed
}


/**
 * Setup function - boot sequence, NO BLOCKING
 */
void setup() {
    LittleFS.begin();
    loadConfig();

    // Initialize I2C with ESP01 pins; fast mode so a frame flush fits the animation budget
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000);

    initDisplay();
    renderLoadingView(display, "MotoWeather", "Booting...", 0);
    display.display();

    touch_init();
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

    state.nextFetchIntervalMs = FETCH_INTERVAL_MS;
    state.fetchNow = true;           // fetch as soon as WiFi is up
    state.lastFrameMs = millis();
    state.displayDirty = true;
}


/**
 * Main loop - non-blocking apart from the periodic weather fetch
 */
void loop() {
    touch_update();
    handleTouch();

    manageWifi();
    handleWebServer();
    if (state.mdnsStarted) {
        MDNS.update();
    }

    updateDayNight();
    handleWeatherFetch();

    if (state.weatherValid) {
        state.weatherAge = (millis() - state.lastSuccessMs) / 60000UL;
    }

    // Animation frame tick (15 FPS)
    if (intervalPassed(state.lastFrameMs, RAIN_FRAME_INTERVAL)) {
        if (!state.weatherValid) {
            state.displayDirty = true;   // loading animation
        } else if (state.displayMode == 0) {
            WeatherData weather = getCurrentWeather();
            if (weather.condition == WEATHER_RAIN) {
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
        }
        state.lastFrameMs = millis();
    }

    // Render ONLY if something actually changed
    if (state.displayDirty) {
        render();
    }

    delay(1);   // let the WiFi stack run
}
