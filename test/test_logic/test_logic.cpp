// Native unit tests for the pure logic: pio test -e native
#include <unity.h>
#include <math.h>
#include "motologic.h"

static const RideThresholds T = { 2.0f, 60.0f, 5.0f, 40.0f, 50.0f };   // the config.h defaults

void setUp() {}
void tearDown() {}

void test_rate_ride_good() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateRide(T, 0, 10, 15));
}

void test_rate_ride_caution() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 0.5f, 10, 15));   // light rain
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 0, 10, 3));       // cold
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 0, 45, 15));      // gusty
}

void test_rate_ride_dont() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_DONT, rateRide(T, 3, 10, 15));         // heavy rain
    TEST_ASSERT_EQUAL_CHAR(RIDE_DONT, rateRide(T, 0, 65, 15));         // storm gusts
}

void test_rate_ride_thresholds_are_exclusive() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 2.0f, 60.0f, 15));   // exactly at the maximum
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateRide(T, 0, 40.0f, 5.0f));       // exactly at the warnings
}

void test_rate_window_uses_worst_hour() {
    float temp[3] = { 12, 4, 12 };
    float rain[3] = { 0, 0, 0 };
    float gust[3] = { 10, 10, 10 };
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateWindow(T, temp, rain, gust, nullptr, 3));   // one cold hour
}

void test_rate_window_sums_rain() {
    float temp[2] = { 12, 12 };
    float rain[2] = { 1.5f, 1.5f };   // 3 mm in total
    float gust[2] = { 10, 10 };
    TEST_ASSERT_EQUAL_CHAR(RIDE_DONT, rateWindow(T, temp, rain, gust, nullptr, 2));
}

void test_rate_window_missing_hours() {
    float nan = NAN;
    float temp[2] = { nan, 12 };
    float rain[2] = { nan, 0 };
    float gust[2] = { nan, 10 };
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateWindow(T, temp, rain, gust, nullptr, 2));

    float allNan[2] = { nan, nan };
    TEST_ASSERT_EQUAL_CHAR(RIDE_UNKNOWN, rateWindow(T, allNan, allNan, allNan, nullptr, 2));
    TEST_ASSERT_EQUAL_CHAR(RIDE_UNKNOWN, rateWindow(T, allNan, allNan, allNan, nullptr, 0));
}

void test_rain_probability_makes_it_caution() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateRide(T, 0, 10, 15, 30));
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 0, 10, 15, 50));    // at the threshold
    TEST_ASSERT_EQUAL_CHAR(RIDE_DONT, rateRide(T, 3, 10, 15, 90));       // mm still wins
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateRide(T, 0, 10, 15, NAN));      // unknown probability is ignored
    RideThresholds off = T;
    off.rainProbPct = 101;                                               // disabled
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateRide(off, 0, 10, 15, 100));
}

void test_rate_window_uses_the_highest_probability() {
    float temp[3] = { 15, 15, 15 }, rain[3] = { 0, 0, 0 }, gust[3] = { 10, 10, 10 };
    float prob[3] = { 10, 70, 20 };
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateWindow(T, temp, rain, gust, prob, 3));
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateWindow(T, temp, rain, gust, nullptr, 3));
}

void test_score_window() {
    TEST_ASSERT_EQUAL(100, scoreWindow(20, 0, 10));          // ideal
    TEST_ASSERT_EQUAL(91, scoreWindow(23, 0, 10));           // -3 per degree
    TEST_ASSERT_EQUAL(91, scoreWindow(17, 0, 10));           // either side of 20
    TEST_ASSERT_EQUAL(80, scoreWindow(20, 1.0f, 10));        // -20 per mm
    TEST_ASSERT_EQUAL(80, scoreWindow(20, 0, 30));           // -2 per km/h above 20
    TEST_ASSERT_EQUAL(0, scoreWindow(-10, 5, 80));           // clamped
    TEST_ASSERT_EQUAL(100, scoreWindow(20, 0, 20));          // exactly 20 km/h costs nothing
}

void test_score_window_hours() {
    float nan = NAN;
    float temp[3] = { 18, 22, nan }, rain[3] = { 0.5f, 0.5f, nan }, gust[3] = { 25, 35, nan };
    // avg 20 C, 1.0 mm, max gust 35 -> 100 - 20 - 30 = 50
    TEST_ASSERT_EQUAL(50, scoreWindowHours(temp, rain, gust, 3));
    float none[2] = { nan, nan };
    TEST_ASSERT_EQUAL(-1, scoreWindowHours(none, none, none, 2));
}

