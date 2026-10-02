# Plan: pull-based OTA updates from GitHub Releases

> **Status:** implemented on branch `claude/vibrant-mayer-01x9ex`. Builds (all three environments,
> no warnings), host tests and tool tests pass. **Not run on hardware yet**: see the checklist.

## Decisions

- The device **pulls** updates from GitHub Releases. It **checks daily** and shows "update
  available" (`UPD` on the OLED, *Firmware Update* card in the web UI); the user **confirms the
  install**. Nothing installs by itself.
- **No TLS on the device; a plain-HTTP relay instead.** TLS was the first choice but does not fit:
  see "Measurements". The relay (`tools/ota-relay`, a Cloudflare Worker) serves the latest release
  over HTTP.
- **Signed manifest and signed images** (RSA-2048, PKCS#1 v1.5, SHA-256). Integrity comes from the
  signature, so the relay and the network are untrusted.
- **gzip-compressed images**, unpacked by the ESP8266 bootloader (eboot).
- **64 KB filesystem** (`eagle.flash.1m64.ld`) instead of the board default of 256 KB. Existing
  devices need **one serial flash**; their `config.json` starts from scratch once (agreed).
- The filesystem is **not** part of an update.

## Measurements (`pio run`, 1 MB ESP-01)

Free space for an update = flash before the filesystem minus the running image, rounded to sectors
(`ESP.getFreeSketchSpace()`); the web upload page takes 4 KB less.

| Build | Image | Signed update | Free (256 KB FS, old) | Free (128 KB FS) | Free (64 KB FS) |
|---|---|---|---|---|---|
| Before this work | 472 KB | (raw) 472 KB | 290 KB | — | — |
| + TLS client + `SigningVerifier` (spike) | 590 KB | 414 KB gz | 172 KB | — | 369 KB |
| + `SigningVerifier` only (no TLS) | 534 KB | 369 KB gz | — | 360 KB | — |
| **Final: direct BearSSL RSA check** | **504 KB** | **346 KB gz** | — | — | **451 KB** |

- The old layout could never take an OTA update, not even the existing browser upload (472 KB into
  290 KB). It was never tried on hardware.
- TLS does not fit next to a second image in any 1 MB layout.
- `BearSSL::SigningVerifier` links the key parser and the elliptic-curve code (30 KB). Calling
  `br_rsa_i15_pkcs1_vrfy` directly, on the core's second ("thunk") stack, avoids that.
- Final margin: about 105 KB. The firmware can grow to roughly 560 KB before an update no longer
  fits; CI (`ota_tool.py check-size`) fails before that happens.
- Static RAM: 39.0 KB → 41.0 KB. During a signature check the core's 6.2 KB second stack is
  allocated and freed again; checks require an 8 KB free block.

## Design

### Release (`.github/workflows/release.yml`, on tag `vX.Y.Z`)

1. Tag must equal `FW_VERSION`; the `OTA_SIGNING_KEY` secret must match the committed
   `firmware/include/ota_pubkey.h` (`ota_tool.py check-key`).
2. Tests, build rider + kids + filesystem, size check.
3. Assets:

| Asset | Content |
|---|---|
| `ota-manifest.txt` | line 1: JSON `{"version","notes","variants":{"rider":{"file","size","sha256"},"kids":{…}}}`; line 2: hex signature of line 1 |
| `motoclock-rider.bin.gz`, `motoclock-kids.bin.gz` | gzip image ‖ RSA signature of SHA-256(gzip image) ‖ uint32 LE signature length (the core's format) |
| `motoclock-*-serial.bin`, `motoclock-fs-serial.bin` | raw images for serial flashing |

`sha256` in the manifest is the hash of the signed part of the image, so an older (validly signed)
image cannot be passed off under a newer version.

### Relay (`tools/ota-relay`)

`GET /ota-manifest.txt` → latest release's manifest; `GET /vX.Y.Z/motoclock-(rider|kids).bin.gz` →
that release's image. Buffers bodies so replies have a `Content-Length`. Any static HTTP server with
the same layout works too.

### Device (`firmware/src/ota.cpp`)

- `otaInit()` installs the signature check in the core `Updater` for **every** firmware update,
  including manual uploads, when a key is compiled in.
- Check: two minutes after boot, then daily (hourly after a failure); only while nobody touched the
  device for a minute. Verifies the manifest signature, then parses it; a manifest is only taken
  over when fully valid. Variant fixed at build time (`KIDS_MODE` → `kids`).
- Install: only on request from the web UI, only a newer version, only if it fits and the heap
  allows the check. `ESPhttpUpdate` streams the image; the `Updater` verifies the hash against the
  manifest and the signature before it marks the image bootable. On failure the old firmware keeps
  running and the reason is shown in the web UI.
- Web API: `GET /api/ota`, `POST /api/ota/check`, `POST /api/ota/install`, `POST /api/ota/settings`
  (all behind the admin password, POSTs with the CSRF token). Config: `ota.url`, `ota.autoCheck`.

### Pure logic (`firmware/lib/motologic`, host tested)

`parseVersion`, `isNewerVersion` (numeric, no downgrades), `hexToBytes`.

## Status

- [x] Phase 0: measured TLS, signing and layouts (table above); TLS rejected, relay chosen
- [x] Version logic + host tests
- [x] `tools/ota_tool.py` (keygen, check-key, sign, manifest, check-size) + tests
- [x] `release.yml`; CI size check
- [x] Relay worker (smoke-tested locally with Node)
- [x] Device: `ota.cpp`, web UI card and API, `UPD` mark, config, 64 KB layout
- [x] README
- [ ] **Owner:** `python tools/ota_tool.py keygen`, add the `OTA_SIGNING_KEY` secret, commit
      `ota_pubkey.h`; deploy the relay and confirm it answers over plain `http://`
- [ ] **Hardware checklist** (not possible in CI):
  - [ ] serial flash of the new layout; device boots, LittleFS is formatted, setup AP appears
  - [ ] *Check now* finds a newer release through the relay; log shows no memory errors
  - [ ] install works and the device boots the new version with its settings
  - [ ] an image signed with another key is rejected (pull and manual upload); old firmware runs on
  - [ ] a manifest signed with another key is rejected
  - [ ] WiFi dropped / power cut during the download: old firmware keeps running
  - [ ] kids build only offers and installs the kids image
  - [ ] daily check pauses the display only briefly and only when idle

## Risks

- **No hardware rollback** on ESP8266: a bad but validly signed image needs a serial flash. Only tag
  releases that passed the hardware checklist.
- **Key management:** a leaked private key lets anyone who can reach the device's network path
  install their firmware; a lost key means one manual upload of a build with a new key.
- **Withholding:** the relay or the network can block updates or keep serving an old manifest.
  They cannot install anything older or unsigned.
- **Firmware growth:** about 60 KB left before updates stop fitting (CI guards it). Beyond that,
  options are trimming code or a 4 MB module.
- **Relay host:** workers.dev must answer plain HTTP without redirecting to HTTPS; to verify on
  deployment (see the relay README).
