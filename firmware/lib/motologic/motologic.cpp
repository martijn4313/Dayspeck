// Dayspeck — pure logic

#include "motologic.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

char rateRide(const RideThresholds& t, float precipMm, float gustKmh, float tempC, float rainProbPct) {
    if (precipMm > t.maxRainMm || gustKmh > t.maxWindKmh) {
        return RIDE_DONT;
    }
    if (precipMm > 0 || tempC < t.minTempC || gustKmh > t.warnWindKmh ||
        (!isnan(rainProbPct) && rainProbPct >= t.rainProbPct)) {
        return RIDE_CAUTION;
    }
    return RIDE_GOOD;
}

char rateWindow(const RideThresholds& t, const float* temp, const float* precip, const float* gust,
                const float* prob, size_t count) {
    float rain = 0, maxGust = 0, minTemp = 100, maxProb = NAN;
    bool any = false;
    for (size_t i = 0; i < count; i++) {
        if (!isnan(precip[i])) { rain += precip[i]; any = true; }
        if (!isnan(gust[i]) && gust[i] > maxGust) { maxGust = gust[i]; any = true; }
        if (!isnan(temp[i]) && temp[i] < minTemp) { minTemp = temp[i]; any = true; }
        if (prob && !isnan(prob[i]) && (isnan(maxProb) || prob[i] > maxProb)) { maxProb = prob[i]; }
    }
    if (!any) {
        return RIDE_UNKNOWN;
    }
    return rateRide(t, rain, maxGust, minTemp, maxProb);
}

int scoreWindow(float avgTempC, float rainMm, float maxGustKmh) {
    float score = 100.0f - 3.0f * fabsf(avgTempC - 20.0f) - 20.0f * rainMm;
    if (maxGustKmh > 20.0f) score -= 2.0f * (maxGustKmh - 20.0f);
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    return (int)(score + 0.5f);
}

int scoreWindowHours(const float* temp, const float* precip, const float* gust, size_t count) {
    float rain = 0, maxGust = 0, tempSum = 0;
    size_t tempN = 0;
    bool any = false;
    for (size_t i = 0; i < count; i++) {
        if (!isnan(precip[i])) { rain += precip[i]; any = true; }
        if (!isnan(gust[i])) { if (gust[i] > maxGust) maxGust = gust[i]; any = true; }
        if (!isnan(temp[i])) { tempSum += temp[i]; tempN++; any = true; }
    }
    if (!any) return -1;
    return scoreWindow(tempN ? tempSum / tempN : 20.0f, rain, maxGust);   // no temperature: neutral
}

int scoreDay(int amScore, int pmScore, bool weekend) {
    int best = amScore > pmScore ? amScore : pmScore;
    if (best < 0) return -1;
    return best + (weekend ? 15 : 0);
}

int bestDay(const int* scores, size_t count) {
    int best = -1, bestScore = -1;
    for (size_t i = 0; i < count; i++) {
        if (scores[i] > bestScore) { bestScore = scores[i]; best = (int)i; }
    }
    return best;
}

int bestStartHour(const HourSlice* hours, size_t count, size_t windowLen, size_t from, size_t to) {
    if (windowLen == 0 || windowLen > 24) return -1;
    int best = -1, bestScore = -1;
    for (size_t start = from; start < to && start + windowLen <= count; start++) {
        float temp[24], rain[24], gust[24];
        bool complete = true;
        for (size_t i = 0; i < windowLen; i++) {
            const HourSlice& h = hours[start + i];
            if (!h.valid) { complete = false; break; }
            temp[i] = h.tempC;
            rain[i] = h.rainTenthMm / 10.0f;
            gust[i] = h.gustKmh;
        }
        if (!complete) continue;
        int score = scoreWindowHours(temp, rain, gust, windowLen);
        if (score > bestScore) { bestScore = score; best = (int)start; }
    }
    return best;
}

int kidsWeatherFor(int code, float windKmh, float warnWindKmh) {
    if (code >= 95) return KIDS_WEATHER_STORM;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return KIDS_WEATHER_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return KIDS_WEATHER_SNOW;
    if (windKmh > warnWindKmh) return KIDS_WEATHER_WIND;
    if (code == 2) return KIDS_WEATHER_PARTLY;
    if (code == 3 || code == 45 || code == 48) return KIDS_WEATHER_CLOUDY;   // overcast, fog
    return KIDS_WEATHER_CLEAR;
}

