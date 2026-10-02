// MotoWeather Bedside Display — Weather Data Implementation
// Open-Meteo API fetch (single request, filtered stream parse), ride decision logic

#include "weather.h"
#include "config.h"
#include <math.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define FORECAST_DAYS        7
#define FORECAST_HOURS       (FORECAST_DAYS * 24)
#define MAX_WINDOW_HOURS     24
#define MIN_FREE_HEAP_BYTES  20000
#define DEFAULT_FETCH_MS     900000UL
#define WIND_SYMBOL_KMH      25     // sustained wind from which the wind symbol replaces clear weather (matches the display effect)

// Weekly state arrays
char weekAM[7] = { RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN };
char weekPM[7] = { RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN, RIDE_UNKNOWN };
uint8_t weekStartDow = 0;
int16_t weekScore[7] = { -1, -1, -1, -1, -1, -1, -1 };
int8_t  weekBestDay = -1;

// Compact copy of the next hours (from the hour of the last fetch), for the hourly view and leave advice
#define HOURLY_KEEP 24
static HourSlice hourSlices[HOURLY_KEEP];
static size_t hourlyCount = 0;
static time_t hourlyStartEpoch = 0;     // start of hourly[0]

time_t lastUpdateEpoch = 0;
static time_t apiEpochAtFetch = 0;      // the API's clock at the last fetch (valid without NTP)
static unsigned long fetchMillis = 0;

// Current weather data
static WeatherData currentWeather = { 0, 0, 0, 0, WEATHER_CLEAR, 'f', 0 };

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
float rainProbPct = DEFAULT_RAIN_PROB_PCT;

// Ride windows (start hour, length in hours); overridden by config.json
RideWindow weekdayAM = { 7, 2 };
RideWindow weekdayPM = { 17, 2 };
RideWindow weekendAM = { 10, 3 };
RideWindow weekendPM = { 13, 4 };

// Sunrise/sunset (unix time, UTC) for day/night detection, and the location's UTC offset
time_t sunriseTime = 0;
time_t sunsetTime = 0;
long   utcOffsetSeconds = 0;

// Thresholds as the pure logic wants them
static RideThresholds thresholds() {
    return RideThresholds{ maxRainMm, maxWindKmh, minTempC, warnWindKmh, rainProbPct };
}

// Read element i of a numeric JSON array, NAN when missing or null
static float valueAt(JsonArray arr, size_t i) {
    JsonVariant v = arr[i];
    return v.is<float>() ? v.as<float>() : NAN;
}

static bool isWeekend(uint8_t dow) {
    return dow == 0 || dow == 6;
}

struct WindowResult {
    char rating;
    int  score;     // -1 when the window cannot be ridden or has no data
};

// Rate one ride window: [startHour, startHour + hours) of forecast day `day`
static WindowResult rateJsonWindow(JsonArray temps, JsonArray precips, JsonArray gusts, JsonArray probs,
                                   int day, RideWindow w) {
    float t[MAX_WINDOW_HOURS], p[MAX_WINDOW_HOURS], g[MAX_WINDOW_HOURS], pr[MAX_WINDOW_HOURS];
    size_t n = w.hours > MAX_WINDOW_HOURS ? MAX_WINDOW_HOURS : w.hours;
    size_t first = (size_t)day * 24 + w.startHour;
    for (size_t i = 0; i < n; i++) {
        t[i] = valueAt(temps, first + i);
        p[i] = valueAt(precips, first + i);
        g[i] = valueAt(gusts, first + i);
        pr[i] = valueAt(probs, first + i);
    }
    WindowResult r;
    r.rating = rateWindow(thresholds(), t, p, g, pr, n);
    r.score = (r.rating == RIDE_DONT || r.rating == RIDE_UNKNOWN) ? -1 : scoreWindowHours(t, p, g, n);
    return r;
}

