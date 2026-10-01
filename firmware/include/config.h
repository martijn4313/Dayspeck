// MotoWeather Bedside Display — Compile-time Configuration
// Edit these values before building

#ifndef CONFIG_H
#define CONFIG_H

// Secrets: copy secrets.h.example to secrets.h (git-ignored) and edit it.
// Without it the device boots with no WiFi credentials and starts its setup access point;
// WiFi can then be configured from the web UI.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID       ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS       ""
#endif

// Open-Meteo API
#define API_BASE_URL        "http://api.open-meteo.com/v1/forecast"

// Weather API configuration (defaults; overridden by config.json)
#define DEFAULT_WEATHER_API_KEY ""
#define DEFAULT_WEATHER_API_URL API_BASE_URL
#define DEFAULT_WEATHER_UNITS    "metric"
#define DEFAULT_WEATHER_DEBUG    false

// Google Geolocation API key (optional). Leave empty to disable automatic geolocation.
// Privacy: when enabled, the BSSID and signal strength of nearby WiFi access points are sent to Google.
#ifndef GEOLOCATION_API_KEY
#define GEOLOCATION_API_KEY ""
#endif

// Display (SSD1306 I2C)
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_I2C_ADDR   0x3C
#define OLED_SDA        0
#define OLED_SCL        2

// Touch sensor (GPIO3 = RX pin)
#define TOUCH_PIN       3
#define TOUCH_DEBOUNCE_MS   50
#define TOUCH_LONG_PRESS_MS 1000

// Ride decision thresholds (defaults; overridden by config.json)
#define DEFAULT_MAX_RAIN_MM   2.0
#define DEFAULT_MAX_WIND_KMH  45
#define DEFAULT_MIN_TEMP_C    5
#define DEFAULT_WARN_WIND_KMH 25

// Fetch interval (milliseconds)
#define FETCH_INTERVAL_MS     900000  // 15 minutes

// Display Debug Status
// Uncomment this define to enable on-screen debug status messages during weather fetching
#define DISPLAY_STATUS_DEBUG

#endif // CONFIG_H