uint8_t contrastForPercent(int percent) {
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    return (uint8_t)((percent * 255 + 50) / 100);
}

int warmthStep(float tempC, const KidsLimits& l) {
    if (isnan(tempC)) return 2;
    if (tempC >= l.hotFromC) return 0;
    if (tempC >= l.shortsFromC) return 1;
    if (tempC >= l.sweaterBelowC) return 2;
    if (tempC >= l.coatBelowC) return 3;
    if (tempC >= l.freezeBelowC) return 4;
    return 5;
}

int outfitFor(float tempC, int kidsWeather, bool night, const KidsLimits& l) {
    int step = warmthStep(tempC, l);
    if (kidsWeather == KIDS_WEATHER_SNOW || step == 5) return OUTFIT_FREEZING;
    if (step == 4) return OUTFIT_COLD;
    if (kidsWeather == KIDS_WEATHER_RAIN || kidsWeather == KIDS_WEATHER_STORM) return OUTFIT_RAIN;
    if (step == 3) return OUTFIT_COOL;
    if (step == 2) return OUTFIT_MILD;
    bool sunny = kidsWeather == KIDS_WEATHER_CLEAR || kidsWeather == KIDS_WEATHER_PARTLY;
    if (step == 0 && sunny && !night) return OUTFIT_HOT;
    return OUTFIT_WARM;
}

int kidsHourWeather(const KidsHour& h, const KidsLimits& l) {
    int code = h.code >= 0 ? h.code : (h.rainMm >= 0.2f ? 61 : 0);
    return kidsWeatherFor(code, h.gustKmh, l.windyGustKmh);
}

// Precipitation that counts: rain or snow of at least 0.2 mm, or a storm
static bool isWet(const KidsHour& h, int weather) {
    if (weather == KIDS_WEATHER_STORM) return true;
    return (weather == KIDS_WEATHER_RAIN || weather == KIDS_WEATHER_SNOW) && h.rainMm >= 0.2f;
}

static int wetRank(int weather) {
    return weather == KIDS_WEATHER_STORM ? 3 : weather == KIDS_WEATHER_SNOW ? 2 : weather == KIDS_WEATHER_RAIN ? 1 : 0;
}

KidsOutlook kidsWindowOutlook(const KidsHour* hours, size_t from, size_t to, const KidsLimits& l) {
    KidsOutlook o = { false, OUTFIT_MILD, KIDS_WEATHER_CLEAR, false, 0, 0, (int)from };
    float tempSum = 0, tempMax = -1000;
    int n = 0, nights = 0, wettest = KIDS_WEATHER_CLEAR;
    int skyCount[7] = { 0, 0, 0, 0, 0, 0, 0 };
    for (size_t i = from; i < to; i++) {
        const KidsHour& h = hours[i];
        if (!h.valid) continue;
        int w = kidsHourWeather(h, l);
        if (isWet(h, w)) {
            if (wetRank(w) > wetRank(wettest)) wettest = w;
        } else if (wetRank(w) == 0) {
            skyCount[w]++;
        } else {
            skyCount[KIDS_WEATHER_CLOUDY]++;   // a trace of rain: just clouds
        }
        tempSum += h.tempC;
        if (h.tempC > tempMax) tempMax = h.tempC;
        if (h.night) nights++;
        n++;
    }
    if (n == 0) return o;

    o.valid = true;
    o.night = nights * 2 > n;
    if (wetRank(wettest) > 0) {
        o.weather = wettest;
    } else {
        static const int sky[4] = { KIDS_WEATHER_CLEAR, KIDS_WEATHER_PARTLY, KIDS_WEATHER_CLOUDY, KIDS_WEATHER_WIND };
        int best = KIDS_WEATHER_CLEAR;
        for (int w : sky) {
            if (skyCount[w] > skyCount[best] || (w == KIDS_WEATHER_WIND && skyCount[w] > 0)) best = w;
        }
        o.weather = best;
    }
    float avg = tempSum / n;
    o.tempC = (int)lroundf(avg);
    o.maxTempC = (int)lroundf(tempMax);
    o.outfit = outfitFor(avg, o.weather, o.night, l);
    o.hour = (int)(from + (to - from - 1) / 2);
    return o;
}

