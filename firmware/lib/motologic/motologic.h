// Dayspeck — pure logic (no Arduino dependencies, unit tested natively with `pio test -e native`)

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
#define OUTFIT_COLD      5   // winter coat and scarf
#define OUTFIT_FREEZING  6   // winter coat, hat, scarf and mittens

struct KidsLimits {
    float hotFromC;       // sun cap from here on, when it is sunny and daytime
    float shortsFromC;    // shorts from here on
    float sweaterBelowC;  // sweater below this
    float coatBelowC;     // winter coat below this
    float freezeBelowC;   // hat and mittens below this
    float windyGustKmh;   // gusts above this show the wind picture (dry weather only)
};

// The outfit limits must go from warm to cold: freezeBelowC <= coatBelowC <= sweaterBelowC <= shortsFromC <=
// hotFromC (an equal pair skips that step), and the wind picture needs a positive gust speed.
bool kidsLimitsValid(const KidsLimits& l);

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
    int   light;   // KIDS_LIGHT_* at the start of the hour (the current hour: now)
};

// How light it is at a moment: dusk is the hour before sunset up to half an hour after it
#define KIDS_LIGHT_DAY   0
#define KIDS_LIGHT_DUSK  1
#define KIDS_LIGHT_DARK  2
int lightAt(long t, long sunrise, long sunset);

// Weather picture of one hour (without a code: rain from the amount, otherwise clear)
int kidsHourWeather(const KidsHour& h, const KidsLimits& l);

// What the kids screens show for a stretch of hours
struct KidsOutlook {
    bool valid;
    int  outfit;     // OUTFIT_*
    int  weather;    // KIDS_WEATHER_*
    bool night;      // draw the moon instead of the sun
    int  tempC;      // average temperature, rounded (kidsPartOutlook: the part's own temperature)
    int  maxTempC;   // highest temperature, rounded
    int  hour;       // index of the middle hour
    int  light;      // KIDS_LIGHT_*: sun, setting sun or moon for a clear sky
};

// Summary of hours [from, to): the most severe precipitation if there is any (at least 0.2 mm in an
// hour, or a storm), otherwise the most common sky; outfit from the average temperature.
KidsOutlook kidsWindowOutlook(const KidsHour* hours, size_t from, size_t to, const KidsLimits& l);

// Parts of the day on the kids screens, by local hour: morning 07-12, afternoon 12 to dinner time, dinner
// until 22, and the night (22-07), when the kids sleep. Instead of dinner there can be the sunset evening
// (18-22, dinnerHour KIDS_SUNSET_EVENING): a setting sun as its symbol, and the moon when most of it is dark.
#define KIDS_PART_MORNING    0
#define KIDS_PART_AFTERNOON  1
#define KIDS_PART_DINNER     2
#define KIDS_PART_NIGHT      3
#define KIDS_PART_EVENING    4
#define KIDS_SUNSET_EVENING  0   // dinnerHour for the sunset evening instead of dinner
#define KIDS_MORNING_FROM_HR    7
#define KIDS_AFTERNOON_FROM_HR 12
#define KIDS_EVENING_FROM_HR   18   // the weather report's evening
#define KIDS_EVENING_UNTIL_HR  22   // bedtime: the night starts
#define KIDS_DINNER_MIN_HR     15   // the range of the dinner time setting
#define KIDS_DINNER_MAX_HR     21
int partOfDay(int localHour, int dinnerHour);   // KIDS_PART_*

// The symbol a column shows for its part: an hour after dinner time the current dinner column becomes the
// evening (setting sun), dinner is over. Its outlook keeps the dinner rules (the conditions of now).
#define KIDS_DINNER_LENGTH_HR  1
int kidsShownPart(int part, bool now, int localHourNow, int dinnerHour);

// One column of the kids screens: a part of the day and the forecast hours [from, to) that belong to it
struct KidsPart {
    int    part;         // KIDS_PART_*
    size_t from, to;
    bool   afterSleep;   // a night lies between the previous column (or now) and this one
    bool   now;          // the current hour belongs to it
};

// Split forecast hours into the next parts of the day. localHours[i] is the local hour of forecast
// hour i, and hour 0 is the current one. Fills at most maxParts parts, the current part first (only
// its remaining hours); returns how many. The night is skipped (the next part is then afterSleep),
// unless nightColumn is set and it is not the last column: tomorrow morning always stays in view.
size_t kidsDayParts(const int* localHours, size_t count, KidsPart* out, size_t maxParts, int dinnerHour,
                    bool nightColumn);

