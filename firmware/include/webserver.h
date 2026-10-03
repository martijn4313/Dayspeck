// Dayspeck Bedside Display — Embedded Web Server
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESP8266WebServer.h>

// Registers all routes. Every route requires the admin password (HTTP Basic auth); state-changing
// routes additionally require POST plus the per-boot CSRF token in the X-Dayspeck header.
void initWebServer();
// Path of the OTA upload endpoint (contains the per-boot token); valid after initWebServer()
String webOtaPath();
void handleWebServer();

extern ESP8266WebServer server;

#endif // WEBSERVER_H
