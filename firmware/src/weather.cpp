// MotoWeather Bedside Display — Weather Data Implementation
// Open-Meteo API fetch, ArduinoJson streaming filter, ride decision logic

#include "weather.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// Extern weather API config from main.cpp
extern String weatherApiKey;
extern String weatherApiUrl;
extern String weatherUnits;
extern bool weatherDebug;

// Weekly state arrays
char weekAM[7] = { 'G', 'G', 'G', 'G', 'G', 'G', 'G' };
char weekPM[7] = { 'G', 'G', 'G', 'G', 'G', 'G', 'G' };

// Current weather data
static WeatherData currentWeather = { 0, 0, 0, 0, WEATHER_CLEAR, 'f' };

// Display mode (0 = primary view, 1 = weekly matrix)
static int displayMode = 0;

// Logging system - circular buffer of 16 entries
static char logBuffer[MAX_LOG_ENTRIES][128];
static uint8_t logHead = 0;
static uint8_t logCount = 0;

// Ride decision thresholds (can be overridden by config.json)
float maxRainMm = DEFAULT_MAX_RAIN_MM;
float maxWindKmh = DEFAULT_MAX_WIND_KMH;
float minTempC = DEFAULT_MIN_TEMP_C;
float warnWindKmh = DEFAULT_WARN_WIND_KMH;

// Load thresholds from config.json or use defaults
void loadThresholds() {
    // TODO: Read from LittleFS config.json and override defaults
    // For now, use compile-time defaults from config.h
    maxRainMm = DEFAULT_MAX_RAIN_MM;
    maxWindKmh = DEFAULT_MAX_WIND_KMH;
    minTempC = DEFAULT_MIN_TEMP_C;
    warnWindKmh = DEFAULT_WARN_WIND_KMH;
}

// Store sunrise/sunset times for day/night detection
time_t sunriseTime = 0;
time_t sunsetTime = 0;

