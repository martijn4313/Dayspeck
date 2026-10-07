// Host build of the firmware's own drawing code (firmware/src/display.cpp, Adafruit GFX, motologic) that draws
// the pictures of the manual: the picture screens, the village, the countdown and the weather report.
// Built and run by tools/screen_pictures.py, which turns the frames into PNGs and GIFs.
//
// Output: for every frame a line "=== <picture> <index>", then 64 lines of 128 '#' (on) or '.' (off).

#include <stdio.h>
#include "Arduino.h"
#include "config.h"
#include "display.h"
#include "motologic.h"

unsigned long hostMillis = 0;
static unsigned long seedValue = 1;
void hostSeed(unsigned long seed) { seedValue = seed; }
long random(long howBig) {
    seedValue = seedValue * 1103515245ul + 12345ul;
    return howBig > 0 ? (long)((seedValue >> 16) % (unsigned long)howBig) : 0;
}
long random(long howSmall, long howBig) { return howBig > howSmall ? howSmall + random(howBig - howSmall) : howSmall; }
unsigned long millis() { return hostMillis; }

static Adafruit_SSD1306 d;
static const KidsLimits LIMITS = { KIDS_HOT_FROM_C, KIDS_SHORTS_FROM_C, KIDS_SWEATER_BELOW_C, KIDS_COAT_BELOW_C,
                                   KIDS_FREEZE_BELOW_C, KIDS_WINDY_GUST_KMH };

static void frame(const char* picture, int index) {
    printf("=== %s %d\n", picture, index);
    for (int y = 0; y < 64; y++) {
        char row[129];
        for (int x = 0; x < 128; x++) row[x] = d.px[y][x] ? '#' : '.';
        row[128] = '\0';
        puts(row);
    }
}

// ---- A day of forecast hours ------------------------------------------------------------------------------

struct Hour { float t, rainMm, gust; int code; };
typedef Hour (*HourFn)(int hour);

struct Day {
    int  now;                 // local hour of hours[0]
    int  sunrise, sunset;     // local hours, for the night pictures
    Hour hours[36];
    size_t count;
};

static Day makeDay(int now, int sunrise, int sunset, HourFn f, size_t count = 24) {
    Day day = { now, sunrise, sunset, {}, count };
    for (size_t i = 0; i < count; i++) day.hours[i] = f((now + (int)i) % 24);
    return day;
}

// The kids columns of a day, as the firmware's kidsColumns() makes them
static size_t columns(const Day& day, KidsColumn* cols, KidsPart* parts, size_t max, bool nightColumn = false,
                      int evening = KIDS_DINNER_HOUR) {
    KidsHour hours[36];
    int localHours[36];
    for (size_t i = 0; i < day.count; i++) {
        int h = (day.now + (int)i) % 24;
        const Hour& x = day.hours[i];
        int light = h < day.sunrise || h > day.sunset ? KIDS_LIGHT_DARK
                  : h >= day.sunset - 1 ? KIDS_LIGHT_DUSK : KIDS_LIGHT_DAY;   // dusk: an hour before sunset on
        hours[i] = KidsHour{ x.t, x.rainMm, x.gust, x.code, h < day.sunrise || h >= day.sunset, true, light };
        localHours[i] = h;
    }
    size_t np = kidsDayParts(localHours, day.count, parts, max, evening, nightColumn);
    for (size_t i = 0; i < np; i++) {
        KidsOutlook o = kidsPartOutlook(hours, parts[i], LIMITS);
        int shown = kidsShownPart(parts[i].part, parts[i].now, day.now, evening);
        KidsColumn& c = cols[i];
        c = KidsColumn{};
        c.part = shown;
        c.valid = o.valid;
        c.outfit = o.outfit;
        c.weather = o.weather;
        c.light = o.light;
        c.temp = o.tempC;
        c.precip = o.precip;
        c.umbrella = o.umbrella;
        c.hours = (uint8_t)kidsPartTimeline(hours, parts[i], LIMITS, c.hourWeather, c.hourPrecip, KIDS_MAX_PART_HOURS);
    }
    return np;
}

// loopMs 0: the still pictures (each column's summary); otherwise the time-lapse at elapsedMs
static void dayStrip(const Day& day, bool weather, bool nightColumn = false, int evening = KIDS_DINNER_HOUR,
                     unsigned long elapsedMs = 0, unsigned long loopMs = 0) {
    KidsPart parts[3];
    KidsColumn cols[3];
    size_t np = columns(day, cols, parts, 3, nightColumn, evening);
    int nowColumn = -1, nightBefore = -1;
    for (size_t i = 0; i < np; i++) {
        if (parts[i].now) nowColumn = (int)i;
        if (parts[i].afterSleep && i > 0 && nightBefore < 0) nightBefore = (int)i;
    }
    renderKidsDayStrip(d, cols, np, nowColumn, nightBefore, weather, elapsedMs, loopMs);
}

