// MotoWeather Bedside Display — Device credentials
// One per-device password protects the setup access point and the web UI / OTA updates.

#ifndef SECURITY_H
#define SECURITY_H

#include <Arduino.h>

#define ADMIN_USER          "admin"
#define PASSWORD_MIN_LEN    8     // WPA2 minimum
#define PASSWORD_MAX_LEN    63    // WPA2 maximum

// Password chosen by the user (config.json "auth.password"); empty = use the device default
extern String adminPassword;

// Default password derived from the chip ID, e.g. "moto1a2b3c". Shown on the OLED while the
// setup access point is active. Not secret against someone who knows the device's MAC address:
// change it from the web UI.
String deviceDefaultPassword();

// Password currently in force: adminPassword, or the device default when unset
String effectivePassword();

bool adminPasswordIsDefault();

#endif // SECURITY_H