// HTTP GET to Weather API with ArduinoJson streaming filter
unsigned long fetchWeather(float lat, float lon) {
    WiFiClient client;
    WiFiClientSecure clientSecure;
    HTTPClient http;

    // Build URL with lat, lon, parameters based on units
    String url = weatherApiUrl;
    if (url.startsWith("https://")) {
        url.replace(0, 8, "http://");
    }
    url += "?latitude=";
    url += String(lat, 6);
    url += "&longitude=";
    url += String(lon, 6);
    url += "&current=temperature_2m,precipitation,wind_speed_10m,wind_gusts_10m,weather_code";
    url += "&hourly=temperature_2m,precipitation_probability,wind_gusts_10m";
    url += "&forecast_hours=12&forecast_days=7";
    url += "&timezone=auto";

    // Add units parameters
    if (weatherUnits == "imperial") {
        url += "&temperature_unit=fahrenheit&windspeed_unit=mph";
    } else {
        url += "&temperature_unit=celsius&windspeed_unit=kmh";
    }

    // Add API key if present
    if (weatherApiKey.length() > 0) {
        url += "&apikey=" + weatherApiKey;
    }

    if (weatherDebug) {
        Serial.println("[Weather] Fetching current weather from:");
        Serial.println(url);
        logMessage("Starting weather fetch...");
    }

    http.begin(client, url);
    
    int httpCode = http.GET();
    
    if (weatherDebug) {
        Serial.print("[Weather] HTTP Code: ");
        Serial.println(httpCode);
    }
    
    // Default next fetch interval - 15 minutes fallback
    unsigned long nextFetchMs = 900000UL;
    
    if (httpCode == HTTP_CODE_OK) {
        // Extract Cache-Control header to know when data actually expires
        String cacheControl = http.header("Cache-Control");
        if (cacheControl.startsWith("max-age=")) {
            long maxAgeSeconds = cacheControl.substring(8).toInt();
            if (maxAgeSeconds > 60) {
                nextFetchMs = maxAgeSeconds * 1000UL;
            }
        }
        String payload = http.getString();

        if (weatherDebug) {
            Serial.printf("[Weather] HTTP OK, payload length: %d bytes\n", payload.length());
            char logBuf[128];
            snprintf(logBuf, sizeof(logBuf), "Weather API: HTTP OK, %d bytes", payload.length());
            logMessage(logBuf);
            if (payload.length() == 0) {
                Serial.println("[Weather] WARNING: Empty payload received from API");
                logMessage("Weather API WARNING: Empty payload!");
            }
        }
        
        // Parse current weather using ArduinoJson
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            // Extract current weather
            JsonObject current = doc["current"];
            if (current["temperature_2m"].is<float>()) {
                currentWeather.tempC = current["temperature_2m"].as<float>();
            }
            if (current["precipitation"].is<float>()) {
                currentWeather.precipMm = current["precipitation"].as<float>();
            }
            if (current["wind_speed_10m"].is<float>()) {
                currentWeather.windKmh = current["wind_speed_10m"].as<float>();
            }
            if (current["wind_gusts_10m"].is<float>()) {
                currentWeather.gustKmh = current["wind_gusts_10m"].as<float>();
            }
            if (current["weather_code"].is<int>()) {
                int code = current["weather_code"].as<int>();
                // Map WMO weather codes to our conditions
                // 0 = clear, 1-3 = partly cloudy, 45-48 = fog, 51-67 = rain, 71-77 = snow, 80-82 = showers, 95-99 = thunderstorm
                if (code == 0) {
                    currentWeather.condition = WEATHER_CLEAR;
                } else if (code >= 51 && code <= 67) {
                    currentWeather.condition = WEATHER_RAIN;
                } else if (code >= 71 && code <= 77) {
                    currentWeather.condition = WEATHER_SNOW;
                } else if (code >= 80 && code <= 82) {
                    currentWeather.condition = WEATHER_RAIN;
                } else if (code >= 95) {
                    currentWeather.condition = WEATHER_RAIN;
                } else if (currentWeather.windKmh > 25) {
                    currentWeather.condition = WEATHER_WIND;
                } else {
                    currentWeather.condition = WEATHER_CLEAR;
                }
            }
            
            // Extract sunrise sunset for accurate astronomical day/night detection
            JsonObject daily = doc["daily"];
            if (daily["sunrise"].is<JsonArray>() && daily["sunset"].is<JsonArray>()) {
                sunriseTime = daily["sunrise"][0].as<time_t>();
                sunsetTime = daily["sunset"][0].as<time_t>();
            }
            
            // Determine trend by comparing to yesterday's temperature (simplified)
            currentWeather.trend = 'f';  // flat by default
            
            http.end();
            return nextFetchMs;
        }
    } else {
        if (weatherDebug) {
            Serial.printf("[Weather] API request FAILED, HTTP code: %d\n", httpCode);
            Serial.printf("[Weather] Error string: %s\n", http.errorToString(httpCode).c_str());
            char logBuf[128];
            snprintf(logBuf, sizeof(logBuf), "Weather API FAILED: HTTP %d - %s", httpCode, http.errorToString(httpCode).c_str());
            logMessage(logBuf);
        }
    }
    
    http.end();
    
    // On failure: return 0 to indicate failure
    return 0;
}

// Ride decision algorithm
char evaluateRide(float precipMm, float gustKmh, float tempC, float windKmh) {
    // IF (precipMm > max_rain_mm) OR (gustKmh > max_wind_kmh) → RIDE_DONT
    if (precipMm > maxRainMm || gustKmh > maxWindKmh) {
        return RIDE_DONT;
    }
    
    // ELSE IF (precipMm > 0) OR (tempC < min_temp) OR (gustKmh > warn_wind_kmh) → RIDE_CAUTION
    if (precipMm > 0 || tempC < minTempC || gustKmh > warnWindKmh) {
        return RIDE_CAUTION;
    }
    
    // ELSE → RIDE_GOOD
    return RIDE_GOOD;
}

