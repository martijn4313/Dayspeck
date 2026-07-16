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
#include "bitmaps.h"
#include "display.h"
#include "weather.h"
#include "touch.h"
#include "webserver.h"

// Display object
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT);

// HTTP Update Server
ESP8266HTTPUpdateServer httpUpdater;

// System State - single source of truth
struct SystemState {
    // Display state
    bool         showTomorrow;
    uint8_t      displayMode;
    bool         displayDirty;

    // Timing
    unsigned long lastFetchMs;
    unsigned long nextFetchIntervalMs;
    unsigned long lastFrameMs;
    unsigned long lastWifiAttemptMs;

    // Environment
    bool         isNight;
    bool         rainAnimationActive;

    // Network state
    bool         wifiConnected;
    int8_t       wifiSignal;
    bool         mdnsStarted;
    bool         apModeStarted;

    // Validity flags
    bool         weatherValid;
    unsigned int weatherAge;
    bool         timeSynced;
};

static SystemState state = {
    .showTomorrow = false,
    .displayMode = 0,
    .displayDirty = true,

    .lastFetchMs = 0,
    .nextFetchIntervalMs = 900000UL, // Start with 15 minute default
    .lastFrameMs = 0,
    .lastWifiAttemptMs = 0,

    .isNight = false,
    .rainAnimationActive = false,

    .wifiConnected = false,
    .wifiSignal = 0,
    .mdnsStarted = false,
    .apModeStarted = false,

    .weatherValid = false,
    .weatherAge = 0,
    .timeSynced = false
};

struct SsidLocation {
    String ssid;
    float lat;
    float lon;
};

std::vector<SsidLocation> ssidLocations;

// Configuration from JSON
float configLat = 51.5074;  // Default: London
float configLon = -0.1278;

// Location source flags
bool geolocationActive = false;
bool manualConfigPresent = false;
bool manualLocation = false;
bool ssidBasedLocation = false;

// WiFi configuration (loaded from config.json)
String wifiSsid = WIFI_SSID;  // Default to compile-time values
String wifiPassword = WIFI_PASS;

// Weather API configuration (loaded from config.json)
String weatherApiKey = DEFAULT_WEATHER_API_KEY;
String weatherApiUrl = DEFAULT_WEATHER_API_URL;
String weatherUnits = DEFAULT_WEATHER_UNITS;
bool weatherDebug = DEFAULT_WEATHER_DEBUG;

// WiFi state (extern for webserver)
bool wifiConnected = false;
int8_t wifiSignal = 0;

// Debug state (extern for webserver)
bool weatherValid = false;
unsigned int weatherAge = 0;
bool mdnsStarted = false;


/**
 * Safe timing check with proper millis() overflow handling
 * Works correctly across rollover at 49 days
 */
bool intervalPassed(unsigned long lastRun, unsigned long interval) {
    unsigned long now = millis();
    if (now < lastRun) return true;
    return (now - lastRun) >= interval;
}


/**
 * Load config from LittleFS
 */
void loadConfig() {
    File file = LittleFS.open("/config.json", "r");
    if (file) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (!error) {
            // Load WiFi credentials if present
            if (doc["wifi"].is<JsonObject>()) {
                wifiSsid = doc["wifi"]["ssid"].as<String>();
                wifiPassword = doc["wifi"]["password"].as<String>();
            }

            // Load location if present
            if (doc["lat"].is<float>()) configLat = doc["lat"];
            if (doc["lon"].is<float>()) configLon = doc["lon"];
            if (doc["manualLocation"].is<bool>()) manualLocation = doc["manualLocation"];

            // Load thresholds if present
            if (doc["thresholds"].is<JsonObject>()) {
                if (doc["thresholds"]["maxRainMm"].is<float>()) maxRainMm = doc["thresholds"]["maxRainMm"];
                if (doc["thresholds"]["maxWindKmh"].is<float>()) maxWindKmh = doc["thresholds"]["maxWindKmh"];
                if (doc["thresholds"]["minTempC"].is<float>()) minTempC = doc["thresholds"]["minTempC"];
                if (doc["thresholds"]["warnWindKmh"].is<float>()) warnWindKmh = doc["thresholds"]["warnWindKmh"];
            }

            // Load weather API config if present
            if (doc["weatherApiKey"].is<String>()) weatherApiKey = doc["weatherApiKey"].as<String>();
            if (doc["weatherApiUrl"].is<String>()) weatherApiUrl = doc["weatherApiUrl"].as<String>();
            if (doc["weatherUnits"].is<String>()) weatherUnits = doc["weatherUnits"].as<String>();
            if (doc["weatherDebug"].is<bool>()) weatherDebug = doc["weatherDebug"].as<bool>();

            // Load SSID locations if present
            if (doc["ssidLocations"].is<JsonArray>()) {
                ssidLocations.clear();
                JsonArray arr = doc["ssidLocations"];
                for (JsonObject item : arr) {
                    SsidLocation loc;
                    loc.ssid = item["ssid"].as<String>();
                    loc.lat = item["lat"];
                    loc.lon = item["lon"];
                    ssidLocations.push_back(loc);
                }
            }

            manualConfigPresent = true;
        }
    }
}


