/**
 * Energy monitor: reads the JSON document of docs/energy.md from the local network
 */
#include "energy.h"
#include "config.h"
#include "weather.h"   // logMessage
#include "energyjson.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <limits.h>

#define ENERGY_MIN_FREE_HEAP 12000   // the document is small, but leave the web server room

String energyUrl;

static EnergyData data;
static bool haveData = false;
static unsigned long lastGoodMs = 0;
static unsigned long lastTryMs = 0;
static bool tried = false;
static bool lastFailed = false;
static char lastError[48] = "";

static void fail(const char* what) {
    snprintf(lastError, sizeof(lastError), "%s", what);
    lastFailed = true;
    char buf[96];
    snprintf(buf, sizeof(buf), "Energy monitor: %s", what);
    logMessage(buf);
}

bool energyFetchIfDue(bool showing) {
    if (!showing || energyUrl.length() == 0 || WiFi.status() != WL_CONNECTED) return false;
    unsigned long wait = lastFailed ? ENERGY_RETRY_MS : ENERGY_REFRESH_MS;
    if (tried && millis() - lastTryMs < wait) return false;
    tried = true;
    lastTryMs = millis();
    if (ESP.getFreeHeap() < ENERGY_MIN_FREE_HEAP) {
        fail("skipped, low memory");
        return false;
    }

    WiFiClient client;
    HTTPClient http;
    http.useHTTP10(true);                 // no chunked encoding: parse straight off the stream
    http.setTimeout(ENERGY_TIMEOUT_MS);
    if (!http.begin(client, energyUrl)) {
        fail("bad address");
        return false;
    }
    int code = http.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        JsonDocument filter, doc;
        energyFilter(filter);
        DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        EnergyData fresh;
        if (error) {
            char what[40];
            snprintf(what, sizeof(what), "JSON error %s", error.c_str());
            fail(what);
        } else if (!energyParse(doc.as<JsonVariantConst>(), fresh)) {
            fail("not a JSON object");
        } else {
            data = fresh;
            ok = true;
        }
    } else if (code < 0) {
        char what[40];
        snprintf(what, sizeof(what), "no answer (%s)", http.errorToString(code).c_str());
        fail(what);
    } else {
        char what[24];
        snprintf(what, sizeof(what), "HTTP %d", code);
        fail(what);
    }
    http.end();
    if (ok) {
        haveData = true;
        lastGoodMs = millis();
        lastFailed = false;
        lastError[0] = '\0';
    }
    return ok;
}

void energyForget() {
    haveData = false;
    tried = false;
    lastFailed = false;
    lastError[0] = '\0';
    energyClear(data);
}

const EnergyData& energyData() { return data; }

unsigned long energyAgeMs() { return haveData ? millis() - lastGoodMs : ULONG_MAX; }

bool energyFresh() { return haveData && energyAgeMs() <= ENERGY_STALE_MS; }

const char* energyLastError() { return lastError; }