KidsOutlook kidsPartOutlook(const KidsHour* hours, const KidsPart& part, const KidsLimits& l) {
    KidsOutlook o = kidsWindowOutlook(hours, part.from, part.to, l);
    if (!o.valid) return o;
    float t = NAN;
    for (size_t i = part.from; i < part.to; i++) {
        const KidsHour& h = hours[i];
        if (!h.valid || isnan(h.tempC)) continue;
        if (isnan(t)) {
            t = h.tempC;
            if (part.part == KIDS_PART_EVENING) break;          // the start of the evening
        } else if (part.part == KIDS_PART_MORNING) {
            if (h.tempC < t) t = h.tempC;                       // the coldest of the morning
        } else if (h.tempC > t) {
            t = h.tempC;                                        // the warmest of the afternoon
        }
    }
    if (isnan(t)) return o;
    o.tempC = (int)lroundf(t);
    o.outfit = outfitFor(t, o.weather, o.night, l);
    return o;
}

int partOfDay(int localHour) {
    if (localHour >= KIDS_MORNING_FROM_HR && localHour < KIDS_AFTERNOON_FROM_HR) return KIDS_PART_MORNING;
    if (localHour >= KIDS_AFTERNOON_FROM_HR && localHour < KIDS_EVENING_FROM_HR) return KIDS_PART_AFTERNOON;
    if (localHour >= KIDS_EVENING_FROM_HR && localHour < KIDS_EVENING_UNTIL_HR) return KIDS_PART_EVENING;
    return -1;
}

size_t kidsDayParts(const int* localHours, size_t count, KidsPart* out, size_t maxParts) {
    size_t n = 0;
    bool night = false;
    for (size_t i = 0; i < count && n < maxParts;) {
        int part = partOfDay(localHours[i]);
        if (part < 0) {          // a night hour: whatever comes next is after sleeping
            night = true;
            i++;
            continue;
        }
        size_t from = i;
        while (i < count && partOfDay(localHours[i]) == part) i++;
        out[n++] = KidsPart{ part, from, i, night, from == 0 };
        night = false;
    }
    return n;
}

int mapWeatherCode(int code, float windKmh, float warnWindKmh) {
    int condition = WEATHER_CLEAR;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95) {
        condition = WEATHER_RAIN;      // drizzle, rain, showers, thunderstorm
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        condition = WEATHER_SNOW;      // snow, snow grains, snow showers
    }
    // Clear, cloudy (1-3) and fog (45, 48): show wind if it is strong
    if (condition == WEATHER_CLEAR && windKmh > warnWindKmh) {
        condition = WEATHER_WIND;
    }
    return condition;
}

char temperatureTrend(float now, float later) {
    if (isnan(now) || isnan(later)) return 'f';
    if (later - now >= 1.0f) return 'u';
    if (now - later >= 1.0f) return 'd';
    return 'f';
}

bool kidsLimitsValid(const KidsLimits& l) {
    if (isnan(l.hotFromC) || isnan(l.shortsFromC) || isnan(l.sweaterBelowC) || isnan(l.coatBelowC) ||
        isnan(l.freezeBelowC) || isnan(l.windyGustKmh)) return false;
    return l.freezeBelowC <= l.coatBelowC && l.coatBelowC <= l.sweaterBelowC &&
           l.sweaterBelowC <= l.shortsFromC && l.shortsFromC <= l.hotFromC && l.windyGustKmh > 0;
}

// ---- Weather report ----

static bool reportStorm(int code) { return code >= 95; }
static bool reportSnow(int code) { return (code >= 71 && code <= 77) || code == 85 || code == 86; }
static bool reportDrizzle(int code) { return code >= 51 && code <= 57; }

// Precipitation that counts, as on the kids screens: at least 0.2 mm of rain or snow, or a storm
static bool reportWet(const ReportHour& h) {
    return reportStorm(h.code) || (h.rainMm >= 0.2f && (h.code < 0 || h.code >= 51));
}

enum { SKY_CLEAR, SKY_PARTLY, SKY_CLOUDY, SKY_FOG };
static int reportSky(int code) {
    if (code <= 1) return SKY_CLEAR;   // unknown counts as clear, as on the kids screens
    if (code == 2) return SKY_PARTLY;
    if (code == 45 || code == 48) return SKY_FOG;
    return SKY_CLOUDY;
}

