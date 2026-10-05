#!/usr/bin/env python3
"""Render screenshots of the device's web UI for the manual (docs/images/webui-*.png).

The page is read from firmware/src/webserver.cpp, so the screenshots always show the real UI. A small
mock server answers the /api/* routes with demo data, and the place search (Open-Meteo geocoding) is
answered with canned results, so no device and no internet are needed.

    pip install playwright pillow       # a Chromium is needed too, see --chromium
    python tools/webui_screenshots.py [--out docs/images] [--chromium /path/to/chrome]
"""
import argparse
import json
import re
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "firmware" / "src" / "webserver.cpp"


def extract(name, source):
    """Return the contents of a `static const char <name>[] PROGMEM = R"TAG( ... )TAG";` literal."""
    m = re.search(name + r'\[\] PROGMEM = R"(\w+)\((.*?)\)\1";', source, re.S)
    if not m:
        raise SystemExit(f"{name} not found in {SOURCE}")
    return m.group(2)


STATUS = {
    "firmware": "0.2.0 (3f9c1ab)",
    "kids": {"hotFromC": 25, "shortsFromC": 20, "sweaterBelowC": 15, "coatBelowC": 5, "freezeBelowC": 0,
             "windyGustKmh": 50, "birthdays": [{"date": "2021-11-02", "initial": "E"},
                                               {"date": "2019-12-20", "initial": "S"}],
             "halloween": True, "sinterklaas": True, "christmas": True, "countdownDays": 14},
    "lat": 52.37, "lon": 4.89, "locationSource": "Manual Selection",
    "locationName": "Amsterdam, Noord-Holland, Nederland",
    "thresholds": {"maxRainMm": 2.0, "maxWindKmh": 60, "minTempC": 5, "warnWindKmh": 40, "rainProbPct": 50},
    "display": {"previewHr": 18, "dimAtNight": True, "nightBrightness": 10, "sleepMinutes": 0,
                "alwaysSleep": False, "language": "en", "quietStart": 23, "quietEnd": 6,
                "touchEnabled": True, "cycleSeconds": 0,
                # a combination: the kids screens on a tap, rider screens on a long press
                "screens": {"tap": ["weather", "clothes", "countdown"], "hold": ["ride", "week", "clock"],
                            "returnSeconds": 30}},
    "current": {"tempC": 14.2, "windKmh": 18.0, "precipMm": 0.0},
    "wifi": {"connected": True, "signalStrength": -58, "ssid": "HomeNet", "passwordSet": True},
    "weatherApi": {"url": "http://api.open-meteo.com/v1/forecast", "units": "metric", "debug": False},
    "ssidLocations": [{"ssid": "OfficeWiFi", "lat": 51.9244, "lon": 4.4777}],
    "debug": {"weatherValid": True, "weatherAge": 12, "mdnsStarted": True},
}
SCAN = {"networks": [{"ssid": "HomeNet", "signalStrength": -58}, {"ssid": "OfficeWiFi", "signalStrength": -71},
                     {"ssid": "Neighbour-2G", "signalStrength": -84}]}
LOGS = {"logs": ["[00:00:03] WiFi connected to HomeNet, 192.168.1.42",
                 "[00:00:04] mDNS started: dayspeck.local",
                 "[00:00:06] Time synced (NTP)",
                 "[00:00:09] Weather updated: 14.2 C, wind 18 km/h",
                 "[00:15:00] Weather updated: 14.5 C, wind 17 km/h"]}
OTA = {"current": "0.2.0", "build": "3f9c1ab", "variant": "dayspeck", "keySet": True,
       "url": "http://dayspeck-ota.example.workers.dev", "autoCheck": True, "checkedMinutesAgo": 42,
       "latest": "0.3.0", "notes": "Adds the web UI manual and the Dayspeck name.", "available": True,
       "size": 412000, "freeSpace": 700000, "error": ""}