// One column of the kids screens: the weather picture of the part (as kidsWindowOutlook), and one
// characteristic temperature that is both the number on the weather screen and what the outfit goes by:
// the morning its lowest (the walk to school), the afternoon its highest, dinner its first hour (dinner
// time; the sunset evening its first hour too) and the night its lowest. For the current part only its remaining hours count. tempC holds that
// number. Dinner takes its light from that same hour, the night always has the moon.
KidsOutlook kidsPartOutlook(const KidsHour* hours, const KidsPart& part, const KidsLimits& l);

// The village: what to wear right now. The outfit goes by the current temperature (tempC, the number the
// village shows) and the weather of the current hour and the next ones (KIDS_NOW_HOURS in all), so rain on
// its way already gives the rain coat. Not valid without the current hour.
#define KIDS_NOW_HOURS 3
KidsOutlook kidsNowOutlook(const KidsHour* hours, size_t count, const KidsLimits& l);

// Kids variant: countdowns to a birthday or a holiday, counted in sleeps (nights until the day)
#define KIDS_EVENT_BIRTHDAY     0
#define KIDS_EVENT_HALLOWEEN    1   // 31 October
#define KIDS_EVENT_SINTERKLAAS  2   // 5 December (pakjesavond)
#define KIDS_EVENT_CHRISTMAS    3   // 25 December
#define KIDS_HOLIDAY(kind)      (1u << (kind))   // bit of a holiday in the `holidays` mask
#define KIDS_ALL_HOLIDAYS       (KIDS_HOLIDAY(KIDS_EVENT_HALLOWEEN) | KIDS_HOLIDAY(KIDS_EVENT_SINTERKLAAS) | \
                                 KIDS_HOLIDAY(KIDS_EVENT_CHRISTMAS))
#define KIDS_COUNTDOWN_DAYS     14  // default: a countdown shows from this many sleeps before the day
#define KIDS_MAX_COUNTDOWN_DAYS 60
#define KIDS_MAX_BIRTHDAYS      2

struct KidsBirthday {
    int  year, month, day;   // date of birth; month 0 = not set
    char initial;            // letter drawn on the cake ('A'-'Z'), 0 = none
};

// What the countdown screen shows
struct KidsCountdown {
    bool active;     // false: nothing within range
    int  kind;       // KIDS_EVENT_*
    int  sleeps;     // nights until the day, 0 = it is today
    int  age;        // birthday: the age on that day
    char initial;    // birthday: its letter, 0 = none
};

// The letter for the cake from a name or initial: its first character as a capital A-Z, otherwise 0
char kidsInitial(const char* text);

// Days since 1970-01-01 of a date in the proleptic Gregorian calendar (month 1-12)
long daysFromCivil(int year, int month, int day);

// "YYYY-MM-DD" (a valid date, year 1900-2100) and nothing more
bool parseIsoDate(const char* text, int& year, int& month, int& day);

// Days from today (year, month, day) to the next `month`/`day`, today included (0). A 29 February
// falls on 28 February in other years. occurrenceYear gets the year it falls in.
int daysUntilNext(int year, int month, int day, int onMonth, int onDay, int& occurrenceYear);

// The nearest birthday or enabled holiday (KIDS_HOLIDAY mask) within `withinDays` sleeps from today; a
// birthday wins a tie. Birthdays that are not set, or lie in the future, are skipped.
KidsCountdown nextKidsCountdown(int year, int month, int day, const KidsBirthday* birthdays, size_t count,
                                unsigned holidays, int withinDays);

// Map an Open-Meteo WMO weather code (plus wind) to a display condition
int mapWeatherCode(int code, float windKmh, float warnWindKmh);

// Temperature trend 'u' (rising), 'd' (falling) or 'f' (flat) from now to a few hours later; NAN = flat
char temperatureTrend(float now, float later);

// Weather report: the day in 1-5 short sentences, by fixed rules (no network service). It is about the rest
// of today until 22:00, or from 18:00 on about the evening, the night and tomorrow 07-22 (from 22:00 the night
// and the coming day). Sentences: the evening and night (their rain and lowest temperature), the sky, rain and
// temperature of the day (a fresh morning gets its own sentence), a change during the day (a shower around an
// hour, rain from or until an hour), and one thing to watch out for (frost, strong gusts, a lot of rain, wind).
#define REPORT_LANG_EN        0
#define REPORT_LANG_NL        1
#define REPORT_MAX_SENTENCES  5
#define REPORT_SENTENCE_LEN   80
#define REPORT_DEGREE         '\xF8'   // the degree sign in the display font (code page 437)

