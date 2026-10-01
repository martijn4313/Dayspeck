// Native unit tests for the pure logic: pio test -e native
#include <unity.h>
#include <math.h>
#include "motologic.h"

static const RideThresholds T = { 2.0f, 45.0f, 5.0f, 25.0f };   // the config.h defaults

void setUp() {}
void tearDown() {}

void test_rate_ride_good() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateRide(T, 0, 10, 15));
}

void test_rate_ride_caution() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 0.5f, 10, 15));   // light rain
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 0, 10, 3));       // cold
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 0, 30, 15));      // gusty
}

void test_rate_ride_dont() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_DONT, rateRide(T, 3, 10, 15));         // heavy rain
    TEST_ASSERT_EQUAL_CHAR(RIDE_DONT, rateRide(T, 0, 50, 15));         // storm gusts
}

void test_rate_ride_thresholds_are_exclusive() {
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateRide(T, 2.0f, 45.0f, 15));   // exactly at the maximum
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateRide(T, 0, 25.0f, 5.0f));       // exactly at the warnings
}

void test_rate_window_uses_worst_hour() {
    float temp[3] = { 12, 4, 12 };
    float rain[3] = { 0, 0, 0 };
    float gust[3] = { 10, 10, 10 };
    TEST_ASSERT_EQUAL_CHAR(RIDE_CAUTION, rateWindow(T, temp, rain, gust, 3));   // one cold hour
}

void test_rate_window_sums_rain() {
    float temp[2] = { 12, 12 };
    float rain[2] = { 1.5f, 1.5f };   // 3 mm in total
    float gust[2] = { 10, 10 };
    TEST_ASSERT_EQUAL_CHAR(RIDE_DONT, rateWindow(T, temp, rain, gust, 2));
}

void test_rate_window_missing_hours() {
    float nan = NAN;
    float temp[2] = { nan, 12 };
    float rain[2] = { nan, 0 };
    float gust[2] = { nan, 10 };
    TEST_ASSERT_EQUAL_CHAR(RIDE_GOOD, rateWindow(T, temp, rain, gust, 2));

    float allNan[2] = { nan, nan };
    TEST_ASSERT_EQUAL_CHAR(RIDE_UNKNOWN, rateWindow(T, allNan, allNan, allNan, 2));
    TEST_ASSERT_EQUAL_CHAR(RIDE_UNKNOWN, rateWindow(T, allNan, allNan, allNan, 0));
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rate_ride_good);
    RUN_TEST(test_rate_ride_caution);
    RUN_TEST(test_rate_ride_dont);
    RUN_TEST(test_rate_ride_thresholds_are_exclusive);
    RUN_TEST(test_rate_window_uses_worst_hour);
    RUN_TEST(test_rate_window_sums_rain);
    RUN_TEST(test_rate_window_missing_hours);
    RUN_TEST(test_weather_codes);
    RUN_TEST(test_wind_overrides_only_dry_weather);
    RUN_TEST(test_trend);
    RUN_TEST(test_night_detection);
    RUN_TEST(test_night_detection_rolls_forward_by_days);
    RUN_TEST(test_day_of_week);
    RUN_TEST(test_interval_elapsed);
    return UNITY_END();
}