GEOCODING = {"results": [
    {"name": "Amsterdam", "latitude": 52.37403, "longitude": 4.88969, "country": "Nederland",
     "admin1": "Noord-Holland", "admin2": "Gemeente Amsterdam"},
    {"name": "Amsterdam", "latitude": 42.93869, "longitude": -74.18819, "country": "Verenigde Staten",
     "admin1": "New York", "admin2": "Montgomery"},
    {"name": "Nieuw Amsterdam", "latitude": 52.71667, "longitude": 6.85833, "country": "Nederland",
     "admin1": "Drenthe", "admin2": "Emmen"},
]}


def make_handler(page, default_password):
    routes = {
        "/api/token": {"token": "demo", "otaPath": "/update/demo", "defaultPassword": default_password},
        "/api/status": STATUS, "/api/wifi/scan": SCAN, "/api/logs": LOGS, "/api/ota": OTA,
    }

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/":
                body, kind = page.encode(), "text/html; charset=utf-8"
            elif path in routes:
                body, kind = json.dumps(routes[path]).encode(), "application/json"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    return Handler


def shoot(browser, url, out, name, first, last, setup=None):
    """Screenshot from the top of card `first` to the bottom of card `last` (0-based card indexes)."""
    page = browser.new_page(viewport={"width": 640, "height": 900}, device_scale_factor=2)
    page.goto(url)
    page.wait_for_function("document.getElementById('lat').textContent !== ''")
    page.wait_for_function("document.getElementById('otaCurrent').textContent !== ''")
    if setup:
        setup(page)
    # page coordinates (bounding_box() is relative to the viewport, which the setup may have scrolled)
    top, bottom = page.evaluate(
        """([first, last]) => {
            const cards = document.querySelectorAll('.card');
            const banner = document.getElementById('pwBanner');
            const shown = banner.style.display !== 'none';
            const rect = el => el.getBoundingClientRect();
            const start = first === 0 && shown ? rect(banner).top : rect(cards[first]).top;
            return [start + scrollY - 8, rect(cards[last]).bottom + scrollY + 8];
        }""",
        [first, last],
    )
    top = max(top, 0)
    clip = {"x": 0, "y": top, "width": 640, "height": bottom - top}
    page.screenshot(path=str(out / name), clip=clip, full_page=True)
    page.close()
    print("wrote", out / name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "docs" / "images"))
    ap.add_argument("--chromium", help="path to a Chromium/Chrome binary (default: Playwright's own)")
    args = ap.parse_args()
    out = Path(args.out)

    from playwright.sync_api import sync_playwright

    source = SOURCE.read_text(encoding="utf-8")
    html = extract("index_html", source)

    # Cards in page order: 0 location, 1 set location, 2 WiFi status, 3 WiFi config, 4 SSID locations,
    # 5 thresholds, 6 screens, 7 display, 8 clothing, 9 countdowns (both only while a kids screen is used),
    # 10 weather API, 11 password, 12 debug, 13 firmware update
    def search_place(p):
        p.route("https://geocoding-api.open-meteo.com/**",
                lambda route: route.fulfill(status=200, content_type="application/json", body=json.dumps(GEOCODING)))
        p.fill("#placeQuery", "Amsterdam")
        p.click("#placeSearch button")
        p.wait_for_selector("#placeResults button")

    def scan(p):
        p.click("#scanBtn")
        p.wait_for_selector("#networksList button")

    def logs(p):
        p.click("#toggleLogs")
        p.wait_for_function("document.getElementById('verboseLogs').textContent !== ''")

    servers = []
    for default_password in (True, False):
        srv = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(html, default_password))
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        servers.append(srv)
    base = [f"http://127.0.0.1:{s.server_port}/" for s in servers]

    with sync_playwright() as pw:
        kwargs = {"executable_path": args.chromium} if args.chromium else {}
        browser = pw.chromium.launch(**kwargs)
        shoot(browser, base[0], out, "webui-location.png", 0, 1, search_place)
        shoot(browser, base[1], out, "webui-wifi.png", 2, 4, scan)
        shoot(browser, base[1], out, "webui-settings.png", 5, 9)
        shoot(browser, base[1], out, "webui-screens.png", 6, 6)
        shoot(browser, base[1], out, "webui-update.png", 12, 13, logs)
        browser.close()
    for s in servers:
        s.shutdown()


if __name__ == "__main__":
    main()