void test_score_day_and_best_day() {
    TEST_ASSERT_EQUAL(90, scoreDay(90, 60, false));
    TEST_ASSERT_EQUAL(105, scoreDay(60, 90, true));          // weekend bonus, may exceed 100
    TEST_ASSERT_EQUAL(70, scoreDay(-1, 70, false));          // one window can be ridden
    TEST_ASSERT_EQUAL(-1, scoreDay(-1, -1, true));           // nothing to ride, no bonus
    int scores[5] = { 80, -1, 95, 95, 40 };
    TEST_ASSERT_EQUAL(2, bestDay(scores, 5));                // ties go to the earliest
    int none[2] = { -1, -1 };
    TEST_ASSERT_EQUAL(-1, bestDay(none, 2));
}

void test_best_start_hour() {
    HourSlice h[8];
    for (int i = 0; i < 8; i++) h[i] = HourSlice{ 20, 0, 10, 0, true };
    h[2].rainTenthMm = 20;    // rain in hours 2 and 3
    h[3].rainTenthMm = 20;
    TEST_ASSERT_EQUAL(0, bestStartHour(h, 8, 2, 0, 7));      // dry window, earliest wins
    TEST_ASSERT_EQUAL(4, bestStartHour(h, 8, 2, 2, 7));      // from hour 2 the rain is avoided at 4
    TEST_ASSERT_EQUAL(-1, bestStartHour(h, 8, 9, 0, 7));     // window longer than the data
    h[0].valid = false;                                      // windows containing a missing hour are skipped...
    h[4].rainTenthMm = 50;                                   // ...and rain at 4 and 5 now beats the dry hour 1
    h[5].rainTenthMm = 50;
    TEST_ASSERT_EQUAL(6, bestStartHour(h, 8, 2, 0, 7));      // window 0-1 is skipped, 1-2 and 2-3 are wet, 6-7 is dry
    for (int i = 0; i < 8; i++) h[i].valid = false;
    TEST_ASSERT_EQUAL(-1, bestStartHour(h, 8, 2, 0, 7));
}

void test_weather_codes() {
    TEST_ASSERT_EQUAL(WEATHER_CLEAR, mapWeatherCode(0, 5, 25));
    TEST_ASSERT_EQUAL(WEATHER_CLEAR, mapWeatherCode(3, 5, 25));    // overcast
    TEST_ASSERT_EQUAL(WEATHER_CLEAR, mapWeatherCode(45, 5, 25));   // fog
    TEST_ASSERT_EQUAL(WEATHER_RAIN, mapWeatherCode(51, 5, 25));    // drizzle
    TEST_ASSERT_EQUAL(WEATHER_RAIN, mapWeatherCode(65, 5, 25));
    TEST_ASSERT_EQUAL(WEATHER_RAIN, mapWeatherCode(81, 5, 25));    // showers
    TEST_ASSERT_EQUAL(WEATHER_RAIN, mapWeatherCode(95, 5, 25));    // thunderstorm
    TEST_ASSERT_EQUAL(WEATHER_SNOW, mapWeatherCode(73, 5, 25));
    TEST_ASSERT_EQUAL(WEATHER_SNOW, mapWeatherCode(85, 5, 25));    // snow showers
}

void test_wind_overrides_only_dry_weather() {
    TEST_ASSERT_EQUAL(WEATHER_WIND, mapWeatherCode(0, 30, 25));
    TEST_ASSERT_EQUAL(WEATHER_WIND, mapWeatherCode(2, 30, 25));
    TEST_ASSERT_EQUAL(WEATHER_RAIN, mapWeatherCode(61, 30, 25));
}

void test_autumn_follows_the_hemisphere() {
    TEST_ASSERT_FALSE(isAutumn(8, false));
    TEST_ASSERT_TRUE(isAutumn(9, false));
    TEST_ASSERT_TRUE(isAutumn(11, false));
    TEST_ASSERT_FALSE(isAutumn(12, false));
    TEST_ASSERT_FALSE(isAutumn(4, false));
    // south: the seasons are swapped
    TEST_ASSERT_FALSE(isAutumn(2, true));
    TEST_ASSERT_TRUE(isAutumn(3, true));
    TEST_ASSERT_TRUE(isAutumn(5, true));
    TEST_ASSERT_FALSE(isAutumn(6, true));
    TEST_ASSERT_FALSE(isAutumn(10, true));
}

