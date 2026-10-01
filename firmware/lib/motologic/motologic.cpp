// MotoWeather — pure logic

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

int clothingFor(float tempC, float shortsFromC, float sweaterBelowC) {
    if (isnan(tempC)) return CLOTHES_MILD;
    if (tempC >= shortsFromC) return CLOTHES_WARM;
    if (tempC < sweaterBelowC) return CLOTHES_COOL;
    return CLOTHES_MILD;
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
