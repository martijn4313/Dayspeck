// WeatherWise Bedside Display — Pull updates from the release relay (see plans/ota_plan.md)
//
// The device fetches <server>/ota-manifest.txt once a day, and on request installs the signed,
// gzip-compressed image it names. Transport is plain HTTP; trust comes from RSA signatures that
// are checked against the public key compiled in from ota_pubkey.h:
//  * the manifest is signed, so its version and image hash cannot be forged
//  * every image is signed, and must also match the hash in the manifest (no older image can be
//    passed off as a newer one)
// Manual uploads in the web UI go through the same signature check when a key is compiled in.
#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

#ifdef KIDS_MODE
#define OTA_VARIANT "kids"
#else
#define OTA_VARIANT "rider"
#endif

// Settings (config.json "ota"; loaded by main.cpp, changed by the web UI)
extern String otaServerUrl;   // http://... base URL of the relay, "" = not configured
extern bool   otaAutoCheck;   // check once a day

struct OtaStatus {
    bool          keySet;            // a public key is compiled in (else pull updates are off)
    bool          checked;           // at least one check has completed (successfully or not)
    unsigned long lastCheckMs;       // millis() of the last check
    bool          available;         // the server offers a newer version for this variant
    String        latestVersion;     // from the last good manifest, "" if none
    String        notes;
    uint32_t      size;              // download size of the image for this variant
    String        error;             // last check or install error, "" if none
};

extern OtaStatus otaStatus;

// Installs the signature check for every firmware update (pull and manual upload). Call in setup().
void otaInit();

// Ask for a check / an install at the next otaLoop() (the web server cannot block for this)
void otaRequestCheck();
void otaRequestInstall();

// Runs requested work and the daily check. Blocks while it talks to the server (a few seconds
// for a check, up to a minute or two for an install, which ends in a restart on success).
// `idle`: nobody is using the device right now, so a scheduled check may pause the display.
void otaLoop(bool idle);

// Free space for an update next to the running firmware
uint32_t otaFreeSpace();

// Draws the install progress (0-100) on the display; set by main.cpp
extern void (*otaProgressHook)(int percent);

#endif // OTA_H
