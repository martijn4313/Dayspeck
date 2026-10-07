// Native unit tests for the pure logic: pio test -e native
#include <unity.h>
#include <initializer_list>
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

static ScreenList list(std::initializer_list<int> ids) {
    ScreenList l = {};
    for (int id : ids) l.ids[l.count++] = (uint8_t)id;
    return l;
}

void test_screen_names() {
    TEST_ASSERT_EQUAL_STRING("rideOther", screenName(SCREEN_RIDE_OTHER));
    TEST_ASSERT_EQUAL_STRING("", screenName(42));
    for (int i = 0; i < SCREEN_COUNT; i++) TEST_ASSERT_EQUAL(i, screenFromName(screenName(i)));
    TEST_ASSERT_EQUAL(-1, screenFromName("nope"));
    TEST_ASSERT_EQUAL(SCREEN_VILLAGE, screenFromName("village"));
    TEST_ASSERT_TRUE(screenAlwaysAvailable(SCREEN_VILLAGE));   // it can be the home screen
    TEST_ASSERT_EQUAL(-1, screenFromName(nullptr));
}

void test_screen_lists_valid() {
    ScreenList none = {};
    TEST_ASSERT_TRUE(screenListsValid(list({ SCREEN_RIDE, SCREEN_RIDE_OTHER }), list({ SCREEN_WEEK, SCREEN_CLOCK })));
    TEST_ASSERT_TRUE(screenListsValid(list({ SCREEN_WEATHER }), none));
    TEST_ASSERT_FALSE(screenListsValid(none, none));                                    // no home screen
    TEST_ASSERT_FALSE(screenListsValid(list({ SCREEN_CLOCK, SCREEN_RIDE }), none));      // home may disappear
    TEST_ASSERT_FALSE(screenListsValid(list({ SCREEN_COUNTDOWN }), none));
    TEST_ASSERT_FALSE(screenListsValid(list({ SCREEN_RIDE, 99 }), none));
}

void test_screen_tap_steps_and_skips() {
    ScreenList tap = list({ SCREEN_WEATHER, SCREEN_CLOTHES, SCREEN_COUNTDOWN });
    ScreenList hold = {};
    uint16_t all = 0xFFFF, noCountdown = (uint16_t)~(1u << SCREEN_COUNTDOWN);
    ScreenNav n = { 0, 0 };
    n = screenNextTap(n, tap, all);
    TEST_ASSERT_EQUAL(SCREEN_CLOTHES, screenAt(n, tap, hold));
    n = screenNextTap(n, tap, all);
    TEST_ASSERT_EQUAL(SCREEN_COUNTDOWN, screenAt(n, tap, hold));
    n = screenNextTap(n, tap, all);
    TEST_ASSERT_EQUAL(SCREEN_WEATHER, screenAt(n, tap, hold));             // wraps around
    n = screenNextTap(ScreenNav{ 0, 1 }, tap, noCountdown);
    TEST_ASSERT_EQUAL(SCREEN_WEATHER, screenAt(n, tap, hold));             // the countdown is skipped
    n = screenNextTap(ScreenNav{ 0, 0 }, list({ SCREEN_RIDE }), all);
    TEST_ASSERT_EQUAL(0, n.slot);                                          // one screen: stays home
}

