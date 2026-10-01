// MotoWeather — pure logic

#include "motologic.h"
#include <math.h>

char rateRide(const RideThresholds& t, float precipMm, float gustKmh, float tempC) {
    if (precipMm > t.maxRainMm || gustKmh > t.maxWindKmh) {
        return RIDE_DONT;
    }
    if (precipMm > 0 || tempC < t.minTempC || gustKmh > t.warnWindKmh) {
        return RIDE_CAUTION;
    }
    return RIDE_GOOD;
}

char rateWindow(const RideThresholds& t, const float* temp, const float* precip, const float* gust, size_t count) {
    float rain = 0, maxGust = 0, minTemp = 100;
    bool any = false;
    for (size_t i = 0; i < count; i++) {
        if (!isnan(precip[i])) { rain += precip[i]; any = true; }
        if (!isnan(gust[i]) && gust[i] > maxGust) { maxGust = gust[i]; any = true; }
        if (!isnan(temp[i]) && temp[i] < minTemp) { minTemp = temp[i]; any = true; }
    }
    if (!any) {
        return RIDE_UNKNOWN;
    }
    return rateRide(t, rain, maxGust, minTemp);
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
