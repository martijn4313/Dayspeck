// WeatherWise Bedside Display — Shared Application State
// Single definition of the types and globals shared between main, weather and webserver.

#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>
#include <vector>

// System state - single source of truth (defined in main.cpp)
struct SystemState {
    // Display state
    bool          showTomorrow;
    uint8_t       displayMode;        // 0 = primary view, 1 = weekly matrix, 2 = next hours, 3 = clock
    bool          displayDirty;
    unsigned long weeklyEnteredMs;    // when the weekly / hourly view was opened
    unsigned long lastActivityMs;     // last touch (for the sleep timer)
    bool          displayOff;         // panel switched off (quiet hours / sleep timer)
    bool          previewActive;      // after previewHr the default view is tomorrow

    // Timing
    unsigned long lastFetchMs;
    unsigned long lastSuccessMs;
    unsigned long lastGoodIntervalMs; // refresh interval of the last successful fetch
    uint8_t       fetchFailures;      // consecutive failed fetches (drives the back-off)
    unsigned long nextFetchIntervalMs;
    unsigned long lastFrameMs;
    bool          fetchNow;           // force a fetch on the next loop iteration

    // Environment
    bool          isNight;
    bool          rainAnimationActive;

    // Network state
    bool          wifiConnected;
    bool          everConnected;      // has connected at least once since boot
    unsigned long disconnectedSinceMs; // start of the current WiFi outage (0 = unset); the web UI resets it on new credentials
    int8_t        wifiSignal;
    bool          mdnsStarted;
    bool          apModeStarted;
    bool          ntpStarted;

    // Validity flags
    bool          weatherValid;
    unsigned int  weatherAge;         // minutes since last successful fetch
    bool          weatherStale;       // data older than twice its refresh interval
    bool          timeSynced;         // NTP time is valid
};

extern SystemState state;

struct SsidLocation {
    String ssid;
    float  lat;
    float  lon;
};

// Ride window: start hour (local time) and length in hours
struct RideWindow {
    uint8_t startHour;
    uint8_t hours;
};

extern std::vector<SsidLocation> ssidLocations;

// Location (config.json / web UI)
extern float configLat;
extern float configLon;
extern bool  manualConfigPresent;
extern bool  manualLocation;
extern bool  ssidBasedLocation;

// Display options (config.json "display" and "previewHr")
extern int  previewHr;            // from this local hour the default view is tomorrow; 24 = never
extern bool displayDimAtNight;
extern int  displaySleepMinutes;  // switch the panel off after this many idle minutes at night; 0 = never
extern int  quietStartHr;         // quiet hours: panel off from start (inclusive) to end (exclusive); -1 = off
extern int  quietEndHr;
extern int  displayNightBrightness;   // percent (1-100) used at night when displayDimAtNight is set
extern bool displayAlwaysSleep;   // panel stays off; a touch wakes it for 30 s
extern String displayLanguage;    // kids variant words: "en" (default) or "nl"

// WiFi credentials (config.json / web UI)
extern String wifiSsid;
extern String wifiPassword;

// Weather API configuration (config.json / web UI). Plain HTTP only: the ESP8266 is too slow for TLS.
extern String weatherApiUrl;
extern String weatherUnits;   // "metric" or "imperial" (display only; API data is always metric)
extern bool   weatherDebug;

#endif // APP_STATE_H
