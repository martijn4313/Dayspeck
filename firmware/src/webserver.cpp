// MotoWeather Bedside Display — Embedded Web Server Implementation
#include "webserver.h"
#include "config.h"
#include "weather.h"
#include "app_state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

ESP8266WebServer server(80);

// Predefined locations: countries and cities with lat/lon
const char* locationsJson = R"JSON(
{
  "United States": {
    "New York": {"lat": 40.7128, "lon": -74.0060},
    "Los Angeles": {"lat": 34.0522, "lon": -118.2437},
    "Chicago": {"lat": 41.8781, "lon": -87.6298},
    "Houston": {"lat": 29.7604, "lon": -95.3698},
    "Phoenix": {"lat": 33.4484, "lon": -112.0740}
  },
  "United Kingdom": {
    "London": {"lat": 51.5074, "lon": -0.1278},
    "Manchester": {"lat": 53.4808, "lon": -2.2426},
    "Birmingham": {"lat": 52.4862, "lon": -1.8904},
    "Glasgow": {"lat": 55.8642, "lon": -4.2518},
    "Liverpool": {"lat": 53.4084, "lon": -2.9916}
  },
  "Germany": {
    "Berlin": {"lat": 52.5200, "lon": 13.4050},
    "Munich": {"lat": 48.1351, "lon": 11.5820},
    "Hamburg": {"lat": 53.5511, "lon": 9.9937},
    "Cologne": {"lat": 50.9375, "lon": 6.9603},
    "Frankfurt": {"lat": 50.1109, "lon": 8.6821}
  },
  "France": {
    "Paris": {"lat": 48.8566, "lon": 2.3522},
    "Marseille": {"lat": 43.2965, "lon": 5.3698},
    "Lyon": {"lat": 45.7640, "lon": 4.8357},
    "Toulouse": {"lat": 43.6047, "lon": 1.4442},
    "Nice": {"lat": 43.7102, "lon": 7.2620}
  },
  "Italy": {
    "Rome": {"lat": 41.9028, "lon": 12.4964},
    "Milan": {"lat": 45.4642, "lon": 9.1900},
    "Naples": {"lat": 40.8518, "lon": 14.2681},
    "Turin": {"lat": 45.0703, "lon": 7.6869},
    "Palermo": {"lat": 38.1157, "lon": 13.3615}
  },
  "Spain": {
    "Madrid": {"lat": 40.4168, "lon": -3.7038},
    "Barcelona": {"lat": 41.3851, "lon": 2.1734},
    "Valencia": {"lat": 39.4699, "lon": -0.3763},
    "Seville": {"lat": 37.3886, "lon": -5.9823},
    "Zaragoza": {"lat": 41.6488, "lon": -0.8891}
  },
  "Netherlands": {
    "Amsterdam": {"lat": 52.3676, "lon": 4.9041},
    "Rotterdam": {"lat": 51.9244, "lon": 4.4777},
    "The Hague": {"lat": 52.0705, "lon": 4.3007},
    "Utrecht": {"lat": 52.0907, "lon": 5.1214},
    "Eindhoven": {"lat": 51.4416, "lon": 5.4697}
  },
  "Canada": {
    "Toronto": {"lat": 43.6532, "lon": -79.3832},
    "Vancouver": {"lat": 49.2827, "lon": -123.1207},
    "Montreal": {"lat": 45.5017, "lon": -73.5673},
    "Calgary": {"lat": 51.0447, "lon": -114.0719},
    "Ottawa": {"lat": 45.4215, "lon": -75.6972}
  },
  "Australia": {
    "Sydney": {"lat": -33.8688, "lon": 151.2093},
    "Melbourne": {"lat": -37.8136, "lon": 144.9631},
    "Brisbane": {"lat": -27.4698, "lon": 153.0251},
    "Perth": {"lat": -31.9505, "lon": 115.8605},
    "Adelaide": {"lat": -34.9285, "lon": 138.6007}
  },
  "Japan": {
    "Tokyo": {"lat": 35.6762, "lon": 139.6503},
    "Osaka": {"lat": 34.6937, "lon": 135.5023},
    "Nagoya": {"lat": 35.1815, "lon": 136.9066},
    "Sapporo": {"lat": 43.0618, "lon": 141.3545},
    "Fukuoka": {"lat": 33.5904, "lon": 130.4017}
  }
}
)JSON";