struct ReportSummary {
    int   n, wet, storm, snow, drizzle, probMax, firstWet, lastWet, lastHour;
    int   sky[4];
    float tmin, tmax, rainSum, gustMax, morningMin, afternoonMax;
};

// Summary of the valid hours [from, to)
static ReportSummary summarizeReport(const ReportHour* hours, size_t from, size_t to) {
    ReportSummary s = { 0, 0, 0, 0, 0, 0, -1, -1, -1, { 0, 0, 0, 0 }, 1000, -1000, 0, 0, 1000, -1000 };
    for (size_t i = from; i < to; i++) {
        const ReportHour& h = hours[i];
        if (!h.valid || isnan(h.tempC)) continue;
        s.n++;
        s.lastHour = h.hour;
        if (h.tempC < s.tmin) s.tmin = h.tempC;
        if (h.tempC > s.tmax) s.tmax = h.tempC;
        if (h.hour >= KIDS_MORNING_FROM_HR && h.hour < KIDS_AFTERNOON_FROM_HR && h.tempC < s.morningMin) s.morningMin = h.tempC;
        if (h.hour >= KIDS_AFTERNOON_FROM_HR && h.hour < KIDS_EVENING_FROM_HR && h.tempC > s.afternoonMax) s.afternoonMax = h.tempC;
        if (!isnan(h.rainMm)) s.rainSum += h.rainMm;
        if (!isnan(h.gustKmh) && h.gustKmh > s.gustMax) s.gustMax = h.gustKmh;
        if (h.prob > s.probMax) s.probMax = h.prob;
        if (reportWet(h)) {
            s.wet++;
            if (s.firstWet < 0) s.firstWet = h.hour;
            s.lastWet = h.hour;
            if (reportStorm(h.code)) s.storm++;
            if (reportSnow(h.code)) s.snow++;
            if (reportDrizzle(h.code)) s.drizzle++;
        } else {
            s.sky[reportSky(h.code)]++;
        }
    }
    return s;
}

// The words, English and Dutch
enum { W_TODAY, W_TOMORROW, W_SUNNY, W_SUN_CLOUDS, W_CLOUDY, W_FOGGY,
       W_DRY, W_MOSTLY_DRY, W_STORM, W_STORM_CHANCE, W_SNOW, W_SNOW_SOME, W_DRIZZLE_SOME, W_SHOWER_SOME,
       W_SHOWERS, W_DRIZZLE, W_RAIN, W_HEAVY_RAIN, W_COLD, W_CHILLY, W_COOL, W_COUNT };
static const char* const REPORT_WORDS[2][W_COUNT] = {
    { "Today", "Tomorrow", "sunny", "sun and clouds", "cloudy", "foggy",
      "dry", "mostly dry", "thunderstorms", "chance of thunder", "snow", "snow at times", "drizzle at times",
      "a shower at times", "showers", "drizzle", "rain", "heavy rain", "Cold", "Chilly", "Cool" },
    { "Vandaag", "Morgen", "zonnig", "zon en wolken", "bewolkt", "mistig",
      "droog", "overwegend droog", "onweer", "kans op onweer", "sneeuw", "soms sneeuw", "soms motregen",
      "soms een bui", "buien", "motregen", "regen", "veel regen", "Koude", "Frisse", "Koele" },
};

static int skyWord(const ReportSummary& s) {
    int best = SKY_CLEAR;
    for (int k = 1; k < 4; k++) if (s.sky[k] > s.sky[best]) best = k;
    if (s.sky[best] == 0) return W_CLOUDY;   // wet all day
    // Sun and clouds when the clear hours do not outnumber the cloudy ones
    if (best == SKY_CLEAR && s.sky[SKY_CLOUDY] + s.sky[SKY_PARTLY] > s.sky[SKY_CLEAR]) return W_SUN_CLOUDS;
    static const int words[4] = { W_SUNNY, W_SUN_CLOUDS, W_CLOUDY, W_FOGGY };
    return words[best];
}