// Parse the combined response. Commits to the globals only when the data is usable.
static bool parseForecast(JsonDocument& doc) {
    JsonObject current = doc["current"];
    JsonObject daily = doc["daily"];
    JsonObject hourlyJson = doc["hourly"];
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
        w.code = current["weather_code"].as<int>();
        w.condition = mapWeatherCode(w.code, w.windKmh, WIND_SYMBOL_KMH);
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
    JsonArray temps = hourlyJson["temperature_2m"];
    JsonArray precips = hourlyJson["precipitation"];
    JsonArray gusts = hourlyJson["wind_gusts_10m"];
    JsonArray probs = hourlyJson["precipitation_probability"];
    JsonArray codes = hourlyJson["weather_code"];
    w.trend = 'f';

    if (temps.size() >= FORECAST_HOURS && precips.size() >= FORECAST_HOURS && gusts.size() >= FORECAST_HOURS) {
        // Hourly index 0 is local midnight of day 0
        time_t nowEpoch = state.timeSynced ? time(nullptr) : (time_t)(current["time"] | 0L);
        long nowIdx = nowEpoch > day0 ? (long)((nowEpoch - day0) / 3600) : 0;
        if (nowIdx + 3 < (long)temps.size()) {
            w.trend = temperatureTrend(valueAt(temps, nowIdx), valueAt(temps, nowIdx + 3));
        }

        uint8_t dow0 = dayOfWeek((long)day0, offset);
        int scores[FORECAST_DAYS];
        for (int d = 0; d < FORECAST_DAYS; d++) {
            bool weekend = isWeekend((dow0 + d) % 7);
            WindowResult am = rateJsonWindow(temps, precips, gusts, probs, d, weekend ? weekendAM : weekdayAM);
            WindowResult pm = rateJsonWindow(temps, precips, gusts, probs, d, weekend ? weekendPM : weekdayPM);
            weekAM[d] = am.rating;
            weekPM[d] = pm.rating;
            scores[d] = scoreDay(am.score, pm.score, weekend);
            weekScore[d] = scores[d];
        }
        weekBestDay = bestDay(scores, FORECAST_DAYS);
        weekStartDow = dow0;

        // Keep the next 24 hours in compact form for the hourly view and the leave advice
        long startIdx = nowIdx < 0 ? 0 : nowIdx;
        size_t keep = 0;
        for (; keep < HOURLY_KEEP && (size_t)(startIdx + keep) < temps.size(); keep++) {
            float t = valueAt(temps, startIdx + keep);
            float p = valueAt(precips, startIdx + keep);
            float g = valueAt(gusts, startIdx + keep);
            float pr = valueAt(probs, startIdx + keep);
            HourSlice& h = hourSlices[keep];
            h.valid = !isnan(t) || !isnan(g);
            h.tempC = isnan(t) ? 0 : (int8_t)constrain((int)lroundf(t), -127, 127);
            h.rainTenthMm = isnan(p) ? 0 : (uint8_t)constrain((int)lroundf(p * 10.0f), 0, 255);
            h.gustKmh = isnan(g) ? 0 : (uint8_t)constrain((int)lroundf(g), 0, 254);
            h.rainProb = isnan(pr) ? 255 : (uint8_t)constrain((int)lroundf(pr), 0, 100);
            h.code = codes[startIdx + keep].is<int>() ? (uint8_t)constrain(codes[startIdx + keep].as<int>(), 0, 254) : 255;
        }
        hourlyCount = keep;
        hourlyStartEpoch = day0 + (time_t)startIdx * 3600;
        apiEpochAtFetch = nowEpoch;
        fetchMillis = millis();
        lastUpdateEpoch = nowEpoch;
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
    // Plain HTTP only: the ESP8266 is too slow and too short on RAM for TLS, and the public
    // Open-Meteo data is not secret. An https:// URL from the config is downgraded.
    String url = weatherApiUrl;
    if (url.startsWith("https://")) {
        url = "http://" + url.substring(8);
    }

    if (ESP.getFreeHeap() < MIN_FREE_HEAP_BYTES) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Weather fetch skipped, low heap: %u (max block %u)", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
        logMessage(buf);
        return 0;
    }

    WiFiClient client;
    HTTPClient http;

    url += "?latitude=";
    url += String(lat, 6);
    url += "&longitude=";
    url += String(lon, 6);
    url += "&current=temperature_2m,precipitation,wind_speed_10m,wind_gusts_10m,weather_code";
    url += "&hourly=temperature_2m,precipitation,precipitation_probability,wind_gusts_10m";
#ifdef KIDS_MODE
    url += ",weather_code";   // the kids "later" pictures; the rider build saves the RAM
#endif
    url += "&daily=sunrise,sunset";
    url += "&forecast_days=7&timezone=auto&timeformat=unixtime";
    // Data is always requested in metric units (thresholds are metric); the display converts

    if (weatherDebug) {
        logMessage("Starting weather fetch...");
    }

    // HTTP/1.0 avoids chunked transfer encoding so the body can be parsed straight off the stream
    http.useHTTP10(true);
    http.setTimeout(8000);
    const char* headerKeys[] = { "Cache-Control" };
    http.collectHeaders(headerKeys, 1);

    if (!http.begin(client, url)) {
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
        filter["hourly"]["precipitation_probability"][0] = true;
#ifdef KIDS_MODE
        filter["hourly"]["weather_code"][0] = true;
#endif

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
                snprintf(buf, sizeof(buf), "Weather OK, free heap %u (max block %u)", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
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

// Hour of day (0-23) at the configured location; only meaningful when NTP has synced
int localHour() {
    time_t t = time(nullptr) + utcOffsetSeconds;
    return (int)((t / 3600) % 24);
}

bool forecastIsFromPastDay() {
    return state.timeSynced && forecastDay0 > 0 && time(nullptr) >= forecastDay0 + 86400;
}

bool timezoneKnown() {
    return forecastDay0 > 0;
}

int localHourOf(time_t t) {
    return (int)(((t + utcOffsetSeconds) / 3600) % 24);
}

// Best estimate of the current unix time: NTP when synced, else the API's clock plus elapsed time
static time_t nowEstimate() {
    if (state.timeSynced) return time(nullptr);
    return apiEpochAtFetch + (time_t)((millis() - fetchMillis) / 1000UL);
}

size_t getUpcomingHours(const HourSlice*& first, time_t& firstEpoch) {
    long offset = 0;
    if (hourlyCount > 0) {
        time_t now = nowEstimate();
        offset = now > hourlyStartEpoch ? (long)((now - hourlyStartEpoch) / 3600) : 0;
        if (offset > (long)hourlyCount) offset = hourlyCount;
    }
    first = hourSlices + offset;
    firstEpoch = hourlyStartEpoch + (time_t)offset * 3600;
    return hourlyCount - offset;
}

bool getBestLeave(int& hourLocal, bool& startNow) {
    const HourSlice* first;
    time_t firstEpoch;
    size_t n = getUpcomingHours(first, firstEpoch);
    const size_t rideHours = 2;
    if (n < rideHours) return false;

    // Only daytime starts (06:00-20:00 local): hide the hours outside by marking them invalid
    HourSlice local[HOURLY_KEEP];
    for (size_t i = 0; i < n; i++) {
        local[i] = first[i];
        int h = localHourOf(firstEpoch + (time_t)i * 3600);
        if (h < 6 || h + (int)rideHours > 22) local[i].valid = false;
    }
    int best = bestStartHour(local, n, rideHours, 0, n < 12 ? n : 12);
    if (best < 0) return false;
    hourLocal = localHourOf(firstEpoch + (time_t)best * 3600);
    startNow = (best == 0);
    return true;
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

size_t getLogCount() {
    return logCount;
}

// i = 0 is the oldest entry
const char* getLogEntry(size_t i) {
    return logBuffer[(logHead + MAX_LOG_ENTRIES - logCount + i) % MAX_LOG_ENTRIES];
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