void test_screen_hold_list() {
    ScreenList tap = list({ SCREEN_RIDE, SCREEN_RIDE_OTHER });
    ScreenList hold = list({ SCREEN_WEEK, SCREEN_HOURS, SCREEN_CLOCK });
    uint16_t all = 0xFFFF, noClock = (uint16_t)~(1u << SCREEN_CLOCK);
    ScreenNav n = { 0, 1 };                                                // the other day
    n = screenNextHold(n, tap, hold, all);
    TEST_ASSERT_EQUAL(SCREEN_WEEK, screenAt(n, tap, hold));
    n = screenNextHold(n, tap, hold, all);
    TEST_ASSERT_EQUAL(SCREEN_HOURS, screenAt(n, tap, hold));
    n = screenNextHold(n, tap, hold, noClock);
    TEST_ASSERT_EQUAL(SCREEN_RIDE, screenAt(n, tap, hold));                // clock skipped, past the end: home
    TEST_ASSERT_EQUAL(0, n.list);
    n = screenNextTap(ScreenNav{ 1, 1 }, tap, all);
    TEST_ASSERT_EQUAL(SCREEN_RIDE, screenAt(n, tap, hold));                // a tap in the hold list goes home

    ScreenList none = {};
    n = screenNextHold(ScreenNav{ 0, 0 }, tap, none, all);
    TEST_ASSERT_EQUAL(SCREEN_RIDE_OTHER, screenAt(n, tap, none));          // no hold list: like a tap
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

void test_kids_part_temperature() {
    // This morning: 10 at 07:00, 17 from 11:00. The morning column shows 10 and a sweater, not 16.
    KidsHour h[5] = { hr(10, 0, 1), hr(12, 0, 1), hr(14, 0, 1), hr(16, 0, 1), hr(17, 0, 1) };
    KidsPart morning = { KIDS_PART_MORNING, 0, 5, false, true };
    KidsOutlook o = kidsPartOutlook(h, morning, K);
    TEST_ASSERT_EQUAL(10, o.tempC);
    TEST_ASSERT_EQUAL(OUTFIT_COOL, o.outfit);

    // A morning that gets colder: still the lowest
    KidsHour cold[3] = { hr(14, 0, 1), hr(12, 0, 1), hr(9, 0, 3) };
    TEST_ASSERT_EQUAL(9, kidsPartOutlook(cold, KidsPart{ KIDS_PART_MORNING, 0, 3, false, false }, K).tempC);

    // The afternoon: the highest, and the outfit goes by it
    KidsHour pm[3] = { hr(17, 0, 1), hr(21, 0, 1), hr(19, 0, 1) };
    o = kidsPartOutlook(pm, KidsPart{ KIDS_PART_AFTERNOON, 0, 3, false, false }, K);
    TEST_ASSERT_EQUAL(21, o.tempC);
    TEST_ASSERT_EQUAL(OUTFIT_WARM, o.outfit);

    // Dinner: its first hour (dinner time), not the cold of 22:00
    KidsHour ev[4] = { hr(16, 0, 1), hr(14, 0, 1), hr(12, 0, 1), hr(11, 0, 1) };
    o = kidsPartOutlook(ev, KidsPart{ KIDS_PART_DINNER, 0, 4, false, false }, K);
    TEST_ASSERT_EQUAL(16, o.tempC);
    TEST_ASSERT_EQUAL(OUTFIT_MILD, o.outfit);

    // Dinner: rain from 21:00 (dinner at 18:00) is not dinner's rain; rain in the hour after dinner time is
    KidsHour late[4] = { hr(16, 0, 1), hr(15, 0, 1), hr(14, 0, 1), hr(13, 1.5f, 63) };
    o = kidsPartOutlook(late, KidsPart{ KIDS_PART_DINNER, 0, 4, false, false }, K);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_CLEAR, o.weather);
    TEST_ASSERT_EQUAL(OUTFIT_MILD, o.outfit);
    late[1] = hr(15, 1.5f, 63);
    o = kidsPartOutlook(late, KidsPart{ KIDS_PART_DINNER, 0, 4, false, false }, K);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_RAIN, o.weather);
    TEST_ASSERT_EQUAL(OUTFIT_RAIN, o.outfit);
    // The sunset evening still goes by all of its hours
    late[1] = hr(15, 0, 1);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_RAIN, kidsPartOutlook(late, KidsPart{ KIDS_PART_EVENING, 0, 4, false, false }, K).weather);

    // The night: its lowest
    KidsHour nt[4] = { hr(12, 0, 1), hr(9, 0, 1), hr(7, 0, 1), hr(8, 0, 1) };
    o = kidsPartOutlook(nt, KidsPart{ KIDS_PART_NIGHT, 0, 4, false, false }, K);
    TEST_ASSERT_EQUAL(7, o.tempC);

    // Rain still makes it a rain coat, whatever the number
    KidsHour wet[2] = { hr(18, 0, 1), hr(18, 1.0f, 61) };
    o = kidsPartOutlook(wet, KidsPart{ KIDS_PART_AFTERNOON, 0, 2, false, false }, K);
    TEST_ASSERT_EQUAL(OUTFIT_RAIN, o.outfit);

    // Hours without data are skipped; none at all: not valid
    KidsHour gaps[3] = { hr(30, 0, 1), hr(8, 0, 1), hr(12, 0, 1) };
    gaps[0].valid = false;
    TEST_ASSERT_EQUAL(8, kidsPartOutlook(gaps, KidsPart{ KIDS_PART_MORNING, 0, 3, false, false }, K).tempC);
    gaps[1].valid = gaps[2].valid = false;
    TEST_ASSERT_FALSE(kidsPartOutlook(gaps, KidsPart{ KIDS_PART_MORNING, 0, 3, false, false }, K).valid);
}