// 10:00 in autumn: 12 in the morning, warming up to 19, rain in the evening
static Hour autumnMorning(int h) {
    Hour x = { h <= 15 ? 12 + (h - 10) * 1.4f : 19 - (h - 15) * 1.0f, 0, 15, h < 12 ? 1 : 2 };
    if (h == 19 || h == 20) { x.rainMm = 2.0f; x.code = 63; }
    return x;
}
// 19:00: a dry evening of 15, rain tomorrow morning, a windy afternoon
static Hour autumnEvening(int h) {
    Hour x = { h >= 19 ? 15 - (h - 19) * 0.8f : (h < 7 ? 10.0f : 9 + (h - 7) * 0.8f), 0, 20, h >= 19 ? 0 : 3 };
    if (h >= 7 && h < 12) { x.rainMm = 0.8f; x.code = 61; }
    if (h >= 12 && h < 18) { x.gust = 60; x.code = 1; }
    return x;
}
// 08:00 in winter: snow in the morning, a grey afternoon around freezing, a cold clear evening
static Hour winterMorning(int h) {
    Hour x = { h < 12 ? -3.0f : (h < 16 ? 1.0f : -2.0f), 0, 15, h < 12 ? 73 : (h < 17 ? 3 : 0) };
    if (h < 12) x.rainMm = 0.5f;
    return x;
}
// 10:00 on a wet day: drizzle this morning, heavy rain this afternoon, a downpour with thunder at dinner
static Hour wetDay(int h) {
    if (h < 12) return Hour{ 13, 0.3f, 15, 51 };
    if (h < 18) return Hour{ 15, 3.0f, 20, 63 };
    return Hour{ 12, 8.0f, 30, 95 };
}
// A changeable day: a shower at 16:00, rain at dinner that clears up at 19:00, tomorrow morning sun at first,
// clouds at 10:00 and rain at 11:00
static Hour changeableDay(int h) {
    if (h == 16) return Hour{ 17, 1.5f, 20, 63 };
    if (h == 18) return Hour{ 15, 2.5f, 25, 63 };
    if (h == 10) return Hour{ 12, 0, 20, 3 };
    if (h == 11) return Hour{ 13, 1.0f, 20, 61 };
    return Hour{ h >= 12 && h < 19 ? 17.0f : 11.0f, 0, 15, 0 };
}
// 10:00 in winter: light snow this morning, heavy snow this afternoon, snowing hard at dinner
static Hour snowDay(int h) {
    if (h < 12) return Hour{ -1, 0.3f, 10, 71 };
    if (h < 18) return Hour{ -2, 3.0f, 15, 73 };
    return Hour{ -4, 6.0f, 15, 75 };
}

// 13:00 in summer: hot and sunny, a thunderstorm in the evening
static Hour summerAfternoon(int h) {
    Hour x = { h < 18 ? 28.0f : 22.0f, 0, 20, h < 18 ? 0 : (h < 20 ? 95 : 2) };
    if (h >= 18 && h < 20) x.rainMm = 3.0f;
    return x;
}