static int precipWord(const ReportSummary& s) {
    if (s.wet == 0) return s.probMax >= 30 ? W_MOSTLY_DRY : W_DRY;
    bool most = s.wet * 2 >= s.n;
    if (s.storm) return most ? W_STORM : W_STORM_CHANCE;
    if (s.snow) return most ? W_SNOW : W_SNOW_SOME;
    if (s.wet <= 2) return s.drizzle == s.wet ? W_DRIZZLE_SOME : W_SHOWER_SOME;
    if (!most) return W_SHOWERS;
    if (s.drizzle * 2 >= s.wet) return W_DRIZZLE;
    return s.rainSum >= 5 ? W_HEAVY_RAIN : W_RAIN;
}

// Precipitation words that describe the whole day on their own (no sky word in front)
static bool precipAlone(int word) {
    return word == W_STORM || word == W_SNOW || word == W_DRIZZLE || word == W_RAIN || word == W_HEAVY_RAIN;
}

static int roundTemp(float t) { return (int)lroundf(t); }

void weatherReport(const ReportHour* hours, size_t count, int lang, WeatherReport& out) {
    out.count = 0;
    if (count == 0) return;
    bool nl = lang == REPORT_LANG_NL;
    const char* const* w = REPORT_WORDS[nl ? 1 : 0];
    const char deg = REPORT_DEGREE;

    // The stretch the report is about: from now (or the next 07:00) until 22:00. From 18:00 that is tomorrow.
    // The night hours before it only count for frost.
    int now = hours[0].hour;
    bool tomorrow = now >= KIDS_EVENING_FROM_HR;
    size_t dayFrom = 0;
    if (tomorrow || now < KIDS_MORNING_FROM_HR) {
        while (dayFrom < count && hours[dayFrom].hour != KIDS_MORNING_FROM_HR) dayFrom++;
    }
    size_t dayTo = dayFrom;
    while (dayTo < count && hours[dayTo].hour >= KIDS_MORNING_FROM_HR && hours[dayTo].hour < KIDS_EVENING_UNTIL_HR) {
        dayTo++;
    }
    ReportSummary s = summarizeReport(hours, dayFrom, dayTo);
    if (s.n == 0) return;
    ReportSummary night = summarizeReport(hours, 0, dayFrom);

    // A change during the day (rain that starts later, or stops): then the first sentence names only the sky
    char change[REPORT_SENTENCE_LEN] = "";
    if (s.wet > 0 && s.wet < s.n) {
        const ReportHour* first = nullptr;
        for (size_t i = dayFrom; i < dayTo && !first; i++) if (hours[i].valid && !isnan(hours[i].tempC)) first = &hours[i];
        bool wetNow = first && reportWet(*first);
        bool mostlyDrizzle = s.drizzle * 2 >= s.wet;
        if (!wetNow && s.wet <= 2) {
            if (nl) {
                const char* what = s.storm ? "kans op onweer" : s.snow ? "wat sneeuw" : mostlyDrizzle ? "wat motregen" : "een bui";
                snprintf(change, sizeof(change), "Rond %d uur %s.", s.firstWet, what);
            } else {
                const char* what = s.storm ? "Chance of thunder" : s.snow ? "Some snow" : mostlyDrizzle ? "Some drizzle" : "A shower";
                snprintf(change, sizeof(change), "%s around %d:00.", what, s.firstWet);
            }
        } else {
            const char* what = nl ? (s.storm ? "onweer" : s.snow ? "sneeuw" : mostlyDrizzle ? "motregen" : "regen")
                                  : (s.storm ? "Thunderstorms" : s.snow ? "Snow" : mostlyDrizzle ? "Drizzle" : "Rain");
            if (!wetNow) {
                if (nl) snprintf(change, sizeof(change), "Vanaf %d uur %s.", s.firstWet, what);
                else snprintf(change, sizeof(change), "%s from %d:00.", what, s.firstWet);
            } else if (s.lastWet + 1 < s.lastHour) {   // at least two dry hours at the end
                if (nl) snprintf(change, sizeof(change), "Tot %d uur %s, daarna droog.", s.lastWet + 1, what);
                else snprintf(change, sizeof(change), "%s until %d:00, then dry.", what, s.lastWet + 1);
            }
        }
    }

    // 1. The day: the weather and the temperature
    char weather[40];
    int sky = skyWord(s), pre = precipWord(s);
    if (change[0]) snprintf(weather, sizeof(weather), "%s", w[sky]);
    else if (precipAlone(pre)) snprintf(weather, sizeof(weather), "%s", w[pre]);
    else snprintf(weather, sizeof(weather), "%s, %s", w[sky], w[pre]);
    const char* when = w[tomorrow ? W_TOMORROW : W_TODAY];
    int lo = roundTemp(s.tmin), hi = roundTemp(s.tmax);
    char (*line)[REPORT_SENTENCE_LEN] = out.sentences;
    bool freshMorning = s.morningMin < 999 && s.afternoonMax > -999 && s.afternoonMax - s.morningMin >= 5;
    if (freshMorning) {
        int am = roundTemp(s.morningMin), pm = roundTemp(s.afternoonMax);
        const char* morning = w[am <= 0 ? W_COLD : am < 12 ? W_CHILLY : W_COOL];
        snprintf(line[out.count++], REPORT_SENTENCE_LEN, "%s %s.", when, weather);
        if (nl) snprintf(line[out.count++], REPORT_SENTENCE_LEN, "%s ochtend (%d%c), 's middags %d%c.", morning, am, deg, pm, deg);
        else snprintf(line[out.count++], REPORT_SENTENCE_LEN, "%s morning (%d%c), %d%c in the afternoon.", morning, am, deg, pm, deg);
    } else if (hi - lo <= 2) {
        snprintf(line[out.count++], REPORT_SENTENCE_LEN, nl ? "%s %s, rond %d%c." : "%s %s, around %d%c.", when, weather, hi, deg);
    } else {
        snprintf(line[out.count++], REPORT_SENTENCE_LEN, nl ? "%s %s, %d tot %d%c." : "%s %s, %d to %d%c.", when, weather, lo, hi, deg);
    }

    // 2. The change
    if (change[0]) snprintf(line[out.count++], REPORT_SENTENCE_LEN, "%s", change);

    // 3. One thing to watch out for
    char* watch = line[out.count];
    if (night.n && night.tmin <= 0) snprintf(watch, REPORT_SENTENCE_LEN, nl ? "Vannacht vorst, kans op gladheid." : "Frost tonight, roads may be icy.");
    else if (s.tmin <= 0) snprintf(watch, REPORT_SENTENCE_LEN, nl ? "Kans op gladheid." : "Roads may be icy.");
    else if (s.gustMax >= 60) snprintf(watch, REPORT_SENTENCE_LEN, nl ? "Harde windvlagen tot %d km/u." : "Strong gusts up to %d km/h.", roundTemp(s.gustMax));
    else if (s.rainSum >= 10 && pre == W_HEAVY_RAIN && !change[0])   // the first sentence already says it
        snprintf(watch, REPORT_SENTENCE_LEN, nl ? "In totaal %d mm." : "%d mm in total.", roundTemp(s.rainSum));
    else if (s.rainSum >= 10) snprintf(watch, REPORT_SENTENCE_LEN, nl ? "Veel regen: %d mm." : "Lots of rain: %d mm.", roundTemp(s.rainSum));
    else if (s.gustMax >= 45) snprintf(watch, REPORT_SENTENCE_LEN, nl ? "Stevige wind." : "Quite windy.");
    else watch = nullptr;
    if (watch) out.count++;
}