// The village: the temperature of now, and rain on its way
void test_kids_now_outlook() {
    // Noon, 18 now and 27 this afternoon: a t-shirt now, not the shorts of the afternoon
    KidsHour h[5] = { hr(18, 0, 1), hr(22, 0, 1), hr(25, 0, 1), hr(27, 0, 1), hr(27, 0, 1) };
    KidsOutlook o = kidsNowOutlook(h, 5, K);
    TEST_ASSERT_TRUE(o.valid);
    TEST_ASSERT_EQUAL(18, o.tempC);
    TEST_ASSERT_EQUAL(OUTFIT_MILD, o.outfit);

    // Rain within the next two hours: the rain coat now; rain later does not count
    h[2] = hr(17, 1.0f, 61);
    TEST_ASSERT_EQUAL(OUTFIT_RAIN, kidsNowOutlook(h, 5, K).outfit);
    h[2] = hr(25, 0, 1);
    h[3] = hr(17, 1.0f, 61);
    TEST_ASSERT_EQUAL(OUTFIT_MILD, kidsNowOutlook(h, 5, K).outfit);

    // Fewer hours than the window, and none at all
    TEST_ASSERT_EQUAL(18, kidsNowOutlook(h, 1, K).tempC);
    TEST_ASSERT_FALSE(kidsNowOutlook(h, 0, K).valid);
    h[0].valid = false;
    TEST_ASSERT_FALSE(kidsNowOutlook(h, 5, K).valid);
}

void test_kids_precip_level() {
    TEST_ASSERT_EQUAL(KIDS_PRECIP_DRIZZLE, kidsPrecipLevel(0.2f));
    TEST_ASSERT_EQUAL(KIDS_PRECIP_DRIZZLE, kidsPrecipLevel(0.49f));
    TEST_ASSERT_EQUAL(KIDS_PRECIP_RAIN, kidsPrecipLevel(0.5f));
    TEST_ASSERT_EQUAL(KIDS_PRECIP_RAIN, kidsPrecipLevel(1.9f));
    TEST_ASSERT_EQUAL(KIDS_PRECIP_HEAVY, kidsPrecipLevel(2.0f));
    TEST_ASSERT_EQUAL(KIDS_PRECIP_DOWNPOUR, kidsPrecipLevel(5.0f));
    TEST_ASSERT_EQUAL(KIDS_PRECIP_DRIZZLE, kidsPrecipLevel(NAN));

    // A part goes by its wettest hour
    KidsHour h[3] = { hr(10, 0.6f, 61), hr(10, 3.0f, 63), hr(10, 0, 1) };
    KidsOutlook o = kidsWindowOutlook(h, 0, 3, K);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_RAIN, o.weather);
    TEST_ASSERT_EQUAL(KIDS_PRECIP_HEAVY, o.precip);
    TEST_ASSERT_EQUAL(KIDS_PRECIP_RAIN, kidsWindowOutlook(h, 0, 1, K).precip);
    // Snow too
    KidsHour s[2] = { hr(-1, 6.0f, 75), hr(-1, 0, 3) };
    o = kidsWindowOutlook(s, 0, 2, K);
    TEST_ASSERT_EQUAL(KIDS_WEATHER_SNOW, o.weather);
    TEST_ASSERT_EQUAL(KIDS_PRECIP_DOWNPOUR, o.precip);
}