void test_leaves_blow_in_autumn_wind_only() {
    TEST_ASSERT_TRUE(leavesBlowing(true, WEATHER_CLEAR, 20.0f));       // from 20 km/h on, windy day or not
    TEST_ASSERT_TRUE(leavesBlowing(true, WEATHER_WIND, 55.0f));
    TEST_ASSERT_FALSE(leavesBlowing(true, WEATHER_CLEAR, 19.9f));
    TEST_ASSERT_FALSE(leavesBlowing(false, WEATHER_WIND, 55.0f));      // not autumn
    TEST_ASSERT_FALSE(leavesBlowing(true, WEATHER_RAIN, 55.0f));       // rain and snow keep them down
    TEST_ASSERT_FALSE(leavesBlowing(true, WEATHER_SNOW, 55.0f));
    TEST_ASSERT_FALSE(leavesBlowing(true, WEATHER_CLEAR, NAN));
}

void test_kids_leaves_blow_in_dry_autumn_wind_only() {
    TEST_ASSERT_TRUE(kidsLeavesBlowing(true, KIDS_WEATHER_CLEAR, 20.0f));
    TEST_ASSERT_TRUE(kidsLeavesBlowing(true, KIDS_WEATHER_PARTLY, 30.0f));
    TEST_ASSERT_TRUE(kidsLeavesBlowing(true, KIDS_WEATHER_CLOUDY, 30.0f));
    TEST_ASSERT_TRUE(kidsLeavesBlowing(true, KIDS_WEATHER_WIND, 60.0f));
    TEST_ASSERT_FALSE(kidsLeavesBlowing(true, KIDS_WEATHER_CLEAR, 19.9f));
    TEST_ASSERT_FALSE(kidsLeavesBlowing(false, KIDS_WEATHER_WIND, 60.0f));     // not autumn
    TEST_ASSERT_FALSE(kidsLeavesBlowing(true, KIDS_WEATHER_RAIN, 60.0f));      // rain, storm and snow keep them down
    TEST_ASSERT_FALSE(kidsLeavesBlowing(true, KIDS_WEATHER_STORM, 60.0f));
    TEST_ASSERT_FALSE(kidsLeavesBlowing(true, KIDS_WEATHER_SNOW, 60.0f));
    TEST_ASSERT_FALSE(kidsLeavesBlowing(true, KIDS_WEATHER_CLEAR, NAN));
}

void test_trend() {
    TEST_ASSERT_EQUAL_CHAR('u', temperatureTrend(10, 12));
    TEST_ASSERT_EQUAL_CHAR('d', temperatureTrend(12, 10));
    TEST_ASSERT_EQUAL_CHAR('f', temperatureTrend(10, 10.5f));
    TEST_ASSERT_EQUAL_CHAR('f', temperatureTrend(NAN, 10));
}

void test_night_detection() {
    const long rise = 1000000, set = rise + 12 * 3600;   // 12 hours of daylight
    TEST_ASSERT_FALSE(isNightAt(rise + 3600, rise, set));        // morning
    TEST_ASSERT_TRUE(isNightAt(set + 3600, rise, set));          // evening
    TEST_ASSERT_TRUE(isNightAt(rise - 3600, rise, set));         // before the first sunrise
}

void test_night_detection_rolls_forward_by_days() {
    const long rise = 1000000, set = rise + 12 * 3600;
    long nextDayNoon = rise + 86400 + 6 * 3600;
    TEST_ASSERT_FALSE(isNightAt(nextDayNoon, rise, set));        // stale sunrise data, next day
    TEST_ASSERT_TRUE(isNightAt(nextDayNoon + 8 * 3600, rise, set));
    long threeDaysLater = rise + 3 * 86400 + 3600;
    TEST_ASSERT_FALSE(isNightAt(threeDaysLater, rise, set));
}

void test_day_of_week() {
    // 2026-10-01 is a Thursday. Local midnight in UTC+2 is 2026-09-30 22:00 UTC.
    const long utc_midnight_20261001 = 1790812800;
    TEST_ASSERT_EQUAL_UINT8(4, dayOfWeek(utc_midnight_20261001, 0));
    TEST_ASSERT_EQUAL_UINT8(4, dayOfWeek(utc_midnight_20261001 - 7200, 7200));
    TEST_ASSERT_EQUAL_UINT8(0, dayOfWeek(utc_midnight_20261001 + 3 * 86400, 0));   // Sunday
    TEST_ASSERT_EQUAL_UINT8(0, dayOfWeek(0 - 4 * 86400, 0));                       // 1969-12-28
}

void test_interval_elapsed() {
    TEST_ASSERT_FALSE(intervalElapsed(1000, 900, 200));
    TEST_ASSERT_TRUE(intervalElapsed(1100, 900, 200));
    // millis() wrapped around between `last` and `now`
    uint32_t last = 0xFFFFFF00UL;   // 32-bit like millis() on the ESP8266
    TEST_ASSERT_FALSE(intervalElapsed(0x10, last, 0x200));
    TEST_ASSERT_TRUE(intervalElapsed(0x200, last, 0x200));
}