static const char index_html[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>MotoWeather Status</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: system-ui; max-width: 600px; margin: 0 auto; padding: 1rem; }
        .card { border: 1px solid #ddd; border-radius: 8px; padding: 1rem; margin: 1rem 0; }
        .label { font-weight: 600; display: inline-block; width: 180px; }
        input { width: 80px; }
        button { padding: 0.5rem 1rem; margin-top: 1rem; }
    </style>
</head>
<body>
    <h1>MotoWeather</h1>

    <div class="card">
        <h3>Location</h3>
        <span class="label">Latitude:</span> <span id="lat"></span><br>
        <span class="label">Longitude:</span> <span id="lon"></span><br>
        <span class="label">Source:</span> <span id="locationSource"></span>
    </div>

    <div class="card">
        <h3>Manual Location Selection</h3>
        <form id="locationForm">
            <span class="label">Country:</span>
            <select name="country" id="countrySelect" required>
                <option value="">Select Country</option>
            </select><br>
            <span class="label">City:</span>
            <select name="city" id="citySelect" required>
                <option value="">Select City</option>
            </select><br>
            <button type="submit">Set Location</button>
        </form>
        <div id="locationFeedback"></div>
    </div>

    <div class="card">
        <h3>WiFi Status</h3>
        <span class="label">Connected:</span> <span id="wifiConnected"></span><br>
        <span class="label">Signal Strength:</span> <span id="wifiSignal"></span><br>
        <button id="scanBtn">Scan Networks</button>
        <div id="networksList"></div>
    </div>

    <div class="card">
        <h3>WiFi Configuration</h3>
        <form id="wifiForm">
            <span class="label">Network SSID:</span> <input name="ssid" type="text" required><br>
            <span class="label">Password:</span> <input name="password" type="password"><br>
            <button type="submit">Save WiFi Settings</button>
        </form>
    </div>

    <div class="card">
        <h3>SSID Location Settings</h3>
        <form id="ssidLocationForm">
            <span class="label">SSID:</span> <select name="ssid" id="ssidSelect" required>
                <option value="">Select Network</option>
            </select><br>
            <span class="label">Latitude:</span> <input name="lat" type="number" step="0.0001" required><br>
            <span class="label">Longitude:</span> <input name="lon" type="number" step="0.0001" required><br>
            <button type="submit">Add Location</button>
        </form>
        <div id="ssidLocationsList"></div>
    </div>

    <div class="card">
        <h3>Ride Thresholds</h3>
        <form id="thresholdsForm">
            <span class="label">Max Rain (mm):</span> <input name="maxRain" type="number" step="0.1"><br>
            <span class="label">Max Wind (km/h):</span> <input name="maxWind" type="number" step="1"><br>
            <span class="label">Min Temp (°C):</span> <input name="minTemp" type="number" step="1"><br>
            <span class="label">Warn Wind (km/h):</span> <input name="warnWind" type="number" step="1"><br>
            <button type="submit">Save Settings</button>
        </form>
    </div>

    <div class="card">
        <h3>Weather API Config</h3>
        <form id="weatherApiForm">
            <span class="label">API Key:</span> <input name="apiKey" type="text" placeholder="Optional"><br>
            <span class="label">API URL:</span> <input name="apiUrl" type="url" value="https://api.open-meteo.com/v1/forecast"><br>
            <span class="label">Units:</span>
            <select name="units">
                <option value="metric">Metric</option>
                <option value="imperial">Imperial</option>
            </select><br>
            <span class="label">Verbose Debug:</span> <input name="weatherDebug" type="checkbox" id="weatherDebug"><br>
            <button type="submit">Save Settings</button>
        </form>
        <div id="weatherApiFeedback"></div>
    </div>

    <div class="card">
        <h3>Debug Info</h3>
        <span class="label">Weather Valid:</span> <span id="weatherValid"></span><br>
        <span class="label">Weather Age:</span> <span id="weatherAge"></span> minutes<br>
        <span class="label">mDNS Started:</span> <span id="mdnsStarted"></span><br>
        <br>
        <button id="toggleLogs" onclick="toggleVerboseLogs()">Show Verbose Logs</button>
        <div id="verboseLogs" style="display:none; margin-top:10px; font-family:monospace; font-size:12px; background:#222; color:#0f0; padding:8px; max-height:300px; overflow-y:auto;"></div>
    </div>

    <div class="card">
        <h3>OTA Update</h3>
        <form id="otaForm" action="/update" method="POST" enctype="multipart/form-data">
            <input type="file" name="update" accept=".bin" required><br>
            <button type="submit">Upload and Update</button>
        </form>
        <div id="otaFeedback"></div>
    </div>

    <script>
        const locations = {
  "United States": {
    "New York": {"lat": 40.7128, "lon": -74.0060},
    "Los Angeles": {"lat": 34.0522, "lon": -118.2437},
    "Chicago": {"lat": 41.8781, "lon": -87.6298},
    "Houston": {"lat": 29.7604, "lon": -95.3698},
    "Phoenix": {"lat": 33.4484, "lon": -112.0740}
  },
  "United Kingdom": {
    "London": {"lat": 51.5074, "lon": -0.1278},
    "Manchester": {"lat": 53.4808, "lon": -2.2426},
    "Birmingham": {"lat": 52.4862, "lon": -1.8904},
    "Glasgow": {"lat": 55.8642, "lon": -4.2518},
    "Liverpool": {"lat": 53.4084, "lon": -2.9916}
  },
  "Germany": {
    "Berlin": {"lat": 52.5200, "lon": 13.4050},
    "Munich": {"lat": 48.1351, "lon": 11.5820},
    "Hamburg": {"lat": 53.5511, "lon": 9.9937},
    "Cologne": {"lat": 50.9375, "lon": 6.9603},
    "Frankfurt": {"lat": 50.1109, "lon": 8.6821}
  },
  "France": {
    "Paris": {"lat": 48.8566, "lon": 2.3522},
    "Marseille": {"lat": 43.2965, "lon": 5.3698},
    "Lyon": {"lat": 45.7640, "lon": 4.8357},
    "Toulouse": {"lat": 43.6047, "lon": 1.4442},
    "Nice": {"lat": 43.7102, "lon": 7.2620}
  },
  "Italy": {
    "Rome": {"lat": 41.9028, "lon": 12.4964},
    "Milan": {"lat": 45.4642, "lon": 9.1900},
    "Naples": {"lat": 40.8518, "lon": 14.2681},
    "Turin": {"lat": 45.0703, "lon": 7.6869},
    "Palermo": {"lat": 38.1157, "lon": 13.3615}
  },
  "Spain": {
    "Madrid": {"lat": 40.4168, "lon": -3.7038},
    "Barcelona": {"lat": 41.3851, "lon": 2.1734},
    "Valencia": {"lat": 39.4699, "lon": -0.3763},
    "Seville": {"lat": 37.3886, "lon": -5.9823},
    "Zaragoza": {"lat": 41.6488, "lon": -0.8891}
  },
  "Netherlands": {
    "Amsterdam": {"lat": 52.3676, "lon": 4.9041},
    "Rotterdam": {"lat": 51.9244, "lon": 4.4777},
    "The Hague": {"lat": 52.0705, "lon": 4.3007},
    "Utrecht": {"lat": 52.0907, "lon": 5.1214},
    "Eindhoven": {"lat": 51.4416, "lon": 5.4697}
  },
  "Canada": {
    "Toronto": {"lat": 43.6532, "lon": -79.3832},
    "Vancouver": {"lat": 49.2827, "lon": -123.1207},
    "Montreal": {"lat": 45.5017, "lon": -73.5673},
    "Calgary": {"lat": 51.0447, "lon": -114.0719},
    "Ottawa": {"lat": 45.4215, "lon": -75.6972}
  },
  "Australia": {
    "Sydney": {"lat": -33.8688, "lon": 151.2093},
    "Melbourne": {"lat": -37.8136, "lon": 144.9631},
    "Brisbane": {"lat": -27.4698, "lon": 153.0251},
    "Perth": {"lat": -31.9505, "lon": 115.8605},
    "Adelaide": {"lat": -34.9285, "lon": 138.6007}
  },
  "Japan": {
    "Tokyo": {"lat": 35.6762, "lon": 139.6503},
    "Osaka": {"lat": 34.6937, "lon": 135.5023},
    "Nagoya": {"lat": 35.1815, "lon": 136.9066},
    "Sapporo": {"lat": 43.0618, "lon": 141.3545},
    "Fukuoka": {"lat": 33.5904, "lon": 130.4017}
  }
};

        // Populate country dropdown
        const countrySelect = document.getElementById('countrySelect');
        Object.keys(locations).forEach(country => {
            const option = document.createElement('option');
            option.value = country;
            option.textContent = country;
            countrySelect.appendChild(option);
        });

        // Populate city dropdown based on selected country
        countrySelect.addEventListener('change', function() {
            const citySelect = document.getElementById('citySelect');
            citySelect.innerHTML = '<option value="">Select City</option>';
            const selectedCountry = this.value;
            if (selectedCountry && locations[selectedCountry]) {
                Object.keys(locations[selectedCountry]).forEach(city => {
                    const option = document.createElement('option');
                    option.value = city;
                    option.textContent = city;
                    citySelect.appendChild(option);
                });
            }
        });

        fetch('/api/status')
            .then(r => r.json())
            .then(s => {
                document.getElementById('lat').textContent = s.lat.toFixed(4);
                document.getElementById('lon').textContent = s.lon.toFixed(4);
                document.getElementById('locationSource').textContent = s.locationSource;

                document.forms.thresholdsForm.maxRain.value = s.thresholds.maxRainMm;
                document.forms.thresholdsForm.maxWind.value = s.thresholds.maxWindKmh;
                document.forms.thresholdsForm.minTemp.value = s.thresholds.minTempC;
                document.forms.thresholdsForm.warnWind.value = s.thresholds.warnWindKmh;

                // Populate weather API form
                document.forms.weatherApiForm.apiKey.value = s.weatherApi.key;
                document.forms.weatherApiForm.apiUrl.value = s.weatherApi.url;
                document.forms.weatherApiForm.units.value = s.weatherApi.units;
                document.getElementById('weatherDebug').checked = s.weatherApi.debug;

                document.getElementById('wifiConnected').textContent = s.wifi.connected ? 'Yes' : 'No';
                document.getElementById('wifiSignal').textContent = s.wifi.signalStrength + ' dBm';

                // Populate debug info
                document.getElementById('weatherValid').textContent = s.debug.weatherValid ? 'Yes' : 'No';
                document.getElementById('weatherAge').textContent = s.debug.weatherAge;
                document.getElementById('mdnsStarted').textContent = s.debug.mdnsStarted ? 'Yes' : 'No';

                window.toggleVerboseLogs = function() {
                    const logDiv = document.getElementById('verboseLogs');
                    const btn = document.getElementById('toggleLogs');
                    if (logDiv.style.display === 'none') {
                        logDiv.style.display = 'block';
                        btn.textContent = 'Hide Verbose Logs';
                        fetch('/api/logs')
                            .then(r => r.json())
                            .then(data => {
                                document.getElementById('verboseLogs').innerHTML = data.logs.join('<br>');
                            })
                            .catch(err => console.error('Fetch error:', err));
                    } else {
                        logDiv.style.display = 'none';
                        btn.textContent = 'Show Verbose Logs';
                    }
                };

                // Populate WiFi form
                document.forms.wifiForm.ssid.value = s.wifi.ssid;
            })
            .catch(err => console.error('Fetch error:', err));

        document.getElementById('thresholdsForm').addEventListener('submit', e => {
            e.preventDefault();
            const data = new FormData(e.target);
            fetch('/api/thresholds', { method: 'POST', body: data })
                .catch(err => console.error('Fetch error:', err));
        });

        document.getElementById('weatherApiForm').addEventListener('submit', e => {
            e.preventDefault();
            const data = new FormData(e.target);
            fetch('/api/weatherconfig', { method: 'POST', body: data })
                .then(r => r.json())
                .then(data => {
                    document.getElementById('weatherApiFeedback').textContent = data.message;
                })
                .catch(() => {
                    document.getElementById('weatherApiFeedback').textContent = 'Error saving weather API config.';
                });
        });

        document.getElementById('wifiForm').addEventListener('submit', e => {
            e.preventDefault();
            const data = new FormData(e.target);
            fetch('/api/wifi/config', { method: 'POST', body: data })
                .then(() => alert('WiFi settings saved! Device will attempt to connect.'))
                .catch(err => console.error('Fetch error:', err));
        });

        document.getElementById('locationForm').addEventListener('submit', e => {
            e.preventDefault();
            const formData = new FormData(e.target);
            const country = formData.get('country');
            const city = formData.get('city');
            fetch('/api/location', { method: 'POST', body: formData })
                .then(r => r.json())
                .then(data => {
                    document.getElementById('locationFeedback').textContent = data.message;
                    // Refresh status
                    fetch('/api/status')
                        .then(r => r.json())
                        .then(s => {
                            document.getElementById('lat').textContent = s.lat.toFixed(4);
                            document.getElementById('lon').textContent = s.lon.toFixed(4);
                            document.getElementById('locationSource').textContent = s.locationSource;
                        })
                        .catch(err => console.error('Fetch error:', err));
                })
                .catch(() => {
                    document.getElementById('locationFeedback').textContent = 'Error setting location.';
                });
        });

        window.selectNetwork = function(ssid) {
            document.forms.wifiForm.ssid.value = ssid;
        };

        document.getElementById('scanBtn').addEventListener('click', () => {
            fetch('/api/wifi/scan')
                .then(r => r.json())
                .then(s => {
                    // Populate available networks list
                    let html = '<br><b>Available Networks:</b><br>';
                    s.networks.forEach(net => {
                        html += `<button onclick="selectNetwork('${net.ssid}')">${net.ssid} (${net.signalStrength} dBm)</button><br>`;
                    });
                    document.getElementById('networksList').innerHTML = html;

                    // Populate SSID dropdown
                    const ssidSelect = document.getElementById('ssidSelect');
                    ssidSelect.innerHTML = '<option value="">Select Network</option>';
                    s.networks.forEach(net => {
                        const option = document.createElement('option');
                        option.value = net.ssid;
                        option.textContent = net.ssid + ' (' + net.signalStrength + ' dBm)';
                        ssidSelect.appendChild(option);
                    });
                })
                .catch(err => console.error('Fetch error:', err));
        });

        // Populate SSID locations list from status
        function populateSsidLocations() {
            fetch('/api/status')
                .then(r => r.json())
                .then(s => {
                    let html = '<br><b>Configured SSID Locations:</b><br>';
                    if (s.ssidLocations && s.ssidLocations.length > 0) {
                        s.ssidLocations.forEach((loc, idx) => {
                            html += `${loc.ssid}: ${loc.lat.toFixed(4)}, ${loc.lon.toFixed(4)} `;
                            html += `<button onclick="deleteSsidLocation(${idx})">Delete</button><br>`;
                        });
                    } else {
                        html += 'None configured<br>';
                    }
                    document.getElementById('ssidLocationsList').innerHTML = html;
                })
                .catch(err => console.error('Fetch error:', err));
        }
        populateSsidLocations();

        document.getElementById('ssidLocationForm').addEventListener('submit', e => {
            e.preventDefault();
            const data = new FormData(e.target);
            fetch('/api/ssidlocation', { method: 'POST', body: data })
                .then(() => {
                    e.target.reset();
                    populateSsidLocations();
                })
                .catch(err => console.error('Fetch error:', err));
        });

        window.deleteSsidLocation = function(index) {
            fetch('/api/ssidlocation/delete', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'index=' + index
            }).then(() => populateSsidLocations())
            .catch(err => console.error('Fetch error:', err));
        };

        // OTA Update form handler
        document.getElementById('otaForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const formData = new FormData(this);
            fetch('/update', {
                method: 'POST',
                body: formData
            })
            .then(response => {
                if (response.ok) {
                    document.getElementById('otaFeedback').innerHTML = 'Update successful. Device will restart.';
                } else {
                    document.getElementById('otaFeedback').innerHTML = 'Update failed.';
                }
            })
            .catch(error => {
                document.getElementById('otaFeedback').innerHTML = 'Error: ' + error.message;
            });
        });
    </script>