size_t wrapReport(const WeatherReport& r, size_t sentences, char (*lines)[REPORT_COLS + 1], size_t maxLines) {
    size_t n = 0, len = 0;
    char line[REPORT_COLS + 1];
    auto flush = [&]() {
        line[len] = '\0';
        if (n < maxLines) memcpy(lines[n], line, len + 1);
        n++;
        len = 0;
    };
    for (size_t k = 0; k < sentences && k < r.count; k++) {
        const char* p = r.sentences[k];
        while (*p) {
            while (*p == ' ') p++;
            size_t wl = 0;
            while (p[wl] && p[wl] != ' ') wl++;
            if (wl == 0) break;
            while (wl > 0) {
                if (len > 0 && len + 1 + wl > REPORT_COLS) flush();
                if (len > 0) line[len++] = ' ';
                size_t take = wl > REPORT_COLS - len ? REPORT_COLS - len : wl;   // a word longer than a line is split
                memcpy(line + len, p, take);
                len += take;
                p += take;
                wl -= take;
                if (wl > 0) flush();
            }
        }
        if (len > 0) flush();   // every sentence starts on a new line
    }
    return n;
}

static const char* const SCREEN_NAMES[SCREEN_COUNT] = {
    "ride", "rideOther", "week", "hours", "clock", "weather", "clothes", "countdown", "report"
};

