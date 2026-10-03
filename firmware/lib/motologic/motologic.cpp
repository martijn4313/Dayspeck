// Dayspeck — pure logic

#include "motologic.h"
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
    KidsOutlook o = { false, OUTFIT_MILD, KIDS_WEATHER_CLEAR, false, 0, (int)from };
    float tempSum = 0;
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
    o.outfit = outfitFor(avg, o.weather, o.night, l);
    o.hour = (int)(from + (to - from - 1) / 2);
    return o;
}

KidsOutlook kidsLaterOutlook(const KidsHour* hours, size_t count, size_t window, size_t lookahead,
                             const KidsLimits& l) {
    if (window > count) window = count;
    if (lookahead > count) lookahead = count;
    KidsOutlook win = kidsWindowOutlook(hours, 0, window, l);
    if (!win.valid) return win;

    bool winWet = wetRank(win.weather) > 0;
    int winStep = warmthStep((float)win.tempC, l);
    for (size_t i = window; i < lookahead; i++) {
        const KidsHour& h = hours[i];
        if (!h.valid) continue;
        int w = kidsHourWeather(h, l);
        bool heavy = w == KIDS_WEATHER_STORM || (w == KIDS_WEATHER_SNOW && h.rainMm >= 0.2f) ||
                     (w == KIDS_WEATHER_RAIN && h.rainMm >= 1.0f);
        int step = warmthStep(h.tempC, l);
        if ((heavy && !winWet) || step - winStep >= 2 || winStep - step >= 2) {
            return kidsWindowOutlook(hours, i, i + 1, l);
        }
    }
    return win;
}

int timeOfDay(int localHour) {
    if (localHour >= 6 && localHour < 12) return KIDS_TIME_MORNING;
    if (localHour >= 12 && localHour < 18) return KIDS_TIME_AFTERNOON;
    if (localHour >= 18 && localHour < 22) return KIDS_TIME_EVENING;
    return KIDS_TIME_NIGHT;
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
