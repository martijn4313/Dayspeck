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
    uint8_t code = 255;    // WMO weather code, 255 = unknown
};

// Start index of the best `windowLen`-hour window with start in [from, to), by score. Windows that
// contain an invalid hour are skipped; -1 when there is none. Ties go to the earliest start.
int bestStartHour(const HourSlice* hours, size_t count, size_t windowLen, size_t from, size_t to);

// Kids variant: which weather picture to show (day or night is chosen when drawing)
#define KIDS_WEATHER_CLEAR   0
#define KIDS_WEATHER_PARTLY  1
#define KIDS_WEATHER_CLOUDY  2
#define KIDS_WEATHER_RAIN    3
#define KIDS_WEATHER_STORM   4
#define KIDS_WEATHER_SNOW    5
#define KIDS_WEATHER_WIND    6

// From an Open-Meteo WMO weather code. Strong wind (above `warnWindKmh`) only replaces dry weather.
int kidsWeatherFor(int code, float windKmh, float warnWindKmh);

// SSD1306 contrast for a brightness percentage (clamped to 1-100). Never 0: on some panels
// contrast 0 is completely dark.
uint8_t contrastForPercent(int percent);

// Kids variant: outfits. HOT..FREEZING run from warm to cold; RAIN sits outside that order.
#define OUTFIT_HOT       0   // sun cap, t-shirt and shorts
#define OUTFIT_WARM      1   // t-shirt and shorts
#define OUTFIT_MILD      2   // t-shirt
#define OUTFIT_COOL      3   // sweater
#define OUTFIT_RAIN      4   // rain coat and boots
#define OUTFIT_COLD      5   // winter coat and hat
#define OUTFIT_FREEZING  6   // winter coat, hat, scarf and mittens

struct KidsLimits {
    float hotFromC;       // sun cap from here on, when it is sunny and daytime
    float shortsFromC;    // shorts from here on
    float sweaterBelowC;  // sweater below this
    float coatBelowC;     // winter coat below this
    float freezeBelowC;   // scarf and mittens below this
    float windyGustKmh;   // gusts above this show the wind picture (dry weather only)
};

// Outfit for a temperature and a KIDS_WEATHER_* picture. Snow means the full winter outfit,
// rain or a storm the rain coat (unless it is cold enough for the winter coat). NAN = mild.
int outfitFor(float tempC, int kidsWeather, bool night, const KidsLimits& l);

// Warmth step of a temperature: 0 = hot ... 5 = freezing (the outfit order without rain)
int warmthStep(float tempC, const KidsLimits& l);

// One forecast hour for the kids outlook. code = WMO weather code, -1 = unknown.
struct KidsHour {
    float tempC;
    float rainMm;
    float gustKmh;
    int   code;
    bool  night;
    bool  valid;
};

// Weather picture of one hour (without a code: rain from the amount, otherwise clear)
int kidsHourWeather(const KidsHour& h, const KidsLimits& l);

// What the kids screens show for a stretch of hours
struct KidsOutlook {
    bool valid;
    int  outfit;     // OUTFIT_*
    int  weather;    // KIDS_WEATHER_*
    bool night;      // draw the moon instead of the sun
    int  tempC;      // average temperature, rounded
    int  hour;       // index of the hour it is about: the middle of a window, or the hour of an event
};

// Summary of hours [from, to): the most severe precipitation if there is any (at least 0.2 mm in an
// hour, or a storm), otherwise the most common sky; outfit from the average temperature.
KidsOutlook kidsWindowOutlook(const KidsHour* hours, size_t from, size_t to, const KidsLimits& l);

// "Later" for the kids screens. hours[0] is the first hour after now. Normally the summary of the
// first `window` hours; but when an hour in [window, lookahead) deviates strongly from that summary
// (heavy rain of 1 mm or more, a storm or snow while the window is dry, or a temperature two warmth
// steps away), that hour instead, so the screen can warn "rain this afternoon".
KidsOutlook kidsLaterOutlook(const KidsHour* hours, size_t count, size_t window, size_t lookahead,
                             const KidsLimits& l);

// Time of day of a local hour, for the symbol between the halves
#define KIDS_TIME_MORNING    0   // 06-11
#define KIDS_TIME_AFTERNOON  1   // 12-17
#define KIDS_TIME_EVENING    2   // 18-21
#define KIDS_TIME_NIGHT      3   // 22-05
#define KIDS_TIME_TOMORROW   4   // after sleeping (drawn as a bed)
int timeOfDay(int localHour);

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

// Parse a "major.minor.patch" version (an optional leading 'v' is allowed, each part 0-65535).
// Anything else, including a suffix such as "-rc1", is rejected.
bool parseVersion(const char* text, uint16_t out[3]);

// True when `candidate` is a strictly newer version than `current`; false if either is invalid
bool isNewerVersion(const char* candidate, const char* current);

// Decode exactly `len` bytes from `2 * len` hex digits (either case) and nothing more
bool hexToBytes(const char* hex, uint8_t* out, size_t len);

#endif // MOTOLOGIC_H