</body>
</html>
    )HTML";

static void handleRoot() {
    server.send_P(200, "text/html", index_html);
}

static void handleApiStatus() {
    JsonDocument doc;

    doc["lat"] = configLat;
    doc["lon"] = configLon;

    if (manualLocation) {
        doc["locationSource"] = "Manual Selection";
    } else if (ssidBasedLocation) {
        doc["locationSource"] = "SSID-based";
    } else if (manualConfigPresent) {
        doc["locationSource"] = "Configuration file";
    } else if (geolocationActive) {
        doc["locationSource"] = "Automatic Geolocation";
    } else {
        doc["locationSource"] = "Default Fallback";
    }

    JsonObject thresholds = doc["thresholds"].to<JsonObject>();
    thresholds["maxRainMm"] = maxRainMm;
    thresholds["maxWindKmh"] = maxWindKmh;
    thresholds["minTempC"] = minTempC;
    thresholds["warnWindKmh"] = warnWindKmh;

    WeatherData current = getCurrentWeather();
    doc["current"]["tempC"] = current.tempC;
    doc["current"]["windKmh"] = current.windKmh;
    doc["current"]["precipMm"] = current.precipMm;

    doc["wifi"]["connected"] = state.wifiConnected;
    doc["wifi"]["signalStrength"] = state.wifiSignal;
    doc["wifi"]["ssid"] = wifiSsid;
    doc["wifi"]["password"] = wifiPassword;

    doc["weatherApi"]["key"] = weatherApiKey;
    doc["weatherApi"]["url"] = weatherApiUrl;
    doc["weatherApi"]["units"] = weatherUnits;
    doc["weatherApi"]["debug"] = weatherDebug;

    JsonArray ssidLocs = doc["ssidLocations"].to<JsonArray>();
    for (const auto& loc : ssidLocations) {
        JsonObject item = ssidLocs.add<JsonObject>();
        item["ssid"] = loc.ssid;
        item["lat"] = loc.lat;
        item["lon"] = loc.lon;
    }

    JsonObject debug = doc["debug"].to<JsonObject>();
    debug["weatherValid"] = state.weatherValid;
    debug["weatherAge"] = state.weatherAge;
    debug["mdnsStarted"] = state.mdnsStarted;

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void handleApiThresholds() {
    if (server.method() == HTTP_POST) {
        maxRainMm = server.arg("maxRain").toFloat();
        maxWindKmh = server.arg("maxWind").toFloat();
        minTempC = server.arg("minTemp").toFloat();
        warnWindKmh = server.arg("warnWind").toFloat();

        // Persist to config file
        JsonDocument doc;
        File file = LittleFS.open("/config.json", "r");
        if (file) {
            deserializeJson(doc, file);
            file.close();
        }

        doc["thresholds"]["maxRainMm"] = maxRainMm;
        doc["thresholds"]["maxWindKmh"] = maxWindKmh;
        doc["thresholds"]["minTempC"] = minTempC;
        doc["thresholds"]["warnWindKmh"] = warnWindKmh;

        file = LittleFS.open("/config.json", "w");
        if (file) {
            serializeJson(doc, file);
            file.close();
        }

        server.send(200);
    }
}

static void handleApiLocation() {
    if (server.method() == HTTP_POST) {
        String country = server.arg("country");
        String city = server.arg("city");

        // Parse locations JSON to find lat/lon
        JsonDocument locationsDoc;
        DeserializationError error = deserializeJson(locationsDoc, locationsJson);
        if (!error && !locationsDoc[country].isNull() && !locationsDoc[country][city].isNull()) {
            configLat = locationsDoc[country][city]["lat"];
            configLon = locationsDoc[country][city]["lon"];
            manualLocation = true;

            // Persist to config file
            JsonDocument doc;
            File file = LittleFS.open("/config.json", "r");
            if (file) {
                deserializeJson(doc, file);
                file.close();
            }

            doc["lat"] = configLat;
            doc["lon"] = configLon;
            doc["manualLocation"] = manualLocation;

            file = LittleFS.open("/config.json", "w");
            if (file) {
                serializeJson(doc, file);
                file.close();
                JsonDocument response;
                response["message"] = "Location set to " + city + ", " + country;
                String output;
                serializeJson(response, output);
                server.send(200, "application/json", output);
                return;
            }
        }

        // Error response
        JsonDocument response;
        response["message"] = "Invalid country or city selected.";
        String output;
        serializeJson(response, output);
        server.send(400, "application/json", output);
    }
}

static void handleApiWifiScan() {
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    int apCount = WiFi.scanNetworks();
    for (int i = 0; i < apCount; i++) {
        JsonObject ap = networks.add<JsonObject>();
        ap["ssid"] = WiFi.SSID(i);
        ap["signalStrength"] = WiFi.RSSI(i);
        ap["encryptionType"] = WiFi.encryptionType(i);
    }
    WiFi.scanDelete();

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void handleApiLogs() {
    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();
    
    // Get logs from weather module
    char logsBuffer[2048];
    getLogs(logsBuffer, sizeof(logsBuffer));
    
    // Parse log lines and add to response
    char* line = strtok(logsBuffer, "\n");
    while (line != NULL) {
        logs.add(line);
        line = strtok(NULL, "\n");
    }
    
    // Add system debug info
    logs.add("[SYS] Free heap: " + String(system_get_free_heap_size()) + " bytes");
    logs.add("[SYS] Weather valid: " + String(state.weatherValid ? "yes" : "no"));
    logs.add("[SYS] Weather age: " + String(state.weatherAge) + " min");
    logs.add("[SYS] mDNS: " + String(state.mdnsStarted ? "started" : "not started"));
    
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void handleApiWifiConfig() {
    if (server.method() == HTTP_POST) {
        String newSsid = server.arg("ssid");
        String newPassword = server.arg("password");

        // Update global variables
        wifiSsid = newSsid;
        wifiPassword = newPassword;

        // Save to config file
        JsonDocument doc;
        File file = LittleFS.open("/config.json", "r");
        if (file) {
            // Read existing config
            deserializeJson(doc, file);
            file.close();
        }

        // Update values
        doc["wifi"]["ssid"] = newSsid;
        doc["wifi"]["password"] = newPassword;

        file = LittleFS.open("/config.json", "w");
        if (file) {
            serializeJson(doc, file);
            file.close();
        }

        // Attempt to reconnect with new credentials
        state.disconnectedSinceMs = 0;   // restart the outage timer for the setup AP
        WiFi.disconnect();
        WiFi.begin(newSsid.c_str(), newPassword.c_str());

        server.send(200);
    }
}

static void handleApiWeatherConfig() {
    if (server.method() == HTTP_POST) {
        String apiKey = server.arg("apiKey");
        String apiUrl = server.arg("apiUrl");
        String units = server.arg("units");
        bool debug = server.hasArg("weatherDebug");

        // Update global variables
        weatherApiKey = apiKey;
        weatherApiUrl = apiUrl;
        weatherUnits = units;
        weatherDebug = debug;

        // Save to config file
        JsonDocument doc;
        File file = LittleFS.open("/config.json", "r");
        if (file) {
            // Read existing config
            deserializeJson(doc, file);
            file.close();
        }

        // Update values
        doc["weatherApiKey"] = apiKey;
        doc["weatherApiUrl"] = apiUrl;
        doc["weatherUnits"] = units;
        doc["weatherDebug"] = debug;

        file = LittleFS.open("/config.json", "w");
        if (file) {
            serializeJson(doc, file);
            file.close();
            JsonDocument response;
            response["message"] = "Weather API configuration saved.";
            String output;
            serializeJson(response, output);
            server.send(200, "application/json", output);
            return;
        }

        // Error response
        JsonDocument response;
        response["message"] = "Error saving configuration.";
        String output;
        serializeJson(response, output);
        server.send(500, "application/json", output);
    }
}

static void handleApiSsidLocation() {
    if (server.method() == HTTP_POST) {
        String ssid = server.arg("ssid");
        float lat = server.arg("lat").toFloat();
        float lon = server.arg("lon").toFloat();

        // Add to vector
        SsidLocation loc;
        loc.ssid = ssid;
        loc.lat = lat;
        loc.lon = lon;
        ssidLocations.push_back(loc);

        // Save entire config including ssidLocations
        JsonDocument doc;
        File file = LittleFS.open("/config.json", "r");
        if (file) {
            deserializeJson(doc, file);
            file.close();
        }

        doc["lat"] = configLat;
        doc["lon"] = configLon;
        doc["manualLocation"] = manualLocation;

        JsonArray arr = doc["ssidLocations"].to<JsonArray>();
        for (const auto& l : ssidLocations) {
            JsonObject item = arr.add<JsonObject>();
            item["ssid"] = l.ssid;
            item["lat"] = l.lat;
            item["lon"] = l.lon;
        }

        file = LittleFS.open("/config.json", "w");
        if (file) {
            serializeJson(doc, file);
            file.close();
        }

        server.send(200);
    }
}

static void handleApiSsidLocationDelete() {
    if (server.method() == HTTP_POST) {
        int index = server.arg("index").toInt();
        if (index >= 0 && index < (int)ssidLocations.size()) {
            ssidLocations.erase(ssidLocations.begin() + index);

            // Save entire config including ssidLocations
            JsonDocument doc;
            File file = LittleFS.open("/config.json", "r");
            if (file) {
                deserializeJson(doc, file);
                file.close();
            }

            doc["lat"] = configLat;
            doc["lon"] = configLon;
            doc["manualLocation"] = manualLocation;

            JsonArray arr = doc["ssidLocations"].to<JsonArray>();
            for (const auto& l : ssidLocations) {
                JsonObject item = arr.add<JsonObject>();
                item["ssid"] = l.ssid;
                item["lat"] = l.lat;
                item["lon"] = l.lon;
            }

            file = LittleFS.open("/config.json", "w");
            if (file) {
                serializeJson(doc, file);
                file.close();
            }
        }
        server.send(200);
    }
}

void initWebServer() {
    server.on("/", handleRoot);
    server.on("/api/status", handleApiStatus);
    server.on("/api/thresholds", handleApiThresholds);
    server.on("/api/location", handleApiLocation);
    server.on("/api/wifi/scan", handleApiWifiScan);
    server.on("/api/wifi/config", handleApiWifiConfig);
    server.on("/api/weatherconfig", handleApiWeatherConfig);
    server.on("/api/ssidlocation", handleApiSsidLocation);
    server.on("/api/ssidlocation/delete", handleApiSsidLocationDelete);
    server.on("/api/logs", handleApiLogs);
    server.begin();
}

void handleWebServer() {
    server.handleClient();
}