void test_part_of_day() {
    TEST_ASSERT_EQUAL(KIDS_PART_NIGHT, partOfDay(6, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, partOfDay(7, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, partOfDay(11, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, partOfDay(12, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, partOfDay(17, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, partOfDay(18, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, partOfDay(21, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_NIGHT, partOfDay(22, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_NIGHT, partOfDay(0, 18));

    // Dinner time moves the boundary with the afternoon
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, partOfDay(18, 19));
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, partOfDay(19, 19));
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, partOfDay(17, 17));
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, partOfDay(18, 99));   // out of range: 18:00
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, partOfDay(17, 99));

    // The sunset evening: 18-22, like dinner at 18:00
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, partOfDay(17, KIDS_SUNSET_EVENING));
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, partOfDay(18, KIDS_SUNSET_EVENING));
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, partOfDay(21, KIDS_SUNSET_EVENING));
    TEST_ASSERT_EQUAL(KIDS_PART_NIGHT, partOfDay(22, KIDS_SUNSET_EVENING));
}

// An hour after dinner time the current dinner column shows the evening
void test_kids_shown_part() {
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, kidsShownPart(KIDS_PART_DINNER, true, 18, 18));     // eating
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, kidsShownPart(KIDS_PART_DINNER, true, 19, 18));    // dinner is over
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, kidsShownPart(KIDS_PART_DINNER, true, 21, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, kidsShownPart(KIDS_PART_DINNER, false, 19, 18));   // still to come: dinner
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, kidsShownPart(KIDS_PART_DINNER, true, 18, 17));    // dinner at 17
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, kidsShownPart(KIDS_PART_DINNER, true, 21, 21));     // until bedtime
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, kidsShownPart(KIDS_PART_AFTERNOON, true, 19, 18));
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, kidsShownPart(KIDS_PART_EVENING, true, 18, KIDS_SUNSET_EVENING));
}

void test_light_at() {
    const long rise = 7 * 3600, set = 19 * 3600;    // day 0, in seconds
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DAY, lightAt(12 * 3600, rise, set));
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DAY, lightAt(17 * 3600 + 59 * 60, rise, set));
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DUSK, lightAt(18 * 3600, rise, set));        // an hour before sunset
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DUSK, lightAt(19 * 3600 + 30 * 60, rise, set));   // half an hour after
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DARK, lightAt(19 * 3600 + 31 * 60, rise, set));
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DARK, lightAt(3 * 3600, rise, set));
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DUSK, lightAt(86400 + 18 * 3600 + 30 * 60, rise, set));   // the next day too
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DAY, lightAt(86400 + 9 * 3600, rise, set));
}

// Dinner takes the light of dinner time: summer sun, autumn sunset, winter moon
void test_dinner_light() {
    KidsHour h[3] = { hr(15, 0, 0), hr(14, 0, 0), hr(13, 0, 0) };   // a clear evening
    h[1].night = h[2].night = true;                                // most of it after sunset
    KidsPart dinner = { KIDS_PART_DINNER, 0, 3, false, false };

    h[0].light = KIDS_LIGHT_DAY;
    KidsOutlook o = kidsPartOutlook(h, dinner, K);
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DAY, o.light);   // not the moon, though the evening is mostly dark
    TEST_ASSERT_FALSE(o.night);

    h[0].light = KIDS_LIGHT_DUSK;
    o = kidsPartOutlook(h, dinner, K);
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DUSK, o.light);
    TEST_ASSERT_FALSE(o.night);

    h[0].light = KIDS_LIGHT_DARK;
    o = kidsPartOutlook(h, dinner, K);
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DARK, o.light);
    TEST_ASSERT_TRUE(o.night);

    // The night always has the moon
    o = kidsPartOutlook(h, KidsPart{ KIDS_PART_NIGHT, 0, 3, false, false }, K);
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DARK, o.light);
    TEST_ASSERT_TRUE(o.night);

    // The sunset evening: the temperature of its first hour, the moon when most of it is dark
    h[0].light = KIDS_LIGHT_DAY;
    o = kidsPartOutlook(h, KidsPart{ KIDS_PART_EVENING, 0, 3, false, false }, K);
    TEST_ASSERT_EQUAL(15, o.tempC);
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DARK, o.light);
    TEST_ASSERT_TRUE(o.night);
    h[1].night = false;
    o = kidsPartOutlook(h, KidsPart{ KIDS_PART_EVENING, 0, 3, false, false }, K);
    TEST_ASSERT_EQUAL(KIDS_LIGHT_DAY, o.light);
}

