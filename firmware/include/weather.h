// MotoWeather Bedside Display — Weather Data Declarations
// Open-Meteo API fetch, ArduinoJson streaming filter, ride decision logic

#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>
#include <time.h>
#include "app_state.h"

// Weather conditions
#define WEATHER_CLEAR   0
#define WEATHER_RAIN    1
#define WEATHER_SNOW    2
#define WEATHER_WIND    3

// Ride ratings
#define RIDE_GOOD       'G'
#define RIDE_CAUTION    '!'
#define RIDE_DONT       'X'
#define RIDE_UNKNOWN    '?'

// Weekly state arrays (index 0 = today)
extern char weekAM[7];
extern char weekPM[7];
// Day of week of weekAM[0]/weekPM[0] (0 = Sunday ... 6 = Saturday)
extern uint8_t weekStartDow;

// Current weather data
struct WeatherData {
    float tempC;        // Temperature in Celsius
    float windKmh;      // Wind speed in km/h
    float gustKmh;      // Wind gusts in km/h
    float precipMm;     // Precipitation in mm
    int   condition;    // WEATHER_CLEAR, WEATHER_RAIN, WEATHER_SNOW, WEATHER_WIND
    char  trend;        // 'u' (up), 'd' (down), 'f' (flat)
};

// Ride decision thresholds (from config.json, defaults from config.h)
extern float maxRainMm;
extern float maxWindKmh;
extern float minTempC;
extern float warnWindKmh;

// Ride windows (from config.json): weekday / weekend, morning / evening
extern RideWindow weekdayAM, weekdayPM, weekendAM, weekendPM;

// Sunrise/sunset (unix time, UTC) of the first forecast day, and the location's UTC offset
extern time_t sunriseTime;
extern time_t sunsetTime;
extern long   utcOffsetSeconds;

// API functions
// Fetches current weather and the 7-day forecast in a single request.
// Returns the recommended delay until the next fetch in ms, or 0 on failure.
unsigned long fetchWeather(float lat, float lon);
char evaluateRide(float precipMm, float gustKmh, float tempC, float windKmh);

// Rate a ride window from its hourly values (worst case over the window).
// Missing hours are NAN. Returns RIDE_UNKNOWN if there is no data at all.
char evaluateWindow(const float* temp, const float* precip, const float* gust, size_t count);

// Local time helpers (valid only when NTP has synced, see state.timeSynced)
int  localHour();
// True when the forecast was fetched on a previous local day (week arrays are shifted)
bool forecastIsFromPastDay();

// Accessors
WeatherData getCurrentWeather();
char getTodayRating();     // next upcoming ride window today
char getTomorrowRating();  // morning window tomorrow

// Logging system
#define MAX_LOG_ENTRIES 16
void logMessage(const char* message);
void getLogs(char* output, size_t maxLen);

#endif // WEATHER_H
