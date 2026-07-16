// MotoWeather Bedside Display — Embedded Web Server
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESP8266WebServer.h>

void initWebServer();
void handleWebServer();

extern ESP8266WebServer server;

#endif // WEBSERVER_H
