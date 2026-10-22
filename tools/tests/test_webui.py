"""Browser checks for the embedded page. Optional: install Playwright and a Chromium browser.

DAYSPECK_CHROMIUM may point to an existing Chrome/Chromium executable.
"""
import json
import os
import threading
from http.server import ThreadingHTTPServer
from urllib.parse import parse_qs

import pytest

from webui_screenshots import GEOCODING, SOURCE, extract, make_handler

playwright = pytest.importorskip("playwright.sync_api")
expect = playwright.expect


@pytest.fixture(scope="module")
def browser():
    with playwright.sync_playwright() as pw:
        options = {"executable_path": os.environ["DAYSPECK_CHROMIUM"]} if os.environ.get("DAYSPECK_CHROMIUM") else {}
        try:
            instance = pw.chromium.launch(**options)
        except playwright.Error as error:
            pytest.skip(f"Chromium unavailable: {error}")
        yield instance
        instance.close()


@pytest.fixture(scope="module")
def url():
    html = extract("index_html", SOURCE.read_text(encoding="utf-8"))
    server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(html, True))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield f"http://127.0.0.1:{server.server_port}/"
    server.shutdown()
    server.server_close()
    thread.join()


@pytest.fixture
def ui_page(browser, url):
    page = browser.new_page(viewport={"width": 390, "height": 844})
    errors, posts = [], []
    page.on("pageerror", lambda error: errors.append(str(error)))

    def route_api(route):
        if route.request.method == "POST":
            posts.append(route.request)
            route.fulfill(status=200, content_type="application/json", body='{"message":"Saved"}')
        else:
            route.continue_()

    page.route("**/api/**", route_api)
    page.goto(url)
    page.wait_for_function("document.getElementById('overviewWifi').textContent.includes('HomeNet')")
    page.wait_for_function("document.getElementById('otaCurrent').textContent !== ''")
    yield page, posts
    page.close()
    assert not errors


def reveal(locator):
    locator.evaluate("el => { for (let p = el.parentElement; p; p = p.parentElement) if (p.tagName === 'DETAILS') p.open = true; }")


@pytest.mark.parametrize("width", [320, 390, 768, 1280])
def test_responsive_layout_labels_and_touch_targets(ui_page, width):
    page, _ = ui_page
    page.set_viewport_size({"width": width, "height": 900})
    assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
    assert page.locator(".jump-nav a").count() == 4
    assert page.evaluate("Array.from(document.querySelectorAll('input, select')).every(e => e.labels.length || e.getAttribute('aria-label'))")
    assert page.evaluate("Array.from(document.querySelectorAll('button')).filter(e => e.offsetHeight).every(e => e.offsetHeight >= 44)")
    for target in ["#overview", "#display", "#connection", "#device"]:
        page.locator(f'.jump-nav a[href="{target}"]').click()
        assert page.evaluate("location.hash") == target
        assert page.locator(target).bounding_box()["y"] >= page.locator(".jump-nav").bounding_box()["height"]
    page.locator('#pwBanner a').click()
    assert page.evaluate("location.hash") == "#passwordCard"
    for selector in ["#ssidCard", "#weatherApiCard", "#debugCard"]:
        assert not page.locator(selector).evaluate("el => el.open")


@pytest.mark.parametrize("form_id,path,key", [
    ("thresholdsForm", "/api/thresholds", "maxRain"),
    ("displayForm", "/api/display", "nightBrightness"),
    ("kidsForm", "/api/kids", "dinner"),
    ("countdownForm", "/api/countdown", "countdownDays"),
    ("weatherApiForm", "/api/weatherconfig", "apiUrl"),
    ("otaSettingsForm", "/api/ota/settings", "url"),
    ("wifiForm", "/api/wifi/config", "ssid"),
    ("hostnameForm", "/api/hostname", "hostname"),
])
def test_existing_form_contracts_and_inline_feedback(ui_page, form_id, path, key):
    page, posts = ui_page
    form = page.locator("#" + form_id)
    reveal(form)
    form.locator('button[type="submit"]').click()
    expect(form.locator(".form-feedback")).to_have_text("Saved")
    assert posts[-1].url.endswith(path)
    assert posts[-1].headers["x-dayspeck"] == "demo"
    assert key in parse_qs(posts[-1].post_data, keep_blank_values=True)