const char* screenName(int id) {
    return (id >= 0 && id < SCREEN_COUNT) ? SCREEN_NAMES[id] : "";
}

int screenFromName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (strcmp(name, SCREEN_NAMES[i]) == 0) return i;
    }
    return -1;
}

bool screenAlwaysAvailable(int id) {
    return id >= 0 && id < SCREEN_COUNT && id != SCREEN_CLOCK && id != SCREEN_COUNTDOWN;
}

bool screenListsValid(const ScreenList& tap, const ScreenList& hold) {
    if (tap.count < 1 || tap.count > MAX_SCREEN_SLOTS || hold.count > MAX_SCREEN_SLOTS) return false;
    if (!screenAlwaysAvailable(tap.ids[0])) return false;
    for (int i = 0; i < tap.count; i++) if (tap.ids[i] >= SCREEN_COUNT) return false;
    for (int i = 0; i < hold.count; i++) if (hold.ids[i] >= SCREEN_COUNT) return false;
    return true;
}

int screenAt(ScreenNav nav, const ScreenList& tap, const ScreenList& hold) {
    const ScreenList& l = nav.list ? hold : tap;
    if (nav.slot < l.count) return l.ids[nav.slot];
    return tap.count ? tap.ids[0] : SCREEN_RIDE;
}

ScreenNav screenNextTap(ScreenNav nav, const ScreenList& tap, uint16_t available) {
    ScreenNav home = { 0, 0 };
    if (nav.list != 0 || tap.count == 0) return home;      // from the hold list a tap goes home
    for (int k = 1; k <= tap.count; k++) {
        int i = (nav.slot + k) % tap.count;
        if (i == 0 || (available & (1u << tap.ids[i]))) return ScreenNav{ 0, (uint8_t)i };
    }
    return home;
}

ScreenNav screenNextHold(ScreenNav nav, const ScreenList& tap, const ScreenList& hold, uint16_t available) {
    if (hold.count == 0) return screenNextTap(nav, tap, available);   // no hold list: a long press is a tap
    int start = nav.list == 0 ? 0 : nav.slot + 1;
    for (int i = start; i < hold.count; i++) {
        if (available & (1u << hold.ids[i])) return ScreenNav{ 1, (uint8_t)i };
    }
    return ScreenNav{ 0, 0 };                              // past the last one: home
}

bool isAutumn(int month, bool southern) {
    return southern ? (month >= 3 && month <= 5) : (month >= 9 && month <= 11);
}

bool leavesBlowing(bool autumn, int condition, float windKmh) {
    if (!autumn || isnan(windKmh)) return false;
    if (condition != WEATHER_CLEAR && condition != WEATHER_WIND) return false;
    return windKmh >= LEAF_MIN_WIND_KMH;
}

bool kidsLeavesBlowing(bool autumn, int kidsWeather, float windKmh) {
    if (!autumn || isnan(windKmh)) return false;
    bool dry = kidsWeather == KIDS_WEATHER_CLEAR || kidsWeather == KIDS_WEATHER_PARTLY ||
               kidsWeather == KIDS_WEATHER_CLOUDY || kidsWeather == KIDS_WEATHER_WIND;
    return dry && windKmh >= LEAF_MIN_WIND_KMH;
}

char kidsInitial(const char* text) {
    if (text == nullptr) return 0;
    char c = text[0];
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return (c >= 'A' && c <= 'Z') ? c : 0;
}