// One forecast hour for the report
struct ReportHour {
    int   hour;       // local hour 0-23
    float tempC;
    float rainMm;
    float gustKmh;
    int   prob;       // chance of precipitation %, -1 = unknown
    int   code;       // WMO weather code, -1 = unknown
    bool  valid;
};

struct WeatherReport {
    uint8_t count;    // 0: not enough forecast hours
    char    sentences[REPORT_MAX_SENTENCES][REPORT_SENTENCE_LEN];
    uint8_t priority[REPORT_MAX_SENTENCES];   // which to leave out first when they do not fit: the lowest
};

// hours[0] is the current hour, the next ones follow it hour by hour
void weatherReport(const ReportHour* hours, size_t count, int lang, WeatherReport& out);

// Word wrap the sentences in `keep` (bit 1 << i for sentence i) to lines of REPORT_COLS characters, every
// sentence on a new line. Fills at most maxLines lines; returns how many the text needs (which may be more).
#define REPORT_COLS 21
size_t wrapReport(const WeatherReport& r, unsigned keep, char (*lines)[REPORT_COLS + 1], size_t maxLines);

// Word wrap a report to at most maxLines lines: while it does not fit, the sentence with the lowest priority
// (the last of equals) is left out. Returns the number of lines.
size_t fitReport(const WeatherReport& r, char (*lines)[REPORT_COLS + 1], size_t maxLines);

// Screens. Every screen has a fixed name (config.json, web UI); which ones are used, and in which order, is
// set by two slot lists: the tap list (a tap steps through it; slot 0 is the home screen) and the hold list
// (a long press steps through it, after its last slot back home).
#define SCREEN_RIDE        0   // ride rating of the default day (today; tomorrow after previewHr)
#define SCREEN_RIDE_OTHER  1   // ride rating of the other day
#define SCREEN_WEEK        2   // 7-day AM/PM grid
#define SCREEN_HOURS       3   // next hours
#define SCREEN_CLOCK       4   // clock (needs the time)
#define SCREEN_WEATHER     5   // kids: the weather in three parts of the day
#define SCREEN_CLOTHES     6   // kids: what to wear in three parts of the day
#define SCREEN_COUNTDOWN   7   // kids: sleeps to a birthday or holiday (only while one is near)
#define SCREEN_REPORT      8   // a short weather report in words, for the parents
#define SCREEN_VILLAGE     9   // kids home screen: what to wear now, next to the animated village
#define SCREEN_COUNT      10
#define MAX_SCREEN_SLOTS   6

const char* screenName(int id);            // "ride", "rideOther", "week", ...; "" for an unknown id
int  screenFromName(const char* name);     // the id, or -1 for an unknown name
bool screenAlwaysAvailable(int id);        // false for the clock and the countdown: they can be skipped

struct ScreenList {
    uint8_t ids[MAX_SCREEN_SLOTS];
    uint8_t count;
};

// The tap list needs at least one screen and must start with one that is always available (the home
// screen); the hold list may be empty. All ids must be known.
bool screenListsValid(const ScreenList& tap, const ScreenList& hold);

// Where the display is: a slot in the tap list (list 0) or in the hold list (list 1)
struct ScreenNav {
    uint8_t list;
    uint8_t slot;
};
// available: bit (1 << id) set for every screen that can be shown right now
int       screenAt(ScreenNav nav, const ScreenList& tap, const ScreenList& hold);
ScreenNav screenNextTap(ScreenNav nav, const ScreenList& tap, uint16_t available);    // also the auto cycle
ScreenNav screenNextHold(ScreenNav nav, const ScreenList& tap, const ScreenList& hold, uint16_t available);

// Autumn in the hemisphere of the location: September-November in the north, March-May in the south.
// month is 1-12.
bool isAutumn(int month, bool southern);

// From this gust speed (km/h) leaves blow in autumn, even on a day that is not "windy"
#define LEAF_MIN_WIND_KMH 20

// Whether leaves blow along with the wind: autumn, a clear or windy sky (no rain or snow, which keep
// the leaves down) and a gust speed of at least LEAF_MIN_WIND_KMH. condition is WEATHER_*, NAN wind = no.
bool leavesBlowing(bool autumn, int condition, float windKmh);

// Kids variant: leaves blow in autumn in dry weather (a KIDS_WEATHER_* picture of clear, partly cloudy,
// cloudy or windy sky: not rain, thunderstorm or snow) from a gust speed of LEAF_MIN_WIND_KMH on.
bool kidsLeavesBlowing(bool autumn, int kidsWeather, float windKmh);

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
