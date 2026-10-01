// MotoWeather Bedside Display — Shared Application State
// Single definition of the types and globals shared between main, weather and webserver.

#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>
#include <vector>

// System state - single source of truth (defined in main.cpp)
struct SystemState {
    // Display state
    bool          showTomorrow;
    uint8_t       displayMode;        // 0 = primary view, 1 = weekly matrix
    bool          displayDirty;
    unsigned long weeklyEnteredMs;    // when the weekly view was opened

    // Timing
    unsigned long lastFetchMs;
    unsigned long lastSuccessMs;
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

// WiFi credentials (config.json / web UI)
extern String wifiSsid;
extern String wifiPassword;

// Weather API configuration (config.json / web UI). Plain HTTP only: the ESP8266 is too slow for TLS.
extern String weatherApiUrl;
extern String weatherUnits;   // "metric" or "imperial" (display only; API data is always metric)
extern bool   weatherDebug;

#endif // APP_STATE_H
