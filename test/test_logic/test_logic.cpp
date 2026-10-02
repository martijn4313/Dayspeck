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

void test_kids_later_warns_for_rain_after_the_window() {
    KidsHour h[10];
    for (int i = 0; i < 10; i++) h[i] = hr(18, 0, 1);
    KidsOutlook o = kidsLaterOutlook(h, 10, 6, 10, K);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLEAR, o.weather);
    TEST_ASSERT_EQUAL(2, o.hour);                        // middle of the window

    h[8] = hr(17, 0.6f, 61);                             // light rain later: no warning
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLEAR, kidsLaterOutlook(h, 10, 6, 10, K).weather);

    h[8] = hr(17, 2.0f, 63);                             // heavy rain later: show that hour
    o = kidsLaterOutlook(h, 10, 6, 10, K);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_RAIN, o.weather);
    TEST_ASSERT_EQUAL(OUTFIT_RAIN, o.outfit);
    TEST_ASSERT_EQUAL(8, o.hour);

    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLEAR, kidsLaterOutlook(h, 10, 6, 8, K).weather);   // beyond the look-ahead
}

void test_kids_later_warns_for_a_big_temperature_change() {
    KidsHour h[10];
    for (int i = 0; i < 10; i++) h[i] = hr(17, 0, 1);   // mild
    h[7] = hr(13, 0, 1);                                 // one step colder: no warning
    TEST_ASSERT_EQUAL(2, kidsLaterOutlook(h, 10, 6, 10, K).hour);   // still the window
    h[9] = hr(3, 0, 1);                                  // two steps colder
    KidsOutlook o = kidsLaterOutlook(h, 10, 6, 10, K);
    TEST_ASSERT_EQUAL(9, o.hour);
    TEST_ASSERT_EQUAL(OUTFIT_COLD, o.outfit);
}

void test_time_of_day() {
    TEST_ASSERT_EQUAL(KIDS_TIME_NIGHT, timeOfDay(5));
    TEST_ASSERT_EQUAL(KIDS_TIME_MORNING, timeOfDay(6));
    TEST_ASSERT_EQUAL(KIDS_TIME_MORNING, timeOfDay(11));
    TEST_ASSERT_EQUAL(KIDS_TIME_AFTERNOON, timeOfDay(12));
    TEST_ASSERT_EQUAL(KIDS_TIME_EVENING, timeOfDay(18));
    TEST_ASSERT_EQUAL(KIDS_TIME_NIGHT, timeOfDay(22));
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
    RUN_TEST(test_kids_window);
    RUN_TEST(test_kids_later_warns_for_rain_after_the_window);
    RUN_TEST(test_kids_later_warns_for_a_big_temperature_change);
    RUN_TEST(test_time_of_day);
    RUN_TEST(test_contrast_for_percent);
    RUN_TEST(test_kids_weather);
    RUN_TEST(test_weather_codes);
    RUN_TEST(test_wind_overrides_only_dry_weather);
    RUN_TEST(test_trend);
    RUN_TEST(test_night_detection);
    RUN_TEST(test_night_detection_rolls_forward_by_days);
    RUN_TEST(test_day_of_week);
    RUN_TEST(test_interval_elapsed);
    return UNITY_END();
}