static void kidsScreens() {
    // Clothes: a summer afternoon, an autumn morning, an autumn evening
    dayStrip(makeDay(13, 6, 21, summerAfternoon), false);  frame("kids-clothes", 0);
    dayStrip(makeDay(10, 7, 19, autumnMorning), false);    frame("kids-clothes", 1);
    dayStrip(makeDay(19, 7, 19, autumnEvening), false);    frame("kids-clothes", 2);
    // Weather: the same days and a winter day
    dayStrip(makeDay(10, 7, 19, autumnMorning), true);     frame("kids-weather", 0);
    dayStrip(makeDay(8, 8, 17, winterMorning), true);      frame("kids-weather", 1);
    dayStrip(makeDay(13, 6, 21, summerAfternoon), true);   frame("kids-weather", 2);
    dayStrip(makeDay(19, 7, 19, autumnEvening), true);     frame("kids-weather", 3);
    // Dinner at 18:00 seen at noon: in summer, at sunset in autumn, in the dark in winter
    dayStrip(makeDay(13, 6, 21, summerAfternoon), true);   frame("kids-dinner", 0);
    dayStrip(makeDay(12, 7, 18, autumnMorning), true);     frame("kids-dinner", 1);
    dayStrip(makeDay(12, 8, 16, winterMorning), true);     frame("kids-dinner", 2);
    // The night column after dinner: weather and clothes
    dayStrip(makeDay(19, 7, 19, autumnEvening), true, true);   frame("kids-night", 0);
    dayStrip(makeDay(19, 7, 19, autumnEvening), false, true);  frame("kids-night", 1);
    // The sunset evening instead of dinner: a summer afternoon and a winter morning
    dayStrip(makeDay(13, 6, 21, summerAfternoon), true, false, KIDS_SUNSET_EVENING);   frame("kids-sunset", 0);
    dayStrip(makeDay(8, 8, 17, winterMorning), true, false, KIDS_SUNSET_EVENING);      frame("kids-sunset", 1);
    // Rain and snow falling on the numbers, from drizzle to a downpour
    for (int f = 0; f < 45; f++) {
        dayStrip(makeDay(10, 7, 19, wetDay), true, false, KIDS_DINNER_HOUR, f * 66UL, 12000);  frame("kids-rain", f);
    }
    for (int f = 0; f < 45; f++) {
        dayStrip(makeDay(10, 8, 17, snowDay), true, false, KIDS_DINNER_HOUR, f * 66UL, 12000); frame("kids-snow", f);
    }
    // The time-lapse of a changeable day at 15:00, played in 8 s: a shower this afternoon, clearing up after the
    // rain at dinner, tomorrow morning clouding over into rain
    for (int f = 0; f < 122; f++) {
        dayStrip(makeDay(15, 7, 19, changeableDay), true, false, KIDS_DINNER_HOUR, f * 66UL, 8000);
        frame("kids-timelapse", f);
    }
    // The umbrella: a shower this afternoon, a wet dinner, tomorrow morning clouding over into rain
    dayStrip(makeDay(15, 7, 19, changeableDay), false);   frame("kids-umbrella", 0);

    // Leaves blowing over the autumn evening's weather screen
    initWindAnimation(WIND_AREA_KIDS);
    hostSeed(11);
    Day evening = makeDay(19, 7, 19, autumnEvening);
    for (int i = 0; i < 80; i++) {
        updateWindAnimation(30, false, true);
        if (i < 20) continue;   // warm up: let the leaves come in
        dayStrip(evening, true);
        frame("kids-wind", i - 20);
    }
    initWindAnimation(WIND_AREA_KIDS);
}

static void countdowns() {
    hostMillis = 1500;   // confetti on the day itself
    const KidsCountdown c[4] = {
        { true, KIDS_EVENT_BIRTHDAY, 6, 5, 'E' },
        { true, KIDS_EVENT_HALLOWEEN, 12, 0, 0 },
        { true, KIDS_EVENT_SINTERKLAAS, 3, 0, 0 },
        { true, KIDS_EVENT_CHRISTMAS, 0, 0, 0 },
    };
    for (int i = 0; i < 4; i++) {
        renderKidsCountdown(d, c[i], hostMillis);
        frame("kids-countdown", i);
    }
}

// ---- The village ------------------------------------------------------------------------------------------

struct VillageScene {
    Day  day;
    bool night;
    int  condition;      // WEATHER_*
    int  windKmh;
    const char* temp;
    char trend;
};

static void village(const VillageScene& s, int windFrames) {
    KidsHour hours[36];
    for (size_t i = 0; i < s.day.count; i++) {
        const Hour& x = s.day.hours[i];
        int h = (s.day.now + (int)i) % 24;
        hours[i] = KidsHour{ x.t, x.rainMm, x.gust, x.code, h < s.day.sunrise || h >= s.day.sunset, true, KIDS_LIGHT_DAY };
    }
    KidsOutlook o = kidsNowOutlook(hours, s.day.count, LIMITS);
    int part = partOfDay(s.day.now, KIDS_DINNER_HOUR);
    KidsColumn col = {};
    col.part = kidsShownPart(part, true, s.day.now, KIDS_DINNER_HOUR);
    col.valid = o.valid;
    col.outfit = o.outfit;
    col.weather = o.weather;
    col.light = o.light;
    col.temp = o.tempC;
    col.precip = o.precip;
    col.umbrella = o.umbrella;
    bool any = s.day.count > 0;
    initWindAnimation(WIND_AREA_RIDE);
    hostSeed(5);
    bool gusts = s.condition == WEATHER_WIND;
    for (int i = 0; i < windFrames; i++) updateWindAnimation(s.windKmh, gusts, true);
    renderKidsVillageView(d, col, any, s.night, s.condition, 0, s.windKmh, s.temp, s.trend);
}

static Hour windyAutumnMorning(int h) { return Hour{ h < 12 ? 9.0f + (h - 8) : 14.0f, 0, 40, 2 }; }
static Hour hotAfternoon(int h) { return Hour{ h < 19 ? 27.0f : 22.0f, 0, 15, 0 }; }
static Hour coldEvening(int h) { return Hour{ h >= 18 || h < 12 ? 2.0f : 6.0f, 0, 15, h >= 18 || h < 7 ? 0 : 3 }; }

