// MotoWeather Bedside Display — Weather Data Implementation
// Open-Meteo API fetch (single request, filtered stream parse), ride decision logic

#include "weather.h"
#include "config.h"
#include <math.h>
#include <memory>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define FORECAST_DAYS        7
#define FORECAST_HOURS       (FORECAST_DAYS * 24)
#define MAX_WINDOW_HOURS     24
#define MIN_FREE_HEAP_BYTES      20000
#define MIN_FREE_HEAP_TLS_BYTES  32000   // a TLS session needs considerably more RAM
#define DEFAULT_FETCH_MS     900000UL

// Weekly state arrays
char weekAM[7] = { RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN };
char weekPM[7] = { RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN };
uint8_t weekStartDow = 0;

// Current weather data
static WeatherData currentWeather = { 0, 0, 0, 0, WEATHER_CLEAR, 'f' };

// Unix time (UTC) of local midnight of the first forecast day; 0 = no forecast yet
static time_t forecastDay0 = 0;

// Logging system - circular buffer of 16 entries
static char logBuffer[MAX_LOG_ENTRIES][128];
static uint8_t logHead = 0;
static uint8_t logCount = 0;

// Ride decision thresholds (overridden by config.json)
float maxRainMm = DEFAULT_MAX_RAIN_MM;
float maxWindKmh = DEFAULT_MAX_WIND_KMH;
float minTempC = DEFAULT_MIN_TEMP_C;
float warnWindKmh = DEFAULT_WARN_WIND_KMH;

// Ride windows (start hour, length in hours); overridden by config.json
RideWindow weekdayAM = { 7, 2 };
RideWindow weekdayPM = { 17, 2 };
RideWindow weekendAM = { 10, 3 };
RideWindow weekendPM = { 13, 4 };

// Sunrise/sunset (unix time, UTC) for day/night detection, and the location's UTC offset
time_t sunriseTime = 0;
time_t sunsetTime = 0;
long   utcOffsetSeconds = 0;

// Map an Open-Meteo WMO weather code (plus wind) to a display condition
static int mapCondition(int code, float windKmh) {
    int condition = WEATHER_CLEAR;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95) {
        condition = WEATHER_RAIN;      // drizzle, rain, showers, thunderstorm
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        condition = WEATHER_SNOW;      // snow, snow grains, snow showers
    }
    // Clear, cloudy (1-3) and fog (45, 48) fall through; show wind if it is strong
    if (condition == WEATHER_CLEAR && windKmh > warnWindKmh) {
        condition = WEATHER_WIND;
    }
    return condition;
}

// Read element i of a numeric JSON array, NAN when missing or null
static float valueAt(JsonArray arr, size_t i) {
    JsonVariant v = arr[i];
    return v.is<float>() ? v.as<float>() : NAN;
}

static bool isWeekend(uint8_t dow) {
    return dow == 0 || dow == 6;
}

// Rate one ride window: [startHour, startHour + hours) of forecast day `day`
static char rateWindow(JsonArray temps, JsonArray precips, JsonArray gusts, int day, RideWindow w) {
    float t[MAX_WINDOW_HOURS], p[MAX_WINDOW_HOURS], g[MAX_WINDOW_HOURS];
    size_t n = w.hours > MAX_WINDOW_HOURS ? MAX_WINDOW_HOURS : w.hours;
    size_t first = (size_t)day * 24 + w.startHour;
    for (size_t i = 0; i < n; i++) {
        t[i] = valueAt(temps, first + i);
        p[i] = valueAt(precips, first + i);
        g[i] = valueAt(gusts, first + i);
    }
    return evaluateWindow(t, p, g, n);
}