static const KidsLimits K = { 25, 20, 15, 5, 0, 40 };

void test_outfit() {
    TEST_ASSERT_EQUAL(OUTFIT_HOT, outfitFor(27, KIDS_WEATHER_CLEAR, false, K));
    TEST_ASSERT_EQUAL(OUTFIT_WARM, outfitFor(27, KIDS_WEATHER_CLEAR, true, K));     // no sun cap at night
    TEST_ASSERT_EQUAL(OUTFIT_WARM, outfitFor(27, KIDS_WEATHER_CLOUDY, false, K));   // nor when cloudy
    TEST_ASSERT_EQUAL(OUTFIT_WARM, outfitFor(20, KIDS_WEATHER_CLEAR, false, K));    // shorts from the limit on
    TEST_ASSERT_EQUAL(OUTFIT_MILD, outfitFor(19.9f, KIDS_WEATHER_CLEAR, false, K));
    TEST_ASSERT_EQUAL(OUTFIT_MILD, outfitFor(15, KIDS_WEATHER_CLEAR, false, K));
    TEST_ASSERT_EQUAL(OUTFIT_COOL, outfitFor(14.9f, KIDS_WEATHER_CLEAR, false, K));
    TEST_ASSERT_EQUAL(OUTFIT_RAIN, outfitFor(12, KIDS_WEATHER_RAIN, false, K));
    TEST_ASSERT_EQUAL(OUTFIT_RAIN, outfitFor(22, KIDS_WEATHER_STORM, false, K));
    TEST_ASSERT_EQUAL(OUTFIT_COLD, outfitFor(4, KIDS_WEATHER_RAIN, false, K));      // the coat beats the rain coat
    TEST_ASSERT_EQUAL(OUTFIT_FREEZING, outfitFor(-1, KIDS_WEATHER_CLEAR, false, K));
    TEST_ASSERT_EQUAL(OUTFIT_FREEZING, outfitFor(2, KIDS_WEATHER_SNOW, false, K));
    TEST_ASSERT_EQUAL(OUTFIT_MILD, outfitFor(NAN, KIDS_WEATHER_CLEAR, false, K));
}

static KidsHour hr(float t, float rain, int code) { return KidsHour{ t, rain, 10, code, false, true }; }

void test_kids_limits_must_go_from_warm_to_cold() {
    TEST_ASSERT_TRUE(kidsLimitsValid(K));
    KidsLimits l = K;
    l.shortsFromC = l.hotFromC + 1;                      // shorts only above the sun cap limit: wrong order
    TEST_ASSERT_FALSE(kidsLimitsValid(l));
    l = K;
    l.coatBelowC = l.sweaterBelowC + 1;                  // winter coat before the sweater
    TEST_ASSERT_FALSE(kidsLimitsValid(l));
    l = K;
    l.freezeBelowC = l.coatBelowC + 0.5f;
    TEST_ASSERT_FALSE(kidsLimitsValid(l));
    l = K;
    l.sweaterBelowC = l.shortsFromC;                     // equal limits are fine: that step is skipped
    TEST_ASSERT_TRUE(kidsLimitsValid(l));
    l = K;
    l.windyGustKmh = 0;
    TEST_ASSERT_FALSE(kidsLimitsValid(l));
    l = K;
    l.hotFromC = NAN;
    TEST_ASSERT_FALSE(kidsLimitsValid(l));
}

void test_kids_outfits_follow_changed_limits() {
    TEST_ASSERT_EQUAL(OUTFIT_MILD, outfitFor(17, KIDS_WEATHER_CLEAR, false, K));     // default: t-shirt at 17
    KidsLimits l = K;
    l.shortsFromC = 16;                                                               // a warm-blooded child
    TEST_ASSERT_EQUAL(OUTFIT_WARM, outfitFor(17, KIDS_WEATHER_CLEAR, false, l));
    l = K;
    l.shortsFromC = 22;
    TEST_ASSERT_EQUAL(OUTFIT_MILD, outfitFor(21, KIDS_WEATHER_CLEAR, false, l));
    l = K;
    l.coatBelowC = 8;                                                                 // winter coat from 8 degrees down
    TEST_ASSERT_EQUAL(OUTFIT_COLD, outfitFor(7, KIDS_WEATHER_CLEAR, false, l));
    TEST_ASSERT_EQUAL(OUTFIT_COOL, outfitFor(8, KIDS_WEATHER_CLEAR, false, l));
    TEST_ASSERT_EQUAL(OUTFIT_COOL, outfitFor(8, KIDS_WEATHER_CLEAR, false, K));       // default: still a sweater at 8
    l = K;
    l.windyGustKmh = 30;
    TEST_ASSERT_EQUAL(KIDS_WEATHER_WIND, kidsWeatherFor(2, 35, l.windyGustKmh));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_PARTLY, kidsWeatherFor(2, 35, K.windyGustKmh));
}

