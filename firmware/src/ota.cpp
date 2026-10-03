// Dayspeck Bedside Display — Pull updates (see ota.h and plans/ota_plan.md)
#include "ota.h"
#include "config.h"
#include "app_state.h"
#include "version.h"
#include "weather.h"     // logMessage
#include "motologic.h"
#include "ota_pubkey.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <memory>
extern "C" {
#include <bearssl/bearssl_rsa.h>
#include <StackThunk.h>
}

#define OTA_FIRST_CHECK_MS     120000UL     // first check two minutes after boot
#define OTA_CHECK_INTERVAL_MS  86400000UL   // then daily
#define OTA_RETRY_MS           3600000UL    // after a failed check, try again in an hour
#define OTA_MANIFEST_MAX       2048
#define OTA_SIG_LEN            256          // RSA-2048
#define OTA_MIN_BLOCK_BYTES    8000         // signature check: 6.2 KB second stack + the key

String otaServerUrl = OTA_DEFAULT_URL;
bool   otaAutoCheck = true;
OtaStatus otaStatus = {};
void (*otaProgressHook)(int percent) = nullptr;

static bool checkRequested = false;
static bool installRequested = false;

// The image the last good manifest offers for this variant
static String  imageFile;
static uint8_t imageSha256[32];

// RSA needs about 2 KB of stack, more than the loop task can spare: run it on the core's second
// ("thunk") stack, as BearSSL::SigningVerifier does. Calling BearSSL directly instead of through
// SigningVerifier keeps its key parser and elliptic-curve code (30 KB) out of the firmware.
extern "C" uint32_t otaRsaVerify(const unsigned char* signature, size_t length,
                                 const br_rsa_public_key* key, unsigned char* digest) {
    return br_rsa_i15_pkcs1_vrfy(signature, length, BR_HASH_OID_SHA256, 32, key, digest);
}
make_stack_thunk(otaRsaVerify);
extern "C" uint32_t thunk_otaRsaVerify(const unsigned char* signature, size_t length,
                                       const br_rsa_public_key* key, unsigned char* digest);

// True when `signature` is the release key's RSA PKCS#1 v1.5 signature of `sha256`
static bool verifySignature(const uint8_t* sha256, const void* signature, uint32_t length) {
#if OTA_PUBKEY_SET
    if (length != OTA_SIG_LEN || sizeof(OTA_PUBKEY_N) != OTA_SIG_LEN) return false;
    if (ESP.getMaxFreeBlockSize() < OTA_MIN_BLOCK_BYTES) {
        logMessage("OTA: not enough memory to check a signature");
        return false;
    }
    // BearSSL reads the key byte by byte, so it must be in RAM, not in flash (PROGMEM)
    std::unique_ptr<uint8_t[]> n(new (std::nothrow) uint8_t[sizeof(OTA_PUBKEY_N)]);
    if (!n) return false;
    memcpy_P(n.get(), OTA_PUBKEY_N, sizeof(OTA_PUBKEY_N));
    uint8_t e[sizeof(OTA_PUBKEY_E)];
    memcpy_P(e, OTA_PUBKEY_E, sizeof(e));
    br_rsa_public_key key = { n.get(), sizeof(OTA_PUBKEY_N), e, sizeof(e) };

    unsigned char digest[32];
    stack_thunk_add_ref();   // allocates the second stack unless something else holds it
    bool ok = thunk_otaRsaVerify((const unsigned char*)signature, length, &key, digest) == 1;
    stack_thunk_del_ref();
    return ok && memcmp(digest, sha256, sizeof(digest)) == 0;
#else
    (void)sha256; (void)signature; (void)length;
    return false;
#endif
}

// Called by the core Updater at the end of every firmware update, before the new image is
// marked bootable. For a pull update the image must also be the one the signed manifest names.
class OtaVerifier : public UpdaterVerifyClass {
  public:
    const uint8_t* expectedSha256 = nullptr;

    uint32_t length() override { return OTA_SIG_LEN; }

    bool verify(UpdaterHashClass* hash, const void* signature, uint32_t length) override {
        if (hash->len() != 32) return false;
        const uint8_t* sha256 = (const uint8_t*)hash->hash();
        if (expectedSha256 && memcmp(sha256, expectedSha256, 32) != 0) {
            logMessage("OTA: the image does not match the manifest");
            return false;
        }
        bool ok = verifySignature(sha256, signature, length);
        if (!ok) logMessage("OTA: image signature invalid, update rejected");
        return ok;
    }
};

static BearSSL::HashSHA256 updateHash;
static OtaVerifier updateVerifier;

void otaInit() {
    otaStatus.keySet = OTA_PUBKEY_SET;
#if OTA_PUBKEY_SET
    Update.installSignature(&updateHash, &updateVerifier);
#endif
}

void otaRequestCheck() {
    checkRequested = true;
}

void otaRequestInstall() {
    installRequested = true;
}

uint32_t otaFreeSpace() {
    return ESP.getFreeSketchSpace();
}

// Server base URL without a trailing slash, "" if unusable
static String baseUrl() {
    String url = otaServerUrl;
    url.trim();
    while (url.endsWith("/")) url.remove(url.length() - 1);
    return url.startsWith("http://") ? url : String();
}

static bool safeFileName(const char* name) {
    size_t len = strlen(name);
    if (len == 0 || len > 48) return false;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '-') return false;
    }
    return true;
}

static void setError(const String& message) {
    otaStatus.error = message;
    logMessage(("OTA: " + message).c_str());
}