// Parse the combined response. Commits to the globals only when the data is usable.
static bool parseForecast(JsonDocument& doc) {
    JsonObject current = doc["current"];
    JsonObject daily = doc["daily"];
    JsonObject hourly = doc["hourly"];
    if (current.isNull() || !daily["time"][0].is<long>()) {
        return false;
    }

    // --- Current conditions ---
    WeatherData w = currentWeather;
    if (current["temperature_2m"].is<float>())   w.tempC = current["temperature_2m"].as<float>();
    if (current["precipitation"].is<float>())    w.precipMm = current["precipitation"].as<float>();
    if (current["wind_speed_10m"].is<float>())   w.windKmh = current["wind_speed_10m"].as<float>();
    if (current["wind_gusts_10m"].is<float>())   w.gustKmh = current["wind_gusts_10m"].as<float>();
    if (current["weather_code"].is<int>()) {
        w.condition = mapCondition(current["weather_code"].as<int>(), w.windKmh);
    } else {
        logMessage("Weather API: weather_code missing, keeping previous condition");
    }

    // --- Time base ---
    long offset = doc["utc_offset_seconds"] | utcOffsetSeconds;
    time_t day0 = daily["time"][0].as<long>();     // local midnight of day 0 (unix time)

    if (daily["sunrise"][0].is<long>() && daily["sunset"][0].is<long>()) {
        sunriseTime = daily["sunrise"][0].as<long>();
        sunsetTime = daily["sunset"][0].as<long>();
    }

    // --- Hourly forecast: trend and ride windows ---
    JsonArray temps = hourly["temperature_2m"];
    JsonArray precips = hourly["precipitation"];
    JsonArray gusts = hourly["wind_gusts_10m"];
    w.trend = 'f';

    if (temps.size() >= FORECAST_HOURS && precips.size() >= FORECAST_HOURS && gusts.size() >= FORECAST_HOURS) {
        // Hourly index 0 is local midnight of day 0
        time_t nowEpoch = state.timeSynced ? time(nullptr) : (time_t)(current["time"] | 0L);
        long nowIdx = nowEpoch > day0 ? (long)((nowEpoch - day0) / 3600) : 0;
        if (nowIdx + 3 < (long)temps.size()) {
            float now = valueAt(temps, nowIdx);
            float later = valueAt(temps, nowIdx + 3);
            if (!isnan(now) && !isnan(later)) {
                if (later - now >= 1.0f) w.trend = 'u';
                else if (now - later >= 1.0f) w.trend = 'd';
            }
        }

        uint8_t dow0 = (uint8_t)((((long)day0 + offset) / 86400 + 4) % 7);   // 1970-01-01 was a Thursday
        for (int d = 0; d < FORECAST_DAYS; d++) {
            bool weekend = isWeekend((dow0 + d) % 7);
            weekAM[d] = rateWindow(temps, precips, gusts, d, weekend ? weekendAM : weekdayAM);
            weekPM[d] = rateWindow(temps, precips, gusts, d, weekend ? weekendPM : weekdayPM);
        }
        weekStartDow = dow0;
    } else {
        logMessage("Weather API: hourly forecast incomplete, weekly ratings not updated");
    }

    currentWeather = w;
    utcOffsetSeconds = offset;
    forecastDay0 = day0;
    return true;
}

