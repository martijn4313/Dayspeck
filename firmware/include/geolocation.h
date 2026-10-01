// MotoWeather Bedside Display — Automatic WiFi Geolocation
#ifndef GEOLOCATION_H
#define GEOLOCATION_H

// Locate the device via the Google Geolocation API using nearby WiFi access points.
// Blocks for the duration of a WiFi scan plus one HTTPS request.
bool geolocateDevice(float &outLat, float &outLon);

// True when a real API key has been configured in config.h
bool geolocationConfigured();

#endif // GEOLOCATION_H