// Local hours of `count` forecast hours from `first` on
static void hoursFrom(int first, int* out, size_t count) {
    for (size_t i = 0; i < count; i++) out[i] = (first + (int)i) % 24;
}

void test_day_parts_in_the_morning() {
    int h[24];
    hoursFrom(10, h, 24);                       // 10:00
    KidsPart p[3];
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3, 18, false));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[0].part);
    TEST_ASSERT_EQUAL(0, p[0].from);            // only what is left of the morning: 10 and 11
    TEST_ASSERT_EQUAL(2, p[0].to);
    TEST_ASSERT_TRUE(p[0].now);
    TEST_ASSERT_FALSE(p[0].afterSleep);
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, p[1].part);
    TEST_ASSERT_EQUAL(6, p[1].to - p[1].from);
    TEST_ASSERT_FALSE(p[1].now);
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, p[2].part);
    TEST_ASSERT_FALSE(p[2].afterSleep);
}

void test_day_parts_roll_into_tomorrow() {
    int h[24];
    hoursFrom(19, h, 24);                       // 19:00: evening, then tomorrow morning and afternoon
    KidsPart p[3];
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3, 18, false));
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, p[0].part);
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
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3, 18, false));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[0].part);
    TEST_ASSERT_FALSE(p[0].now);
    TEST_ASSERT_TRUE(p[0].afterSleep);
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, p[2].part);

    TEST_ASSERT_EQUAL(0, kidsDayParts(h, 3, p, 3, 18, false));   // only night hours: no parts
}

void test_day_parts_dinner_time() {
    int h[24];
    hoursFrom(13, h, 24);                       // 13:00, dinner at 19:00
    KidsPart p[3];
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3, 19, false));
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, p[0].part);
    TEST_ASSERT_EQUAL(6, p[0].to);              // 13:00-18:00
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, p[1].part);
    TEST_ASSERT_EQUAL(6, p[1].from);            // from 19:00...
    TEST_ASSERT_EQUAL(9, p[1].to);              // ...until 22:00
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[2].part);
}

void test_day_parts_night_column() {
    int h[24];
    KidsPart p[3];

    // The afternoon: the night would be the last column, so tomorrow morning keeps it
    hoursFrom(13, h, 24);
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3, 18, true));
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, p[0].part);
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, p[1].part);
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[2].part);
    TEST_ASSERT_TRUE(p[2].afterSleep);

    // The evening: dinner, the night, tomorrow morning
    hoursFrom(19, h, 24);
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3, 18, true));
    TEST_ASSERT_EQUAL(KIDS_PART_DINNER, p[0].part);
    TEST_ASSERT_TRUE(p[0].now);
    TEST_ASSERT_EQUAL(KIDS_PART_NIGHT, p[1].part);
    TEST_ASSERT_EQUAL(3, p[1].from);            // 22:00...
    TEST_ASSERT_EQUAL(12, p[1].to);             // ...until 07:00
    TEST_ASSERT_FALSE(p[1].afterSleep);
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[2].part);
    TEST_ASSERT_FALSE(p[2].afterSleep);         // the night has a column of its own

    // At night: the night now, then tomorrow morning and afternoon
    hoursFrom(23, h, 24);
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3, 18, true));
    TEST_ASSERT_EQUAL(KIDS_PART_NIGHT, p[0].part);
    TEST_ASSERT_TRUE(p[0].now);
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[1].part);
    TEST_ASSERT_EQUAL(KIDS_PART_AFTERNOON, p[2].part);

    // The sunset evening gets the night column too
    hoursFrom(19, h, 24);
    TEST_ASSERT_EQUAL(3, kidsDayParts(h, 24, p, 3, KIDS_SUNSET_EVENING, true));
    TEST_ASSERT_EQUAL(KIDS_PART_EVENING, p[0].part);
    TEST_ASSERT_EQUAL(KIDS_PART_NIGHT, p[1].part);
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[2].part);

    // One column (the village): never the night
    hoursFrom(23, h, 24);
    TEST_ASSERT_EQUAL(1, kidsDayParts(h, 24, p, 1, 18, true));
    TEST_ASSERT_EQUAL(KIDS_PART_MORNING, p[0].part);
    TEST_ASSERT_TRUE(p[0].afterSleep);
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