// One request for everything: current conditions, 7 days of hourly data, sunrise/sunset.
// The response is stream-parsed through an ArduinoJson filter (no String payload).
unsigned long fetchWeather(float lat, float lon) {
    // Transport: an API key is never sent in clear text. Without a key the public data is
    // fetched over plain HTTP to save RAM (an https:// URL is downgraded).
    String url = weatherApiUrl;
    bool hasKey = weatherApiKey.length() > 0;
    if (hasKey && url.startsWith("http://")) {
        url.replace(0, 7, "https://");
    } else if (!hasKey && url.startsWith("https://")) {
        url.replace(0, 8, "http://");
    }
    bool useTls = url.startsWith("https://");

    uint32_t minHeap = useTls ? MIN_FREE_HEAP_TLS_BYTES : MIN_FREE_HEAP_BYTES;
    if (ESP.getFreeHeap() < minHeap) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Weather fetch skipped, low heap: %u", ESP.getFreeHeap());
        logMessage(buf);
        return 0;
    }

    std::unique_ptr<WiFiClient> client;
    if (useTls) {
        // Encrypts the request, but does not verify the server certificate (no CA store on device)
        WiFiClientSecure* secure = new WiFiClientSecure();
        secure->setInsecure();
        secure->setBufferSizes(1024, 512);
        client.reset(secure);
    } else {
        client.reset(new WiFiClient());
    }
    HTTPClient http;

    url += "?latitude=";
    url += String(lat, 6);
    url += "&longitude=";
    url += String(lon, 6);
    url += "&current=temperature_2m,precipitation,wind_speed_10m,wind_gusts_10m,weather_code";
    url += "&hourly=temperature_2m,precipitation,wind_gusts_10m";
    url += "&daily=sunrise,sunset";
    url += "&forecast_days=7&timezone=auto&timeformat=unixtime";
    // Data is always requested in metric units (thresholds are metric); the display converts

    if (hasKey) {
        url += "&apikey=" + weatherApiKey;
    }

    if (weatherDebug) {
        logMessage("Starting weather fetch...");
    }

    // HTTP/1.0 avoids chunked transfer encoding so the body can be parsed straight off the stream
    http.useHTTP10(true);
    http.setTimeout(8000);
    const char* headerKeys[] = { "Cache-Control" };
    http.collectHeaders(headerKeys, 1);

    if (!http.begin(*client, url)) {
        logMessage("Weather API: begin() failed");
        return 0;
    }

    int httpCode = http.GET();
    unsigned long nextFetchMs = DEFAULT_FETCH_MS;
    bool ok = false;

    if (httpCode == HTTP_CODE_OK) {
        // Server-driven refresh interval, e.g. "public, max-age=900"
        String cacheControl = http.header("Cache-Control");
        int pos = cacheControl.indexOf("max-age=");
        if (pos >= 0) {
            long maxAgeSeconds = cacheControl.substring(pos + 8).toInt();
            if (maxAgeSeconds > 60) {
                nextFetchMs = maxAgeSeconds * 1000UL;
            }
        }

        // Keep only the fields we use
        JsonDocument filter;
        filter["utc_offset_seconds"] = true;
        filter["current"] = true;
        filter["daily"]["time"][0] = true;
        filter["daily"]["sunrise"][0] = true;
        filter["daily"]["sunset"][0] = true;
        filter["hourly"]["temperature_2m"][0] = true;
        filter["hourly"]["precipitation"][0] = true;
        filter["hourly"]["wind_gusts_10m"][0] = true;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        if (error) {
            char buf[96];
            snprintf(buf, sizeof(buf), "Weather API: JSON error %s", error.c_str());
            logMessage(buf);
        } else if (!parseForecast(doc)) {
            logMessage("Weather API: response missing required fields");
        } else {
            ok = true;
            if (weatherDebug) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Weather OK, free heap %u", ESP.getFreeHeap());
                logMessage(buf);
            }
        }
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "Weather API FAILED: HTTP %d - %s", httpCode, http.errorToString(httpCode).c_str());
        logMessage(buf);
    }

    http.end();
    return ok ? nextFetchMs : 0;
}

// Ride decision algorithm
char evaluateRide(float precipMm, float gustKmh, float tempC, float windKmh) {
    (void)windKmh;
    if (precipMm > maxRainMm || gustKmh > maxWindKmh) {
        return RIDE_DONT;
    }
    if (precipMm > 0 || tempC < minTempC || gustKmh > warnWindKmh) {
        return RIDE_CAUTION;
    }
    return RIDE_GOOD;
}

// Worst case over a window: total rain, strongest gust, coldest temperature
char evaluateWindow(const float* temp, const float* precip, const float* gust, size_t count) {
    float rain = 0, maxGust = 0, minTemp = 100;
    bool any = false;
    for (size_t i = 0; i < count; i++) {
        if (!isnan(precip[i])) { rain += precip[i]; any = true; }
        if (!isnan(gust[i]) && gust[i] > maxGust) { maxGust = gust[i]; any = true; }
        if (!isnan(temp[i]) && temp[i] < minTemp) { minTemp = temp[i]; any = true; }
    }
    if (!any) {
        return RIDE_UNKNOWN;
    }
    return evaluateRide(rain, maxGust, minTemp, 0);
}

// Hour of day (0-23) at the configured location; only meaningful when NTP has synced
int localHour() {
    time_t t = time(nullptr) + utcOffsetSeconds;
    return (int)((t / 3600) % 24);
}

bool forecastIsFromPastDay() {
    return state.timeSynced && forecastDay0 > 0 && time(nullptr) >= forecastDay0 + 86400;
}

// Get current weather accessor
WeatherData getCurrentWeather() {
    return currentWeather;
}

// Rating of the next ride window today: morning until it is over, then evening
char getTodayRating() {
    if (state.timeSynced) {
        RideWindow am = isWeekend(weekStartDow) ? weekendAM : weekdayAM;
        if (localHour() >= am.startHour + am.hours) {
            return weekPM[0];
        }
    }
    return weekAM[0];
}

// Tomorrow's morning ride
char getTomorrowRating() {
    return weekAM[1];
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
