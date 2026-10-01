// MotoWeather — pure logic (no Arduino dependencies, unit tested natively with `pio test -e native`)

#ifndef MOTOLOGIC_H
#define MOTOLOGIC_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Ride ratings
#define RIDE_GOOD       'G'
#define RIDE_CAUTION    '!'
#define RIDE_DONT       'X'
#define RIDE_UNKNOWN    '?'

// Display conditions
#define WEATHER_CLEAR   0
#define WEATHER_RAIN    1
#define WEATHER_SNOW    2
#define WEATHER_WIND    3

struct RideThresholds {
    float maxRainMm;
    float maxWindKmh;
    float minTempC;
    float warnWindKmh;
    float rainProbPct;   // chance of rain (%) from which a ride is at least "caution"; >100 disables
};

// Rate a ride from rain (mm), strongest gust (km/h), temperature (C) and chance of rain (%, NAN = unknown)
char rateRide(const RideThresholds& t, float precipMm, float gustKmh, float tempC, float rainProbPct = NAN);

// Rate a ride window from hourly values (worst case over the window).
// Missing hours are NAN; `prob` may be a null pointer. Returns RIDE_UNKNOWN when there is no data at all.
char rateWindow(const RideThresholds& t, const float* temp, const float* precip, const float* gust,
                const float* prob, size_t count);

// Heuristic ride score of a window, 0..100 (higher is better): 100 points minus
// 3 per degree away from 20 C, 20 per mm of rain and 2 per km/h of gusts above 20 km/h.
int scoreWindow(float avgTempC, float rainMm, float maxGustKmh);

// Same, from hourly values (missing hours are NAN); -1 when there is no data at all
int scoreWindowHours(const float* temp, const float* precip, const float* gust, size_t count);

// Score of a day: the better of its two windows (windows rated "don't ride" or unknown, passed as -1,
// do not count), plus a weekend bonus. -1 when neither window can be ridden. May exceed 100.
int scoreDay(int amScore, int pmScore, bool weekend);

// Index of the best day (highest score, earliest on ties); -1 when no day has a score >= 0
int bestDay(const int* scores, size_t count);

// One forecast hour in compact form (4 bytes of data)
struct HourSlice {
    int8_t  tempC;
    uint8_t rainTenthMm;   // precipitation in 0.1 mm, saturating
    uint8_t gustKmh;       // saturating
    uint8_t rainProb;      // percent, 255 = unknown
    bool    valid;
};

// Start index of the best `windowLen`-hour window with start in [from, to), by score. Windows that
// contain an invalid hour are skipped; -1 when there is none. Ties go to the earliest start.
int bestStartHour(const HourSlice* hours, size_t count, size_t windowLen, size_t from, size_t to);

// Kids variant: what to wear. Warm = t-shirt and shorts, mild = t-shirt, cool = sweater.
#define CLOTHES_WARM    0
#define CLOTHES_MILD    1
#define CLOTHES_COOL    2

// `shortsFromC`: from this temperature on shorts are fine; below `sweaterBelowC` a sweater is needed.
// An unknown temperature (NAN) gives the middle option.
int clothingFor(float tempC, float shortsFromC, float sweaterBelowC);

// Map an Open-Meteo WMO weather code (plus wind) to a display condition
int mapWeatherCode(int code, float windKmh, float warnWindKmh);

// Temperature trend 'u' (rising), 'd' (falling) or 'f' (flat) from now to a few hours later; NAN = flat
char temperatureTrend(float now, float later);

// True when `now` is before sunrise or after sunset. Sunrise/sunset are for some earlier or
// current day and are rolled forward by whole days. All values are unix time (UTC).
bool isNightAt(long now, long sunrise, long sunset);

// Day of week (0 = Sunday) of the local date that starts at `localMidnightUtc` in a zone with
// the given UTC offset (seconds)
uint8_t dayOfWeek(long localMidnightUtc, long utcOffsetSeconds);

// Rollover-safe "has `interval` ms passed since `last`" for a 32-bit millis() style counter
bool intervalElapsed(uint32_t now, uint32_t last, uint32_t interval);

#endif // MOTOLOGIC_H