// Weather report: hours from `start` on, made by f(hour)
static size_t reportDay(ReportHour* out, int start, int count, ReportHour (*f)(int)) {
    for (int i = 0; i < count; i++) out[i] = f((start + i) % 24);
    return (size_t)count;
}
static ReportHour rh(int hour, float t, float rain, float gust, int prob, int code) {
    return ReportHour{ hour, t, rain, gust, prob, code, true };
}
static void assertReport(const WeatherReport& r, std::initializer_list<const char*> expected) {
    TEST_ASSERT_EQUAL(expected.size(), r.count);
    size_t i = 0;
    for (const char* e : expected) TEST_ASSERT_EQUAL_STRING(e, r.sentences[i++]);
}

void test_weather_report() {
    ReportHour h[36];
    WeatherReport r;
    // A: 07:00, a fresh morning, sun, rain from 19:00
    size_t n = reportDay(h, 7, 24, [](int hr) {
        float t = hr < 7 || hr >= 22 ? 9.0f : hr < 11 ? 10 + (hr - 7) * 1.8f : hr < 17 ? 17.0f : 17 - (hr - 16) * 0.8f;
        if (hr >= 19 && hr < 22) return rh(hr, t, 1.2f, 20, 80, 61);
        return rh(hr, t, 0, 20, 10, hr < 12 ? 0 : 1);
    });
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vandaag zonnig.", "Frisse ochtend (10\xF8), 's middags 17\xF8.", "Vanaf 19 uur regen." });
    weatherReport(h, n, REPORT_LANG_EN, r);
    assertReport(r, { "Today sunny.", "Chilly morning (10\xF8), 17\xF8 in the afternoon.", "Rain from 19:00." });

    // B: 09:00, rain until 15:00
    n = reportDay(h, 9, 24, [](int hr) {
        float t = 12 + (hr >= 12 && hr < 18 ? 2.0f : 0);
        if (hr >= 7 && hr < 15) return rh(hr, t, 1.5f, 30, 90, 63);
        return rh(hr, t, 0, 30, 40, 3);
    });
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vandaag bewolkt, rond 14\xF8.", "Tot 15 uur regen, daarna droog." });
    weatherReport(h, n, REPORT_LANG_EN, r);
    assertReport(r, { "Today cloudy, around 14\xF8.", "Rain until 15:00, then dry." });

    // C: 07:00, frost and sun
    n = reportDay(h, 7, 24, [](int hr) {
        return rh(hr, hr < 9 ? -3.0f : hr < 15 ? -3 + (hr - 9) * 1.2f : 4 - (hr - 15) * 0.8f, 0, 15, 5, 0);
    });
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vandaag zonnig, droog.", "Koude ochtend (-3\xF8), 's middags 4\xF8.", "Kans op gladheid." });

    // D: 20:00, about tomorrow: a shower around 14:00 and strong gusts
    n = reportDay(h, 20, 30, [](int hr) {
        float t = hr >= 20 || hr < 7 ? 9.0f : hr < 14 ? 8 + (hr - 7) * 1.1f : 16 - (hr - 14) * 0.5f;
        float gust = hr >= 12 && hr < 18 ? 65.0f : 30.0f;
        if (hr == 14 || hr == 15) return rh(hr, t, 2.0f, gust, 70, 81);
        return rh(hr, t, 0, gust, 20, 2);
    });
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vanavond en vannacht droog, minimaal 9\xF8.", "Morgen zon en wolken.",
                      "Frisse ochtend (8\xF8), 's middags 16\xF8.", "Rond 14 uur een bui.", "Harde windvlagen tot 65 km/u." });
    weatherReport(h, n, REPORT_LANG_EN, r);
    assertReport(r, { "Dry this evening and tonight, low 9\xF8.", "Tomorrow sun and clouds.",
                      "Chilly morning (8\xF8), 16\xF8 in the afternoon.", "A shower around 14:00.", "Strong gusts up to 65 km/h." });
    // Too long for the screen (11 lines): the shower and the fresh morning go first, the warning stays
    char lines[7][REPORT_COLS + 1];
    TEST_ASSERT_EQUAL(6, fitReport(r, lines, 7));
    TEST_ASSERT_EQUAL_STRING("Dry this evening and", lines[0]);
    TEST_ASSERT_EQUAL_STRING("Tomorrow sun and", lines[2]);
    TEST_ASSERT_EQUAL_STRING("Strong gusts up to 65", lines[4]);

    // E: 13:00, hot, thunder around 17:00
    n = reportDay(h, 13, 24, [](int hr) {
        float t = hr < 18 ? 27.0f : 22.0f;
        if (hr == 17) return rh(hr, t, 4, 25, 60, 95);
        return rh(hr, t, 0, 25, 20, 1);
    });
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vandaag zonnig, 22 tot 27\xF8.", "Rond 17 uur kans op onweer." });

    // F: 10:00, grey with a chance of rain
    n = reportDay(h, 10, 24, [](int hr) { return rh(hr, 11.0f + (hr >= 12 && hr < 16 ? 1.0f : 0), 0, 25, 40, 3); });
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vandaag bewolkt, overwegend droog, rond 12\xF8." });
    weatherReport(h, n, REPORT_LANG_EN, r);
    assertReport(r, { "Today cloudy, mostly dry, around 12\xF8." });

    // Frost tonight before a mild tomorrow; rain all day
    n = reportDay(h, 21, 30, [](int hr) {
        if (hr >= 22 || hr < 7) return rh(hr, -2, 0, 10, 0, 0);
        return rh(hr, 6, 1.0f, 20, 90, 61);
    });
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vanavond soms een bui, vannacht droog, minimaal -2\xF8.", "Morgen veel regen, rond 6\xF8.",
                      "Kans op gladheid." });
    // The same day without the frost: the amount, without saying "heavy rain" twice
    for (size_t i = 0; i < n; i++) if (h[i].tempC < 0) h[i].tempC = 3;
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vanavond soms een bui, vannacht droog, minimaal 3\xF8.", "Morgen veel regen, rond 6\xF8.",
                      "In totaal 15 mm." });
    weatherReport(h, n, REPORT_LANG_EN, r);
    assertReport(r, { "A shower at times this evening, dry tonight, low 3\xF8.", "Tomorrow heavy rain, around 6\xF8.",
                      "15 mm in total." });

    // 23:00: only the night, then tomorrow
    n = reportDay(h, 23, 24, [](int hr) {
        if (hr >= 22 || hr < 7) return rh(hr, hr < 3 ? 5.0f : 4.0f, hr >= 2 && hr < 5 ? 0.8f : 0, 15, 70, hr >= 2 && hr < 5 ? 61 : 3);
        return rh(hr, 9, 0, 15, 10, 3);
    });
    weatherReport(h, n, REPORT_LANG_NL, r);
    assertReport(r, { "Vannacht buien, minimaal 4\xF8.", "Morgen bewolkt, droog, rond 9\xF8." });
    weatherReport(h, n, REPORT_LANG_EN, r);
    assertReport(r, { "Showers tonight, low 4\xF8.", "Tomorrow cloudy, dry, around 9\xF8." });
}