void test_kids_window() {
    KidsHour h[4] = { hr(16, 0, 1), hr(18, 0, 1), hr(18, 0, 3), hr(20, 0, 1) };
    KidsOutlook o = kidsWindowOutlook(h, 0, 4, K);
    TEST_ASSERT_TRUE(o.valid);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLEAR, o.weather);   // most common sky
    TEST_ASSERT_EQUAL(18, o.tempC);
    TEST_ASSERT_EQUAL(OUTFIT_MILD, o.outfit);
    TEST_ASSERT_EQUAL(1, o.hour);

    h[2] = hr(18, 0.5f, 61);                             // one wet hour makes it a rainy window
    o = kidsWindowOutlook(h, 0, 4, K);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_RAIN, o.weather);
    TEST_ASSERT_EQUAL(OUTFIT_RAIN, o.outfit);

    h[2] = hr(18, 0.1f, 61);                             // a trace is only a cloud
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLEAR, kidsWindowOutlook(h, 0, 4, K).weather);

    KidsHour none[2] = { hr(1, 0, 0), hr(1, 0, 0) };
    none[0].valid = none[1].valid = false;
    TEST_ASSERT_FALSE(kidsWindowOutlook(none, 0, 2, K).valid);
}

void test_kids_window_highest_temperature() {
    KidsHour h[3] = { hr(9, 0, 1), hr(14, 0, 1), hr(16, 0, 1) };
    KidsOutlook o = kidsWindowOutlook(h, 0, 3, K);
    TEST_ASSERT_EQUAL(13, o.tempC);      // the average decides the outfit
    TEST_ASSERT_EQUAL(16, o.maxTempC);   // the highest is the number to show
    TEST_ASSERT_EQUAL(OUTFIT_COOL, o.outfit);
}

void test_part_of_day() {
    TEST_ASSERT_EQUAL(-1, partOfDay(6));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, partOfDay(7));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, partOfDay(11));
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, partOfDay(12));
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, partOfDay(17));
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, partOfDay(18));
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, partOfDay(21));
    TEST_ASSERT_EQUAL(-1, partOfDay(22));
    TEST_ASSERT_EQUAL(-1, partOfDay(0));
}

// Local hours of `count` forecast hours from `first` on
static void hoursFrom(int first, int* out, size_t count) {
    for (size_t i = 0; i < count; i++) out[i] = (first + (int)i) % 24;
}

void test_day_parts_in_the_morning() {
    int h[24];
    hoursFrom(10, h, 24);                       // 10:00
    KidsPart p[3];
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[0].part);
    TEST_ASSERT_EQUAL(0, p[0].from);            // only what is left of the morning: 10 and 11
    TEST_ASSERT_EQUAL(2, p[0].to);
    TEST_ASSERT_TRUE(p[0].now);
    TEST_ASSERT_FALSE(p[0].afterSleep);
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, p[1].part);
    TEST_ASSERT_EQUAL(6, p[1].to - p[1].from);
    TEST_ASSERT_FALSE(p[1].now);
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, p[2].part);
    TEST_ASSERT_FALSE(p[2].afterSleep);
}

void test_day_parts_roll_into_tomorrow() {
    int h[24];
    hoursFrom(19, h, 24);                       // 19:00: evening, then tomorrow morning and afternoon
    KidsPart p[3];
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3));
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, p[0].part);
    TEST_ASSERT_TRUE(p[0].now);
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[1].part);
    TEST_ASSERT_TRUE(p[1].afterSleep);          // the night lies before it
    TEST_ASSERT_EQUAL(12, p[1].from);           // 07:00 is 12 hours after 19:00
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, p[2].part);
    TEST_ASSERT_FALSE(p[2].afterSleep);
    TEST_ASSERT_EQUAL(23, p[2].to);             // the data ends at 18:00 tomorrow: 12:00-17:00 (5 hours) left
}

void test_day_parts_at_night() {
    int h[24];
    hoursFrom(23, h, 24);                       // 23:00: nothing is "now"; the morning is after sleeping
    KidsPart p[3];
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[0].part);
    TEST_ASSERT_FALSE(p[0].now);
    TEST_ASSERT_TRUE(p[0].afterSleep);
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, p[2].part);

    TEST_ASSERT_EQUAL(0, kidsDayParts(h, 3, p, 3));   // only night hours: no parts
}

