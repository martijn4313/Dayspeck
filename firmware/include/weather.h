// MotoWeather Bedside Display — Weather Data Declarations
// Open-Meteo API fetch, ArduinoJson streaming filter, ride decision logic

#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>
#include <time.h>
#include "app_state.h"

#include "motologic.h"   // RIDE_* ratings, WEATHER_* conditions and the pure logic

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
size_t getLogCount();
const char* getLogEntry(size_t i);   // i = 0 is the oldest entry
void getLogs(char* output, size_t maxLen);

#endif // WEATHER_H
