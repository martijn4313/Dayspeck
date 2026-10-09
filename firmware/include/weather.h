// Dayspeck Bedside Display — Weather Data Declarations
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
    int   code;         // WMO weather code (kids variant picture)
};

// Ride decision thresholds (from config.json, defaults from config.h)
extern float maxRainMm;
extern float maxWindKmh;
extern float minTempC;
extern float warnWindKmh;
extern float rainProbPct;

// Kids variant: outfit limits (config.json "kids", defaults KIDS_* in config.h) and the wind picture
extern KidsLimits kidsLimits;
// Kids variant: dinner time (local hour), the sunset evening instead of dinner, and whether the night gets a
// column after dinner (config.json "kids")
extern int kidsDinnerHour;
extern bool kidsSunsetColumn;
extern bool kidsNightColumn;
extern bool kidsUmbrella;   // an umbrella with the outfit for a shower
// Kids variant: countdowns (config.json "kids"): two birthdays (month 0 = not set), the holidays that count
// down (KIDS_HOLIDAY mask) and from how many sleeps before the day
extern KidsBirthday kidsBirthdays[KIDS_MAX_BIRTHDAYS];
extern unsigned kidsHolidays;
extern int kidsCountdownDays;

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


// Ride score per day (0-100, weekend bonus may exceed 100; -1 = nothing rideable) and the best day
extern int16_t weekScore[7];
extern int8_t  weekBestDay;     // index into the week arrays, -1 = none

// Forecast hours from the current hour on. Returns how many are available; `first` points at the
// current hour and `firstEpoch` is its start (unix time, UTC).
size_t getUpcomingHours(const HourSlice*& first, time_t& firstEpoch);

// Best time to leave within the next 12 hours (a 2 hour ride, daytime only, never one rated "don't ride").
// LEAVE_AT sets `hourLocal`, the local start hour.
#define LEAVE_NONE     0   // no usable forecast (or no daytime window left)
#define LEAVE_NOW      1   // the best ride starts this hour
#define LEAVE_AT       2   // the best ride starts at hourLocal
#define LEAVE_NO_RIDE  3   // every daytime window is rated "don't ride"
int getBestLeave(int& hourLocal);

// Unix time of the last successful update, 0 if none (needs NTP, or the API's own clock)
extern time_t lastUpdateEpoch;

// Local time helpers (valid only when NTP has synced, see state.timeSynced)
int  localHour();
int  localHourOf(time_t t);
// True once a forecast has told us the location's UTC offset (needed to show local time)
bool timezoneKnown();   // local hour (0-23) of a unix time
// True when the forecast was fetched on a previous local day (week arrays are shifted)
bool forecastIsFromPastDay();

// Demo mode: while set, the accessors below and getUpcomingHours() give these made-up values instead of the
// real weather (which keeps being fetched as usual). nullptr = the real weather again.
struct WeatherDemo {
    WeatherData      current;
    char             rating;        // the ride rating, today and tomorrow
    const HourSlice* hours;         // from the current hour on
    size_t           count;
    time_t           firstEpoch;    // start of hours[0]
};
void setWeatherDemo(const WeatherDemo* demo);

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
