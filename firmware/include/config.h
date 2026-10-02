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
#define DEFAULT_WEATHER_API_URL API_BASE_URL
#define DEFAULT_WEATHER_UNITS    "metric"
#define DEFAULT_WEATHER_DEBUG    false

// Display (SSD1306 I2C)
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_I2C_ADDR   0x3C
#define OLED_SDA        0
#define OLED_SCL        2

// Touch sensor (GPIO3 = RX pin)
#define TOUCH_PIN       3
// Sensor polarity: 0 = pin is pulled LOW when touched (button/switch to ground, internal pull-up; original behaviour),
// 1 = output goes HIGH when touched (e.g. TTP223 modules in their default mode)
#define TOUCH_ACTIVE_HIGH   0
#define TOUCH_DEBOUNCE_MS   50
#define TOUCH_LONG_PRESS_MS 1000

// Ride decision thresholds (defaults; overridden by config.json)
#define DEFAULT_MAX_RAIN_MM   2.0
#define DEFAULT_MAX_WIND_KMH  60   // gusts above this: do not ride
#define DEFAULT_MIN_TEMP_C    5
#define DEFAULT_WARN_WIND_KMH 40   // gusts above this: caution
#define DEFAULT_PREVIEW_HR    24   // 24 = never switch the default view to tomorrow
#define DEFAULT_RAIN_PROB_PCT 50   // chance of rain (%) from which a ride is at least "caution"; above 100 disables

// Kids variant (build the esp01_1m_kids environment, -DKIDS_MODE): instead of the ride rating the display
// shows what to wear. Shorts (with a t-shirt) from SHORTS_FROM_C on, a sweater below SWEATER_BELOW_C,
// a t-shirt in between.
#define DEFAULT_SHORTS_FROM_C   20
#define DEFAULT_SWEATER_BELOW_C 15

// Display brightness: SSD1306 contrast by day, and the default night brightness (percent, 1-100;
// config.json "display.nightBrightness")
#define DAY_CONTRAST                0xCF   // the Adafruit library's default for SSD1306_SWITCHCAPVCC
#define DEFAULT_NIGHT_BRIGHTNESS_PCT 10

// Fetch interval (milliseconds)
#define FETCH_INTERVAL_MS     900000  // 15 minutes

// Pull updates: base URL of the release relay (tools/ota-relay), plain http:// only.
// "" = not configured; it can also be set in the web UI (config.json "ota.url").
#ifndef OTA_DEFAULT_URL
#define OTA_DEFAULT_URL       ""
#endif

// Version of the config.json layout written by this firmware (bump when keys change)
#define CONFIG_VERSION        1

// On-screen debug status messages: build the esp01_1m_debug environment (-DDISPLAY_STATUS_DEBUG)

#endif // CONFIG_H
