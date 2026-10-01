// MotoWeather Bedside Display — Automatic WiFi Geolocation
// Google Geolocation API implementation for zero configuration setup

#include "geolocation.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define GEOLOCATION_API "https://www.googleapis.com/geolocation/v1/geolocate?key="

bool geolocationConfigured() {
    return strlen(GEOLOCATION_API_KEY) > 0 && strcmp(GEOLOCATION_API_KEY, "YOUR_GOOGLE_API_KEY") != 0;
}

bool geolocateDevice(float &outLat, float &outLon) {
    HTTPClient http;
    WiFiClientSecure client;
    
    client.setInsecure();
    
    // Scan for nearby access points
    int apCount = WiFi.scanNetworks();
    if (apCount < 2) {
        // Need at least 2 visible APs for good accuracy
        WiFi.scanDelete();
        return false;
    }
    
    // Build JSON request body
    JsonDocument doc;
    JsonArray wifiAps = doc["wifiAccessPoints"].to<JsonArray>();
    
    for (int i = 0; i < min(apCount, 10); i++) {
        JsonObject ap = wifiAps.add<JsonObject>();
        ap["macAddress"] = WiFi.BSSIDstr(i);
        ap["signalStrength"] = WiFi.RSSI(i);
    }
    
    WiFi.scanDelete();
    
    // Send request
    http.begin(client, String(GEOLOCATION_API) + GEOLOCATION_API_KEY);
    http.addHeader("Content-Type", "application/json");
    
    String body;
    serializeJson(doc, body);
    
    int httpCode = http.POST(body);
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument response;
        
        DeserializationError error = deserializeJson(response, payload);
        if (!error) {
            outLat = response["location"]["lat"];
            outLon = response["location"]["lng"];
            
            http.end();
            return true;
        }
    }
    
    http.end();
    return false;
}