// Update weekly state — fetch and evaluate all 7 days
void updateWeeklyState(float lat, float lon) {
    WiFiClient client;
    WiFiClientSecure clientSecure;
    HTTPClient http;
    
    // Build URL for 7-day forecast
    String url = String(API_BASE_URL);
    if (url.startsWith("https://")) {
        url.replace(0, 8, "http://");
    }
    url += "?latitude=";
    url += String(lat, 6);
    url += "&longitude=";
    url += String(lon, 6);
    url += "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum,wind_speed_10m_max";
    url += "&timezone=auto";
    url += "&forecast_days=7";
    
    if (weatherDebug) {
        Serial.println("[Weather] Fetching weekly forecast from:");
        Serial.println(url);
    }

    if (url.startsWith("https://")) {
        clientSecure.setInsecure();
        http.begin(clientSecure, url);
    } else {
        http.begin(client, url);
    }
    
    int httpCode = http.GET();
    
    if (weatherDebug) {
        Serial.print("[Weather] Weekly HTTP Code: ");
        Serial.println(httpCode);
    }
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        // Full payload logging REMOVED: Causes complete system hang on ESP8266
        // Serial tx blocks CPU for 8+ seconds, starves watchdog & webserver
        if (weatherDebug) {
            Serial.printf("[Weather] Weekly Payload received: %d bytes\n", payload.length());
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            // Parse daily data
            JsonObject daily = doc["daily"];
            JsonArray time = daily["time"].as<JsonArray>();
            JsonArray tempMax = daily["temperature_2m_max"].as<JsonArray>();
            JsonArray precipSum = daily["precipitation_sum"].as<JsonArray>();
            JsonArray windMax = daily["wind_speed_10m_max"].as<JsonArray>();
            
            for (size_t day = 0; day < 7 && day < time.size(); day++) {
                // For each day, evaluate AM (6-12) and PM (12-18) windows
                // Simplified: just use daily totals for now
                float dailyPrecip = precipSum[day] | 0.0;
                float dailyWind = windMax[day] | 0.0;
                float dailyTemp = tempMax[day] | 10.0;
                
                // Evaluate AM ride
                // For simplicity, use half of daily precip and wind for AM
                char amRating = evaluateRide(dailyPrecip * 0.5, dailyWind * 0.7, dailyTemp, dailyWind * 0.5);
                weekAM[day] = amRating;
                
                // Evaluate PM ride
                char pmRating = evaluateRide(dailyPrecip * 0.5, dailyWind * 0.7, dailyTemp, dailyWind * 0.5);
                weekPM[day] = pmRating;
            }
        }
    }
    
    http.end();
}

// Get current weather accessor
WeatherData getCurrentWeather() {
    return currentWeather;
}

// Get today's ride rating
char getTodayRating() {
    return weekAM[0];
}

// Get tomorrow's ride rating
char getTomorrowRating() {
    return weekAM[1];
}

// Set display mode
void setDisplayMode(int mode) {
    displayMode = mode;
}

// Get display mode
int getDisplayMode() {
    return displayMode;
}

// Logging system implementation
void logMessage(const char* message) {
    uint8_t idx = logHead;
    if (logCount > 0) {
        idx = (logHead + MAX_LOG_ENTRIES - logCount) % MAX_LOG_ENTRIES;
    }
    snprintf(logBuffer[idx], 128, "[%lu] %s", millis(), message);
    
    if (logCount < MAX_LOG_ENTRIES) {
        logCount++;
    } else {
        logHead = (logHead + 1) % MAX_LOG_ENTRIES;
    }
}

void getLogs(char* output, size_t maxLen) {
    size_t pos = 0;
    output[0] = '\0';
    
    if (logCount == 0) {
        return;
    }
    
    for (uint8_t i = 0; i < logCount && pos < maxLen - 1; i++) {
        uint8_t idx = (logHead + MAX_LOG_ENTRIES - logCount + i) % MAX_LOG_ENTRIES;
        size_t len = strlen(logBuffer[idx]);
        if (pos + len + 2 < maxLen) {
            if (pos > 0) {
                output[pos++] = '\n';
            }
            strcpy(output + pos, logBuffer[idx]);
            pos += len;
        }
    }
}