def test_screen_order_home_label_limit_and_payload(ui_page):
    page, posts = ui_page
    page.select_option("#screenPreset", "rider")
    expect(page.locator("#tapSlots .slot-label").first).to_have_text("1 Home")
    page.locator("#tapSlots .slot-row").nth(1).get_by_role("button", name="Move up", exact=True).click()
    assert page.locator("#tapSlots select").first.input_value() == "rideOther"
    expect(page.locator("#tapSlots .slot-label").first).to_have_text("1 Home")
    page.locator("#tapSlots .slot-row").nth(1).get_by_role("button", name="Remove", exact=True).click()
    for _ in range(5):
        page.click("#addTap")
    expect(page.locator("#addTap")).to_be_disabled()
    assert page.locator("#tapSlots select").count() == 6
    page.locator('#screensForm button[type="submit"]').click()
    expect(page.locator("#screensForm .form-feedback")).to_have_text("Saved")
    body = parse_qs(posts[-1].post_data)
    assert posts[-1].url.endswith("/api/screens")
    assert body["tap"][0].split(",")[0] == "rideOther"
    assert len(body["tap"][0].split(",")) == 6


def test_saving_other_settings_preserves_unsaved_forms_and_screens(ui_page):
    page, _ = ui_page
    page.locator('#displayForm [name="nightBrightness"]').fill("37")
    page.select_option("#screenPreset", "rider")
    form = page.locator("#weatherApiForm")
    reveal(form)
    form.locator('button[type="submit"]').click()
    expect(form.locator(".form-feedback")).to_have_text("Saved")
    page.wait_for_function("!document.forms.weatherApiForm.dataset.saving")
    assert page.locator('#displayForm [name="nightBrightness"]').input_value() == "37"
    assert page.locator("#tapSlots select").first.input_value() == "ride"
    expect(page.locator("#displayForm .form-feedback")).to_have_text("Unsaved changes")
    assert page.locator("#countdownCard").is_hidden()


def test_edits_during_save_and_failures_are_preserved(ui_page):
    page, _ = ui_page
    pending = []
    page.route("**/api/display", lambda route: pending.append(route))
    field = page.locator('#displayForm [name="nightBrightness"]')
    field.fill("33")
    submit = page.locator('#displayForm button[type="submit"]')
    submit.click()
    expect(submit).to_be_disabled()
    field.fill("44")
    pending[0].fulfill(status=200, content_type="application/json", body='{"message":"Saved"}')
    expect(page.locator("#displayForm .form-feedback")).to_contain_text("newer unsaved changes")
    assert field.input_value() == "44"
    submit.click()
    pending[1].fulfill(status=500, content_type="application/json", body='{"message":"Could not save"}')
    expect(page.locator("#displayForm .form-feedback.err")).to_have_text("Could not save")
    assert field.input_value() == "44"
    expect(submit).to_be_enabled()


def test_place_search_and_offline_manual_coordinates(ui_page):
    page, posts = ui_page
    page.route("https://geocoding-api.open-meteo.com/**", lambda route: route.fulfill(
        status=200, content_type="application/json", body=json.dumps(GEOCODING)))
    page.fill("#placeQuery", "Amsterdam")
    page.locator("#placeSearch button").click()
    page.locator("#placeResults button").first.click()
    assert page.locator("#manualLocation").evaluate("el => el.open")
    page.locator('#locationForm button[type="submit"]').click()
    expect(page.locator("#locationForm .form-feedback")).to_have_text("Saved")
    assert posts[-1].url.endswith("/api/location")
    body = parse_qs(posts[-1].post_data)
    assert body["lat"] == ["52.37"] and body["lon"] == ["4.89"]
    page.route("https://geocoding-api.open-meteo.com/**", lambda route: route.abort())
    page.locator("#manualLocation").evaluate("el => el.open = false")
    page.locator("#placeSearch button").click()
    expect(page.locator("#placeResults")).to_contain_text("needs an internet connection")
    assert page.locator("#manualLocation").evaluate("el => el.open")


def test_scan_selection_and_password_confirmation(ui_page):
    page, posts = ui_page
    page.click("#scanBtn")
    page.locator("#networksList button").nth(1).click()
    assert page.locator('#wifiForm [name="ssid"]').input_value() == "OfficeWiFi"
    expect(page.locator("#wifiForm .form-feedback")).to_have_text("Unsaved changes")
    page.locator('#passwordForm [name="current"]').fill("old-password")
    page.locator('#passwordForm [name="newPassword"]').fill("new-password")
    page.fill("#newPassword2", "different-password")
    page.locator('#passwordForm button[type="submit"]').click()
    expect(page.locator("#passwordForm .form-feedback.err")).to_have_text("The new passwords do not match")
    assert not posts
    page.fill("#newPassword2", "new-password")
    page.locator('#passwordForm button[type="submit"]').click()
    expect(page.locator("#passwordForm .form-feedback")).to_have_text("Saved")
    assert posts[-1].url.endswith("/api/password")
    assert parse_qs(posts[-1].post_data) == {"current": ["old-password"], "newPassword": ["new-password"]}


def test_required_manual_fields_are_revealed_before_validation(ui_page):
    page, posts = ui_page
    page.locator('#locationForm button[type="submit"]').click()
    assert page.locator("#manualLocation").evaluate("el => el.open")
    assert not posts
