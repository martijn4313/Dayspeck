// MotoWeather — pure logic (no Arduino dependencies, unit tested natively with `pio test -e native`)

#ifndef MOTOLOGIC_H
#define MOTOLOGIC_H

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
};

// Rate a ride from rain (mm), strongest gust (km/h) and temperature (C)
char rateRide(const RideThresholds& t, float precipMm, float gustKmh, float tempC);

// Rate a ride window from hourly values (worst case over the window).
// Missing hours are NAN; returns RIDE_UNKNOWN when there is no data at all.
char rateWindow(const RideThresholds& t, const float* temp, const float* precip, const float* gust, size_t count);

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