void test_kids_weather() {
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLEAR, kidsWeatherFor(0, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLEAR, kidsWeatherFor(1, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_PARTLY, kidsWeatherFor(2, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLOUDY, kidsWeatherFor(3, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLOUDY, kidsWeatherFor(45, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_RAIN, kidsWeatherFor(61, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_RAIN, kidsWeatherFor(81, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_SNOW, kidsWeatherFor(73, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_STORM, kidsWeatherFor(95, 5, 40));
    TEST_ASSERT_EQUAL(KIDS_WEATHER_WIND, kidsWeatherFor(1, 50, 40));    // wind replaces dry weather
    TEST_ASSERT_EQUAL(KIDS_WEATHER_RAIN, kidsWeatherFor(61, 50, 40));   // but not rain
}

void test_parse_version() {
    uint16_t v[3];
    TEST_ASSERT_TRUE(parseVersion("0.2.0", v));
    TEST_ASSERT_EQUAL(0, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
    TEST_ASSERT_EQUAL(0, v[2]);
    TEST_ASSERT_TRUE(parseVersion("v12.34.56", v));
    TEST_ASSERT_EQUAL(56, v[2]);
    TEST_ASSERT_FALSE(parseVersion("", v));
    TEST_ASSERT_FALSE(parseVersion(nullptr, v));
    TEST_ASSERT_FALSE(parseVersion("1.2", v));
    TEST_ASSERT_FALSE(parseVersion("1.2.3.4", v));
    TEST_ASSERT_FALSE(parseVersion("1.2.3-rc1", v));
    TEST_ASSERT_FALSE(parseVersion("1..3", v));
    TEST_ASSERT_FALSE(parseVersion("1.2.x", v));
    TEST_ASSERT_FALSE(parseVersion("1.2.65536", v));
    TEST_ASSERT_FALSE(parseVersion(" 1.2.3", v));
}

void test_newer_version() {
    TEST_ASSERT_TRUE(isNewerVersion("0.3.0", "0.2.0"));
    TEST_ASSERT_TRUE(isNewerVersion("0.10.0", "0.9.9"));   // numeric, not string order
    TEST_ASSERT_TRUE(isNewerVersion("1.0.0", "0.99.99"));
    TEST_ASSERT_TRUE(isNewerVersion("v0.2.1", "0.2.0"));
    TEST_ASSERT_FALSE(isNewerVersion("0.2.0", "0.2.0"));
    TEST_ASSERT_FALSE(isNewerVersion("0.1.9", "0.2.0"));    // never a downgrade
    TEST_ASSERT_FALSE(isNewerVersion("garbage", "0.2.0"));
    TEST_ASSERT_FALSE(isNewerVersion("9.9.9", "garbage"));
}

void test_hex_to_bytes() {
    uint8_t b[3];
    TEST_ASSERT_TRUE(hexToBytes("00aFff", b, 3));
    TEST_ASSERT_EQUAL_HEX8(0x00, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0xaf, b[1]);
    TEST_ASSERT_EQUAL_HEX8(0xff, b[2]);
    TEST_ASSERT_FALSE(hexToBytes("00af", b, 3));      // too short
    TEST_ASSERT_FALSE(hexToBytes("00afff00", b, 3));  // too long
    TEST_ASSERT_FALSE(hexToBytes("00agff", b, 3));    // not hex
    TEST_ASSERT_FALSE(hexToBytes(nullptr, b, 3));
}

void test_kids_initial() {
    TEST_ASSERT_EQUAL_CHAR('E', kidsInitial("E"));
    TEST_ASSERT_EQUAL_CHAR('E', kidsInitial("emma"));
    TEST_ASSERT_EQUAL_CHAR(0, kidsInitial(""));
    TEST_ASSERT_EQUAL_CHAR(0, kidsInitial("3"));
    TEST_ASSERT_EQUAL_CHAR(0, kidsInitial("\xc3\x89"));   // not A-Z: no letter rather than a wrong one
    TEST_ASSERT_EQUAL_CHAR(0, kidsInitial(nullptr));
}

void test_days_from_civil() {
    TEST_ASSERT_EQUAL_INT32(0, daysFromCivil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT32(-1, daysFromCivil(1969, 12, 31));
    TEST_ASSERT_EQUAL_INT32(11017, daysFromCivil(2000, 3, 1));
    TEST_ASSERT_EQUAL_INT32(1, daysFromCivil(2024, 3, 1) - daysFromCivil(2024, 2, 29));
}

void test_parse_iso_date() {
    int y = 0, m = 0, d = 0;
    TEST_ASSERT_TRUE(parseIsoDate("2021-05-14", y, m, d));
    TEST_ASSERT_EQUAL_INT(2021, y);
    TEST_ASSERT_EQUAL_INT(5, m);
    TEST_ASSERT_EQUAL_INT(14, d);
    TEST_ASSERT_TRUE(parseIsoDate("2020-02-29", y, m, d));
    TEST_ASSERT_FALSE(parseIsoDate("2021-02-29", y, m, d));    // not a leap year
    TEST_ASSERT_FALSE(parseIsoDate("2021-13-01", y, m, d));
    TEST_ASSERT_FALSE(parseIsoDate("2021-04-31", y, m, d));
    TEST_ASSERT_FALSE(parseIsoDate("2021-5-14", y, m, d));
    TEST_ASSERT_FALSE(parseIsoDate("2021-05-14x", y, m, d));
    TEST_ASSERT_FALSE(parseIsoDate("", y, m, d));
    TEST_ASSERT_FALSE(parseIsoDate(nullptr, y, m, d));
}

void test_days_until_next() {
    int when = 0;
    TEST_ASSERT_EQUAL_INT(28, daysUntilNext(2026, 10, 3, 10, 31, when));    // Halloween
    TEST_ASSERT_EQUAL_INT(2026, when);
    TEST_ASSERT_EQUAL_INT(0, daysUntilNext(2026, 10, 31, 10, 31, when));    // today
    TEST_ASSERT_EQUAL_INT(364, daysUntilNext(2026, 11, 1, 10, 31, when));   // just past: next year
    TEST_ASSERT_EQUAL_INT(2027, when);
    TEST_ASSERT_EQUAL_INT(7, daysUntilNext(2026, 12, 29, 1, 5, when));      // over the new year
    TEST_ASSERT_EQUAL_INT(2027, when);
    TEST_ASSERT_EQUAL_INT(1, daysUntilNext(2027, 2, 27, 2, 29, when));      // 29 Feb on the 28th
    TEST_ASSERT_EQUAL_INT(2, daysUntilNext(2028, 2, 27, 2, 29, when));      // a leap year
}

void test_countdown_picks_the_nearest_event() {
    KidsBirthday b[2] = { { 2021, 11, 2, 'E' }, { 2019, 12, 20, 0 } };
    // 3 October: nothing within 14 sleeps (Halloween is 28 away)
    TEST_ASSERT_FALSE(nextKidsCountdown(2026, 10, 3, b, 2, KIDS_ALL_HOLIDAYS, 14).active);
    // 20 October: Halloween in 11 sleeps
    KidsCountdown c = nextKidsCountdown(2026, 10, 20, b, 2, KIDS_ALL_HOLIDAYS, 14);
    TEST_ASSERT_TRUE(c.active);
    TEST_ASSERT_EQUAL_INT(KIDS_EVENT_HALLOWEEN, c.kind);
    TEST_ASSERT_EQUAL_INT(11, c.sleeps);
    // 1 November: the first birthday tomorrow, 5 years old
    c = nextKidsCountdown(2026, 11, 1, b, 2, KIDS_ALL_HOLIDAYS, 14);
    TEST_ASSERT_EQUAL_INT(KIDS_EVENT_BIRTHDAY, c.kind);
    TEST_ASSERT_EQUAL_INT(1, c.sleeps);
    TEST_ASSERT_EQUAL_INT(5, c.age);
    TEST_ASSERT_EQUAL_CHAR('E', c.initial);
    // 1 December: Sinterklaas; 10 December: the second birthday (7) before Christmas
    c = nextKidsCountdown(2026, 12, 1, b, 2, KIDS_ALL_HOLIDAYS, 14);
    TEST_ASSERT_EQUAL_INT(KIDS_EVENT_SINTERKLAAS, c.kind);
    TEST_ASSERT_EQUAL_INT(4, c.sleeps);
    c = nextKidsCountdown(2026, 12, 10, b, 2, KIDS_ALL_HOLIDAYS, 14);
    TEST_ASSERT_EQUAL_INT(KIDS_EVENT_BIRTHDAY, c.kind);
    TEST_ASSERT_EQUAL_INT(7, c.age);
    // 21 December: Christmas, and on the day itself 0 sleeps
    c = nextKidsCountdown(2026, 12, 21, b, 2, KIDS_ALL_HOLIDAYS, 14);
    TEST_ASSERT_EQUAL_INT(KIDS_EVENT_CHRISTMAS, c.kind);
    c = nextKidsCountdown(2026, 12, 25, b, 2, KIDS_ALL_HOLIDAYS, 14);
    TEST_ASSERT_EQUAL_INT(KIDS_EVENT_CHRISTMAS, c.kind);
    TEST_ASSERT_EQUAL_INT(0, c.sleeps);
}

void test_countdown_settings() {
    KidsBirthday b[2] = { { 2020, 10, 31, 'A' }, { 0, 0, 0, 0 } };   // the second one is not set
    // a birthday wins a tie with a holiday
    KidsCountdown c = nextKidsCountdown(2026, 10, 25, b, 2, KIDS_ALL_HOLIDAYS, 14);
    TEST_ASSERT_EQUAL_INT(KIDS_EVENT_BIRTHDAY, c.kind);
    TEST_ASSERT_EQUAL_INT(6, c.age);
    // holidays switched off
    TEST_ASSERT_FALSE(nextKidsCountdown(2026, 12, 20, b, 2, 0, 14).active);
    c = nextKidsCountdown(2026, 12, 20, b, 2, KIDS_HOLIDAY(KIDS_EVENT_CHRISTMAS), 14);
    TEST_ASSERT_EQUAL_INT(KIDS_EVENT_CHRISTMAS, c.kind);
    // the range
    TEST_ASSERT_FALSE(nextKidsCountdown(2026, 12, 20, b, 2, KIDS_ALL_HOLIDAYS, 4).active);
    TEST_ASSERT_TRUE(nextKidsCountdown(2026, 12, 20, b, 2, KIDS_ALL_HOLIDAYS, 5).active);
    // a birth date in the future is skipped
    KidsBirthday future[1] = { { 2027, 10, 30, 0 } };
    TEST_ASSERT_FALSE(nextKidsCountdown(2026, 10, 25, future, 1, 0, 14).active);
}

void test_contrast_for_percent() {
    TEST_ASSERT_EQUAL_UINT8(3, contrastForPercent(1));
    TEST_ASSERT_EQUAL_UINT8(3, contrastForPercent(0));      // never 0 (dark on some panels)
    TEST_ASSERT_EQUAL_UINT8(26, contrastForPercent(10));
    TEST_ASSERT_EQUAL_UINT8(255, contrastForPercent(100));
    TEST_ASSERT_EQUAL_UINT8(255, contrastForPercent(150));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rate_ride_good);
    RUN_TEST(test_rate_ride_caution);
    RUN_TEST(test_rate_ride_dont);
    RUN_TEST(test_rate_ride_thresholds_are_exclusive);
    RUN_TEST(test_rain_probability_makes_it_caution);
    RUN_TEST(test_rate_window_uses_the_highest_probability);
    RUN_TEST(test_score_window);
    RUN_TEST(test_score_window_hours);
    RUN_TEST(test_score_day_and_best_day);
    RUN_TEST(test_best_start_hour);
    RUN_TEST(test_rate_window_uses_worst_hour);
    RUN_TEST(test_rate_window_sums_rain);
    RUN_TEST(test_rate_window_missing_hours);
    RUN_TEST(test_outfit);
    RUN_TEST(test_kids_limits_must_go_from_warm_to_cold);
    RUN_TEST(test_kids_outfits_follow_changed_limits);
    RUN_TEST(test_kids_window);
    RUN_TEST(test_kids_window_highest_temperature);
    RUN_TEST(test_part_of_day);
    RUN_TEST(test_day_parts_in_the_morning);
    RUN_TEST(test_day_parts_roll_into_tomorrow);
    RUN_TEST(test_day_parts_at_night);
    RUN_TEST(test_contrast_for_percent);
    RUN_TEST(test_kids_initial);
    RUN_TEST(test_days_from_civil);
    RUN_TEST(test_parse_iso_date);
    RUN_TEST(test_days_until_next);
    RUN_TEST(test_countdown_picks_the_nearest_event);
    RUN_TEST(test_countdown_settings);
    RUN_TEST(test_kids_weather);
    RUN_TEST(test_weather_codes);
    RUN_TEST(test_wind_overrides_only_dry_weather);
    RUN_TEST(test_autumn_follows_the_hemisphere);
    RUN_TEST(test_leaves_blow_in_autumn_wind_only);
    RUN_TEST(test_kids_leaves_blow_in_dry_autumn_wind_only);
    RUN_TEST(test_trend);
    RUN_TEST(test_night_detection);
    RUN_TEST(test_night_detection_rolls_forward_by_days);
    RUN_TEST(test_day_of_week);
    RUN_TEST(test_interval_elapsed);
    RUN_TEST(test_parse_version);
    RUN_TEST(test_newer_version);
    RUN_TEST(test_hex_to_bytes);
    return UNITY_END();
}