// Download, verify and parse the manifest. Returns false (and sets the error) on any problem.
static bool runCheck() {
    if (!otaStatus.keySet) {
        setError("this build has no update key");
        return false;
    }
    String base = baseUrl();
    if (base.length() == 0) {
        setError("no update server configured");
        return false;
    }

    WiFiClient client;
    HTTPClient http;
    http.useHTTP10(true);   // no chunked transfer encoding
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, base + "/ota-manifest.txt")) {
        setError("invalid update server URL");
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        setError("update server: " + (code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code)));
        http.end();
        return false;
    }
    int length = http.getSize();
    if (length <= 0 || length > OTA_MANIFEST_MAX) {
        setError("manifest missing or too large");
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    // Line 1: JSON; line 2: hex RSA signature of line 1
    int newline = body.indexOf('\n');
    if (newline <= 0) {
        setError("manifest has no signature");
        return false;
    }
    String signatureHex = body.substring(newline + 1);
    signatureHex.trim();
    body.remove(newline);   // the signed payload
    uint8_t signature[OTA_SIG_LEN];
    if (!hexToBytes(signatureHex.c_str(), signature, sizeof(signature))) {
        setError("manifest signature unreadable");
        return false;
    }
    signatureHex = String();

    BearSSL::HashSHA256 hash;
    hash.begin();
    hash.add(body.c_str(), body.length());
    hash.end();
    if (!verifySignature((const uint8_t*)hash.hash(), signature, sizeof(signature))) {
        setError("manifest signature invalid (wrong server, or a release signed with another key)");
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        setError("manifest unreadable");
        return false;
    }
    const char* version = doc["version"] | "";
    uint16_t parsed[3];
    if (!parseVersion(version, parsed)) {
        setError("manifest has no valid version");
        return false;
    }
    JsonObject variant = doc["variants"][OTA_VARIANT];
    const char* file = variant["file"] | "";
    uint32_t size = variant["size"] | 0;
    uint8_t sha256[32];
    if (variant.isNull() || !safeFileName(file) || size == 0 ||
        !hexToBytes(variant["sha256"] | "", sha256, sizeof(sha256))) {
        setError(String("release ") + version + " has no valid " OTA_VARIANT " image");
        return false;
    }

    // Only a fully valid manifest replaces what the last one offered
    otaStatus.latestVersion = version;
    otaStatus.notes = doc["notes"] | "";
    otaStatus.size = size;
    otaStatus.available = isNewerVersion(version, FW_VERSION);
    otaStatus.error = "";
    imageFile = file;
    memcpy(imageSha256, sha256, sizeof(imageSha256));
    if (otaStatus.available) {
        logMessage(("OTA: version " + otaStatus.latestVersion + " is available").c_str());
    }
    return true;
}

// Returns only on failure; on success the device restarts into the new firmware
static void runInstall() {
    if (!otaStatus.keySet) {
        setError("this build has no update key");
        return;
    }
    if (!otaStatus.available || imageFile.length() == 0) {
        setError("no update available, check first");
        return;
    }
    if (otaStatus.size > otaFreeSpace()) {
        setError("the update (" + String(otaStatus.size) + " bytes) does not fit in the free flash (" +
                 String(otaFreeSpace()) + " bytes); flash this version over serial once");
        return;
    }
    if (ESP.getMaxFreeBlockSize() < OTA_MIN_BLOCK_BYTES) {
        setError("not enough memory for an update, try again after a restart");
        return;
    }

    String url = baseUrl() + "/v" + otaStatus.latestVersion + "/" + imageFile;
    logMessage(("OTA: installing " + url).c_str());
    if (otaProgressHook) otaProgressHook(0);

    ESPhttpUpdate.rebootOnUpdate(false);
    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.onProgress([](int done, int total) {
        static int lastPercent = -1;
        int percent = total > 0 ? (int)((int64_t)done * 100 / total) : 0;
        if (percent != lastPercent && otaProgressHook) {
            lastPercent = percent;
            otaProgressHook(percent);
        }
    });

    WiFiClient client;
    updateVerifier.expectedSha256 = imageSha256;
    t_httpUpdate_return result = ESPhttpUpdate.update(client, url, FW_VERSION);
    updateVerifier.expectedSha256 = nullptr;

    if (result == HTTP_UPDATE_OK) {
        logMessage(("OTA: version " + otaStatus.latestVersion + " installed, restarting").c_str());
        if (otaProgressHook) otaProgressHook(100);
        delay(500);
        ESP.restart();
    }
    setError("install failed: " + ESPhttpUpdate.getLastErrorString());
    state.displayDirty = true;   // the progress screen covered the normal view
}

void otaLoop(bool idle) {
    if (!state.wifiConnected) return;

    if (installRequested) {
        installRequested = false;
        runInstall();
        return;
    }

    bool due;
    if (!otaStatus.checked) {
        due = millis() >= OTA_FIRST_CHECK_MS;
    } else {
        unsigned long interval = otaStatus.error.length() ? OTA_RETRY_MS : OTA_CHECK_INTERVAL_MS;
        due = intervalElapsed(millis(), otaStatus.lastCheckMs, interval);
    }
    bool scheduled = otaAutoCheck && otaStatus.keySet && idle && due && baseUrl().length() > 0;
    if (!checkRequested && !scheduled) return;
    checkRequested = false;

    bool wasAvailable = otaStatus.available;
    runCheck();
    otaStatus.checked = true;
    otaStatus.lastCheckMs = millis();
    if (otaStatus.available != wasAvailable) state.displayDirty = true;
}