long daysFromCivil(int year, int month, int day) {
    // Howard Hinnant's days_from_civil
    year -= month <= 2;
    long era = (year >= 0 ? year : year - 399) / 400;
    long yoe = year - era * 400;
    long doy = (153L * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int daysInMonth(int year, int month) {
    static const uint8_t DAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (month == 2 && isLeapYear(year)) ? 29 : DAYS[month - 1];
}

bool parseIsoDate(const char* text, int& year, int& month, int& day) {
    if (text == nullptr) return false;
    for (int i = 0; i < 10; i++) {
        bool dash = (i == 4 || i == 7);
        if (dash ? text[i] != '-' : (text[i] < '0' || text[i] > '9')) return false;
    }
    if (text[10] != '\0') return false;
    int y = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
    int m = (text[5] - '0') * 10 + (text[6] - '0');
    int d = (text[8] - '0') * 10 + (text[9] - '0');
    if (y < 1900 || y > 2100 || m < 1 || m > 12 || d < 1 || d > daysInMonth(y, m)) return false;
    year = y;
    month = m;
    day = d;
    return true;
}

int daysUntilNext(int year, int month, int day, int onMonth, int onDay, int& occurrenceYear) {
    long today = daysFromCivil(year, month, day);
    for (int y = year; ; y++) {
        int d = (onMonth == 2 && onDay == 29 && !isLeapYear(y)) ? 28 : onDay;
        long when = daysFromCivil(y, onMonth, d);
        if (when >= today) {
            occurrenceYear = y;
            return (int)(when - today);
        }
    }
}

KidsCountdown nextKidsCountdown(int year, int month, int day, const KidsBirthday* birthdays, size_t count,
                                unsigned holidays, int withinDays) {
    KidsCountdown best = { false, 0, 0, 0, 0 };
    for (size_t i = 0; i < count; i++) {
        const KidsBirthday& b = birthdays[i];
        if (b.month < 1 || b.month > 12 || b.day < 1) continue;
        if (daysFromCivil(b.year, b.month, b.day) > daysFromCivil(year, month, day)) continue;   // not born yet
        int when = 0;
        int sleeps = daysUntilNext(year, month, day, b.month, b.day, when);
        if (sleeps <= withinDays && (!best.active || sleeps < best.sleeps)) {
            best = { true, KIDS_EVENT_BIRTHDAY, sleeps, when - b.year, b.initial };
        }
    }
    static const int8_t HOLIDAYS[][3] = {   // kind, month, day
        { KIDS_EVENT_HALLOWEEN, 10, 31 }, { KIDS_EVENT_SINTERKLAAS, 12, 5 }, { KIDS_EVENT_CHRISTMAS, 12, 25 },
    };
    for (const auto& h : HOLIDAYS) {
        if (!(holidays & KIDS_HOLIDAY(h[0]))) continue;
        int when = 0;
        int sleeps = daysUntilNext(year, month, day, h[1], h[2], when);
        if (sleeps <= withinDays && (!best.active || sleeps < best.sleeps)) {
            best = { true, h[0], sleeps, 0, 0 };
        }
    }
    return best;
}

bool isNightAt(long now, long sunrise, long sunset) {
    long days = (now >= sunrise) ? (now - sunrise) / 86400 : 0;
    long rise = sunrise + days * 86400;
    long set = sunset + days * 86400;
    return now < rise || now > set;
}

uint8_t dayOfWeek(long localMidnightUtc, long utcOffsetSeconds) {
    // 1970-01-01 was a Thursday (4)
    return (uint8_t)((((localMidnightUtc + utcOffsetSeconds) / 86400) + 4) % 7);
}

bool intervalElapsed(uint32_t now, uint32_t last, uint32_t interval) {
    return (now - last) >= interval;   // unsigned subtraction is correct across the 49 day rollover
}

bool parseVersion(const char* text, uint16_t out[3]) {
    if (text == nullptr) return false;
    if (*text == 'v') text++;
    for (int part = 0; part < 3; part++) {
        if (*text < '0' || *text > '9') return false;
        uint32_t value = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (uint32_t)(*text - '0');
            if (value > 65535) return false;
            text++;
        }
        out[part] = (uint16_t)value;
        if (part < 2 && *text++ != '.') return false;
    }
    return *text == '\0';
}

bool isNewerVersion(const char* candidate, const char* current) {
    uint16_t a[3], b[3];
    if (!parseVersion(candidate, a) || !parseVersion(current, b)) return false;
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hexToBytes(const char* hex, uint8_t* out, size_t len) {
    if (hex == nullptr) return false;
    for (size_t i = 0; i < len; i++) {
        int hi = hexDigit(hex[2 * i]);
        if (hi < 0) return false;
        int lo = hexDigit(hex[2 * i + 1]);
        if (lo < 0) return false;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return hex[2 * len] == '\0';
}