void test_weather_report_needs_the_day() {
    ReportHour h[36];
    WeatherReport r;
    weatherReport(h, 0, REPORT_LANG_EN, r);
    TEST_ASSERT_EQUAL(0, r.count);
    // At 20:00 with forecast hours only until 02:00: nothing about tomorrow yet
    size_t n = reportDay(h, 20, 6, [](int hr) { return rh(hr, 10, 0, 10, 0, 0); });
    weatherReport(h, n, REPORT_LANG_EN, r);
    TEST_ASSERT_EQUAL(0, r.count);
    // Invalid hours do not count
    n = reportDay(h, 10, 6, [](int hr) { ReportHour x = rh(hr, 10, 0, 10, 0, 0); x.valid = hr != 10; return x; });
    weatherReport(h, n, REPORT_LANG_EN, r);
    assertReport(r, { "Today sunny, dry, around 10\xF8." });
}

void test_wrap_report() {
    WeatherReport r = { 3, { "Vandaag zonnig, 22 tot 27\xF8.", "Rond 17 uur kans op onweer.", "Abcdefghijklmnopqrstuvwxyz ok" },
                        { 4, 1, 2 } };
    char lines[8][REPORT_COLS + 1];
    TEST_ASSERT_EQUAL(6, wrapReport(r, 7, lines, 8));
    TEST_ASSERT_EQUAL_STRING("Vandaag zonnig, 22", lines[0]);
    TEST_ASSERT_EQUAL_STRING("tot 27\xF8.", lines[1]);
    TEST_ASSERT_EQUAL_STRING("Rond 17 uur kans op", lines[2]);   // every sentence starts on a new line
    TEST_ASSERT_EQUAL_STRING("onweer.", lines[3]);
    TEST_ASSERT_EQUAL_STRING("Abcdefghijklmnopqrstu", lines[4]);   // a word longer than a line is split
    TEST_ASSERT_EQUAL_STRING("vwxyz ok", lines[5]);
    TEST_ASSERT_EQUAL(4, wrapReport(r, 3, lines, 8));                // the first two sentences
    TEST_ASSERT_EQUAL(6, wrapReport(r, 7, lines, 2));               // counts the lines it could not store
    TEST_ASSERT_EQUAL_STRING("tot 27\xF8.", lines[1]);
    // Fitting in 4 lines leaves out the sentence with the lowest priority (the thunder), not the last one
    TEST_ASSERT_EQUAL(4, fitReport(r, lines, 4));
    TEST_ASSERT_EQUAL_STRING("tot 27\xF8.", lines[1]);
    TEST_ASSERT_EQUAL_STRING("Abcdefghijklmnopqrstu", lines[2]);
    // One sentence left that is still too long: as many lines as fit
    TEST_ASSERT_EQUAL(1, fitReport(r, lines, 1));
    TEST_ASSERT_EQUAL_STRING("Vandaag zonnig, 22", lines[0]);
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
    RUN_TEST(test_kids_part_temperature);
    RUN_TEST(test_kids_now_outlook);
    RUN_TEST(test_kids_precip_level);
    RUN_TEST(test_part_of_day);
    RUN_TEST(test_kids_shown_part);
    RUN_TEST(test_light_at);
    RUN_TEST(test_dinner_light);
    RUN_TEST(test_day_parts_in_the_morning);
    RUN_TEST(test_day_parts_roll_into_tomorrow);
    RUN_TEST(test_day_parts_at_night);
    RUN_TEST(test_day_parts_dinner_time);
    RUN_TEST(test_day_parts_night_column);
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
    RUN_TEST(test_screen_names);
    RUN_TEST(test_screen_lists_valid);
    RUN_TEST(test_screen_tap_steps_and_skips);
    RUN_TEST(test_screen_hold_list);
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
    RUN_TEST(test_weather_report);
    RUN_TEST(test_weather_report_needs_the_day);
    RUN_TEST(test_wrap_report);
    return UNITY_END();
}