static void villages() {
    VillageScene scenes[3] = {
        { makeDay(8, 7, 19, windyAutumnMorning), false, WEATHER_WIND, 38, "11C", 'u' },
        { makeDay(14, 6, 21, hotAfternoon), false, WEATHER_CLEAR, 10, "27C", 'u' },
        { makeDay(22, 8, 17, coldEvening), true, WEATHER_CLEAR, 8, "2C", 'd' },   // after 22:00: sleeping
    };
    for (int i = 0; i < 3; i++) {
        village(scenes[i], i == 0 ? 10 : 0);
        frame("kids-village", i);
    }
    initWindAnimation(WIND_AREA_RIDE);
}

// ---- The weather report -----------------------------------------------------------------------------------

static void report(const Day& day, int lang, char (*lines)[REPORT_COLS + 1], size_t& count) {
    ReportHour hours[36];
    for (size_t i = 0; i < day.count; i++) {
        const Hour& x = day.hours[i];
        int prob = x.rainMm > 0 ? 80 : 20;
        hours[i] = ReportHour{ (day.now + (int)i) % 24, x.t, x.rainMm, x.gust, prob, x.code, true };
    }
    WeatherReport r;
    weatherReport(hours, day.count, lang, r);
    count = fitReport(r, lines, 7);
}

// 07:00: a fresh sunny morning, rain from 19:00
static Hour freshMorning(int h) {
    float t = h < 7 || h >= 22 ? 9.0f : h < 11 ? 10 + (h - 7) * 1.8f : h < 17 ? 17.0f : 17 - (h - 16) * 0.8f;
    if (h >= 19 && h < 22) return Hour{ t, 1.2f, 20, 61 };
    return Hour{ t, 0, 20, h < 12 ? 0 : 1 };
}
// 20:00: a dry evening, tomorrow a shower around 14:00 and strong gusts
static Hour showerTomorrow(int h) {
    float t = h >= 20 || h < 7 ? 9.0f : h < 14 ? 8 + (h - 7) * 1.1f : 16 - (h - 14) * 0.5f;
    float gust = h >= 12 && h < 18 ? 65.0f : 30.0f;
    if (h == 14 || h == 15) return Hour{ t, 2.0f, gust, 81 };
    return Hour{ t, 0, gust, 2 };
}
// 09:00: rain until 15:00
static Hour rainUntil15(int h) {
    float t = 12 + (h >= 12 && h < 18 ? 2.0f : 0);
    if (h >= 7 && h < 15) return Hour{ t, 1.5f, 30, 63 };
    return Hour{ t, 0, 30, 3 };
}
// 07:00: frost and sun
static Hour frostySun(int h) {
    return Hour{ h < 9 ? -3.0f : h < 15 ? -3 + (h - 9) * 1.2f : 4 - (h - 15) * 0.8f, 0, 15, 0 };
}

static void reports() {
    char lines[7][REPORT_COLS + 1];
    size_t n;
    report(makeDay(7, 7, 19, freshMorning), REPORT_LANG_NL, lines, n);        renderReportView(d, lines, n); frame("report", 0);
    report(makeDay(20, 7, 19, showerTomorrow, 30), REPORT_LANG_NL, lines, n); renderReportView(d, lines, n); frame("report", 1);
    report(makeDay(9, 7, 19, rainUntil15), REPORT_LANG_EN, lines, n);         renderReportView(d, lines, n); frame("report", 2);
    report(makeDay(7, 8, 17, frostySun), REPORT_LANG_EN, lines, n);           renderReportView(d, lines, n); frame("report", 3);

    // The windy evening's report blows away, then the village of a windy autumn morning shows
    report(makeDay(20, 7, 19, showerTomorrow, 30), REPORT_LANG_NL, lines, n);
    int f = 0;
    for (int i = 0; i < 8; i++) { renderReportView(d, lines, n); frame("report-wind", f++); }
    for (int i = 0; renderReportBlowFrame(d, lines, n, i, 1.0f) && i < 60; i++) frame("report-wind", f++);
    for (int i = 0; i < 3; i++) frame("report-wind", f++);   // the empty screen
    VillageScene after = { makeDay(8, 7, 19, windyAutumnMorning), false, WEATHER_WIND, 38, "11C", 'u' };
    village(after, 10);
    for (int i = 0; i < 12; i++) frame("report-wind", f++);
}

int main() {
    kidsScreens();
    countdowns();
    villages();
    reports();
    return 0;
}