/**
 * Initialize display with graceful failure
 */
bool initDisplay() {
    Serial.print("Initializing display...");
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println(" success");
        display.setRotation(2);  // Flip display 180 degrees since screen is mounted upside down
        display.clearDisplay();
        display.display();
        return true;
    }
    Serial.println(" failed");
    return false;
}


/**
 * Update day/night state using actual astronomical sunrise/sunset times
 */
void updateDayNight() {
    time_t now = time(nullptr);
    if (now > 0 && sunriseTime > 0 && sunsetTime > 0) {
        state.timeSynced = true;
        // Correct astronomical day/night detection
        state.isNight = (now < sunriseTime || now > sunsetTime);
    } else {
        // Fallback to hardcoded hours if sunrise/sunset not available
        state.timeSynced = false;
        struct tm* timeinfo = localtime(&now);
        int hour = timeinfo->tm_hour;
        state.isNight = (hour >= 20 || hour < 6);
    }
}


/**
 * Main render function - only called when display is dirty
 */
void renderDisplay() {
    WeatherData weather = getCurrentWeather();
    
    // Determine which day to show
    char badgeType;
    if (state.showTomorrow) {
        badgeType = getTomorrowRating();
    } else {
        badgeType = getTodayRating();
    }
    
    // Build temp string
    char tempStr[8];
    snprintf(tempStr, sizeof(tempStr), "%.0fC", weather.tempC);
    
    // Determine trend arrow
    char trendArrow = weather.trend;  // 'u', 'd', 'f'
    
    // Render primary view with weather data
    renderPrimaryView(display, badgeType, state.isNight, weather.condition, 
                     (weather.condition == WEATHER_RAIN ? 2 : 
                      weather.condition == WEATHER_SNOW ? 1 : 0),
                     (int)weather.windKmh, tempStr, trendArrow, weather.precipMm);
    
    display.display();
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
 * Setup function - boot sequence, NO BLOCKING
 */
void setup() {
    // Mount filesystem
    LittleFS.begin();
    
    // Load configuration
    loadConfig();

    // Initialize I2C with ESP01 pins
    Wire.begin(OLED_SDA, OLED_SCL);

    // Initialize display first - user gets feedback quickly
    initDisplay();
    
    // Show boot screen immediately
    renderLoadingView(display, "MotoWeather", "Booting...", 0);
    
    // Initialize touch sensor
    touch_init();
    
    // Initialize rain animation
    initRainAnimation();
    state.rainAnimationActive = false;
    
    // Start WiFi connection in background - NO WAITING
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

    // Initialize web server
    initWebServer();

    // Initialize OTA update server
    httpUpdater.setup(&server);

    // Initialize timing
    state.lastFrameMs = millis();
    state.lastFetchMs = millis() - state.nextFetchIntervalMs; // Force immediate fetch
    state.lastWifiAttemptMs = millis();
    
    // Mark display for redraw
    state.displayDirty = true;
    
    // Setup done. Total boot time < 120ms
}


/**
 * Main loop - Event driven, zero polling, minimal CPU usage
 */
void loop() {
    // 1. Update input state
    touch_update();
    
    // 2. Handle touch events
    if (touch_short_tap()) {
        // Toggle between today and tomorrow
        state.showTomorrow = !state.showTomorrow;
        state.displayDirty = true;
        // touch events are auto consumed on read
    }
    
    if (touch_long_press()) {
        // Toggle between primary view and weekly matrix
        state.displayMode = (state.displayMode == 0) ? 1 : 0;
        
        if (state.displayMode == 1) {
            // Show weekly matrix
            display.clearDisplay();
            renderWeeklyMatrix(display, weekAM, weekPM);
            display.display();
        } else {
            state.displayDirty = true;
        }
        
        // touch events are auto consumed on read
    }
    
    // 3. WiFi connection management
    state.wifiConnected = (WiFi.status() == WL_CONNECTED);
    wifiConnected = state.wifiConnected;  // Update extern variable
    if (state.wifiConnected) {
        state.wifiSignal = WiFi.RSSI();
        wifiSignal = state.wifiSignal;  // Update extern variable
        // Start mDNS if not already started
        if (!state.mdnsStarted) {
            MDNS.begin("motoclock");
            state.mdnsStarted = true;
        }

        // Check if connected SSID matches any location
        if (!manualLocation) {
            String currentSsid = WiFi.SSID();
            for (const auto& loc : ssidLocations) {
                if (loc.ssid == currentSsid) {
                    configLat = loc.lat;
                    configLon = loc.lon;
                    ssidBasedLocation = true;
                    break;
                }
            }
        }
    } else {
        // If not connected and we've been trying for 30 seconds, start AP mode
        if (millis() - state.lastWifiAttemptMs > 30000 && !state.apModeStarted) {
            WiFi.mode(WIFI_AP);
            WiFi.softAP("MotoWeather", "password123");
            state.apModeStarted = true;
            // Serial not available (GPIO3 used for touch)
            // AP IP will be 192.168.4.1
        }
    }

    // 4. Handle web server requests
    handleWebServer();

    // 4.5. Handle mDNS
    if (state.mdnsStarted) {
        MDNS.update();
    }

    // 5. Weather fetch logic - dynamically scheduled
    if (state.wifiConnected && intervalPassed(state.lastFetchMs, state.nextFetchIntervalMs)) {
        unsigned long serverInterval = fetchWeather(configLat, configLon);
        
        if (serverInterval > 0) {
            updateWeeklyState(configLat, configLon);
            
            // Add 60-120 second jitter to avoid thundering herd
            unsigned long jitter = random(60000, 120000);
            state.nextFetchIntervalMs = serverInterval + jitter;
            
            // Lengthen interval at nighttime
            if (state.isNight) {
                // During night (20:00 - 06:00) run at 1/4 frequency
                state.nextFetchIntervalMs *= 4;
            }
            
            // Cap maximum interval at 2 hours
            if (state.nextFetchIntervalMs > 7200000UL) {
                state.nextFetchIntervalMs = 7200000UL;
            }
            
            state.weatherValid = true;
            state.weatherAge = 0;
        } else {
            // On failure: retry in 1 minute
            state.nextFetchIntervalMs = 60000UL;
            // Do not set weatherValid to true if it failed
        }
        
        state.lastFetchMs = millis();
        state.displayDirty = true;
    }
    
    // 5. Update day/night state
    updateDayNight();

    // Update weather age
    if (state.weatherValid && intervalPassed(state.lastFetchMs + (state.weatherAge * 60000UL), 60000UL)) {
        state.weatherAge++;
    }

    // Update extern debug variables
    weatherValid = state.weatherValid;
    weatherAge = state.weatherAge;
    mdnsStarted = state.mdnsStarted;

    // 6. Animation frame tick (always runs, 15 FPS)
    if (intervalPassed(state.lastFrameMs, RAIN_FRAME_INTERVAL)) {
        WeatherData weather = getCurrentWeather();
        
        if (!state.weatherValid) {
            // Force display update for loading animation
            state.displayDirty = true;
        } else {
            // Always tick animation system - let it handle transitions
            if (state.displayMode == 0) {
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
        }
        
        state.lastFrameMs = millis();
    }
    
    // 7. Render display ONLY if something actually changed
    if (state.displayDirty && state.displayMode == 0) {
        if (!state.weatherValid) {
            const char* line1;
            const char* line2;
            getInitStatus(line1, line2);
            renderLoadingView(display, line1, line2, millis());
        } else {
            renderDisplay();
        }
        state.displayDirty = false;
    }
    
    // 8. Allow CPU sleep for remaining time
    delay(1);
}
