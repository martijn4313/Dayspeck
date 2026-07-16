// MotoWeather Bedside Display — Weather Data Declarations
// Open-Meteo API fetch, ArduinoJson streaming filter, ride decision logic

#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>

// Weather conditions
#define WEATHER_CLEAR   0
#define WEATHER_RAIN    1
#define WEATHER_SNOW    2
#define WEATHER_WIND    3

// Ride ratings
#define RIDE_GOOD       'G'
#define RIDE_CAUTION    '!'
#define RIDE_DONT       'X'

// Weekly state arrays
extern char weekAM[7];
extern char weekPM[7];

// Current weather data
struct WeatherData {
    // Current (Real-time view)
    float tempC;        // Temperature in Celsius
    float windKmh;      // Wind speed in km/h
    float gustKmh;      // Wind gusts in km/h
    float precipMm;     // Precipitation in mm
    int   condition;    // WEATHER_CLEAR, WEATHER_RAIN, WEATHER_SNOW, WEATHER_WIND
    char  trend;        // 'u' (up), 'd' (down), 'f' (flat)

    // Hourly (Next 12 hours - Commute view)
    float hourlyTemp[12], hourlyWindGusts[12];
    int hourlyRainProb[12];

    // Daily (7 Days - AI Planning view)
    float dailyTempMax[7], dailyTempMin[7];
    float dailyPrecip[7], dailyWindMax[7], dailyGustMax[7];
};

// Ride decision thresholds (from config or JSON)
extern float maxRainMm;
extern float maxWindKmh;
extern float minTempC;
extern float warnWindKmh;

// Sunrise/sunset times for day/night detection
extern time_t sunriseTime;
extern time_t sunsetTime;

// API functions
unsigned long fetchWeather(float lat, float lon);
char evaluateRide(float precipMm, float gustKmh, float tempC, float windKmh);
int getBestRideDay();
void updateWeeklyState(float lat, float lon);
void loadThresholds();

// Accessors
WeatherData getCurrentWeather();
char getTodayRating();
char getTomorrowRating();
void setDisplayMode(int mode);  // 0 = primary, 1 = weekly matrix
int getDisplayMode();

// Logging system
#define MAX_LOG_ENTRIES 16
void logMessage(const char* message);
void getLogs(char* output, size_t maxLen);

#endif // WEATHER_H
