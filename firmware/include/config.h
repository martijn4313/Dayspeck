// Dayspeck Bedside Display — Compile-time Configuration
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

// Kids screens (weather and clothes for the next parts of the day). Outfits by temperature (C), from warm
// to cold. These are the defaults: config.json ("kids") and the web UI ("Clothing") override them.
#define KIDS_HOT_FROM_C        25   // sun cap, t-shirt and shorts (sunny daytime only)
#define KIDS_SHORTS_FROM_C     20   // t-shirt and shorts
#define KIDS_SWEATER_BELOW_C   15   // sweater below this, a t-shirt above
#define KIDS_COAT_BELOW_C       5   // winter coat and scarf
#define KIDS_FREEZE_BELOW_C     0   // winter coat, scarf, hat and mittens
#define KIDS_WINDY_GUST_KMH    50   // gusts above this show the wind picture
// Rain or a storm gives the rain coat and boots (above KIDS_COAT_BELOW_C), snow the full winter outfit.
// The screens show the next three parts of the day: morning 07-12, afternoon 12 to dinner time, dinner
// until 22 and, if switched on, the night (KIDS_*_HR in firmware/lib/motologic/motologic.h).
#define KIDS_DINNER_HOUR       18   // dinner time (15-21): the dinner column's temperature and light
#define KIDS_NIGHT_COLUMN   false   // after dinner: dinner, night, tomorrow morning
#define KIDS_SUNSET_COLUMN  false   // true: the sunset evening (18-22) instead of dinner

// Display brightness: SSD1306 contrast by day, and the default night brightness (percent, 1-100;
// config.json "display.nightBrightness")
#define DAY_CONTRAST                0xCF   // the Adafruit library's default for SSD1306_SWITCHCAPVCC
#define DEFAULT_NIGHT_BRIGHTNESS_PCT 10

// Screens: back to the home screen (the first of the tap list) after this many seconds; 0 = never.
// Which screens there are is set in config.json "display.screens" or the web UI (Screens card).
#define DEFAULT_SCREENS_RETURN_SEC 30

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
