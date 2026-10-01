# Plan: pull-based OTA updates from GitHub Releases

**Decisions taken:** the device *pulls* updates from GitHub Releases; it **auto-checks** (daily) and
shows "update available"; the user **confirms the install** in the web UI. Nothing installs by itself.

## Where we are

- Push OTA already works: `ESP8266HTTPUpdateServer` on `/update-<token>` behind the admin password
  (`main.cpp`, `webserver.cpp`). The user picks a `firmware.bin` by hand.
- CI builds `esp01_1m`, `esp01_1m_debug`, `esp01_1m_kids` and uploads `firmware.bin` + `littlefs.bin`
  as a workflow *artifact* (not a Release, needs a GitHub login to download).
- `FW_VERSION "0.2.0"` + git hash are compiled in (`version.h`, `tools/pio_version.py`).
- **No TLS on purpose** (`improvement_plan.md`: removing it dropped flash from 74 % to 60 %). All
  network traffic is plain HTTP via `WiFiClient` + `HTTPClient` (`weather.cpp`).

## The central problem: GitHub is HTTPS-only

Release assets are served over HTTPS and redirect to `objects.githubusercontent.com`. The ESP-01 has
~1 MB flash and ~40 KB heap. Three ways out; **Phase 0 decides**:

| Option | Flash / RAM cost | Trust model | Notes |
|---|---|---|---|
| **A. TLS on the device** (BearSSL) | +100-150 KB flash, ~20 KB+ heap during handshake | cert pinning or `setInsecure()` | May not fit OTA headroom on 1 MB; GitHub does not do small-buffer (MFLN) TLS; redirect chain needs 2 handshakes. Highest risk. |
| **B. Plain-HTTP relay + signed images** (recommended to try first) | ~+10 KB (signature check) | **end-to-end signature**, transport is irrelevant | A tiny relay (Cloudflare Worker, or any HTTP host) fetches the Release and serves `manifest.json` + `.bin` over `http://`. GitHub Pages cannot be used: it forces HTTPS. |
| **C. Local mirror** | none | LAN trust | A Pi/NAS that syncs the Release. Works, but needs hardware and defeats "one click". |

Because transport is plain HTTP (like weather today), **integrity must come from a signature**, not
from TLS. That is required in any case: without it anyone on the network could push firmware.

## Phase 0: measure before designing (small, do first)

- [ ] Build `esp01_1m` and record: firmware size, free sketch space (`Update.getFreeSketchSpace()`
      equivalent), the linker script in use, and FS size. OTA needs *free space ≥ new image size*;
      state the max firmware size we can ever flash. Put the number in this file.
- [ ] Spike signed OTA in `native`-independent form: confirm `Update.installSignature()` with
      `BearSSL::SigningVerifier` and `signing.py` works on `espressif8266@4.2.1` (core 3.1.x).
      Record the flash cost.
- [ ] Spike option A only if B is rejected: HTTPS GET to the release URL on a real ESP-01, record heap.
- [ ] **Decision gate:** pick A/B/C. Everything below assumes **B**.

## Phase 1: release pipeline (CI)

- [ ] Version source of truth: `FW_VERSION` in `version.h`; a tag `vX.Y.Z` must match it (CI fails
      on mismatch). Compare versions as `major.minor.patch` numbers, never as strings.
- [ ] New workflow `release.yml`, triggered by tag `v*`: run tests, build all three envs plus
      `buildfs`, then create a GitHub Release with:
      - `motoclock-<ver>-rider.bin`, `motoclock-<ver>-kids.bin`, `motoclock-<ver>-fs.bin`
      - `manifest.json`: `{version, variants:{rider:{file,size,sha256}, kids:{...}}, minVersion, notes}`
- [ ] Sign the images in CI. Generate the keypair once; the **private key is a GitHub Actions secret**,
      the **public key is committed** (`firmware/include/ota_pubkey.h`). Document key rotation and
      what happens if the key is lost (devices need one serial/browser flash with a new key).
- [ ] Keep `ci.yml` artifacts as they are for non-release pushes.

## Phase 2: relay (only for option B)

- [ ] A minimal worker: `GET /manifest.json` and `GET /fw/<variant>.bin` read the latest Release
      through the GitHub API and stream it over HTTP; cache the manifest ~10 min (rate limits).
      Lives in `tools/ota-relay/` with its own README and deploy steps.
- [ ] The relay is **untrusted by design**: it can delay or withhold updates but cannot install
      anything, because the device verifies the signature.
- [ ] Relay base URL is a config field (`ota.url`), default compiled in `config.h`.

## Phase 3: device side

New files: `firmware/include/ota.h`, `firmware/src/ota.cpp`. Pure logic (version compare, manifest
parse/validate) goes in `firmware/lib/motologic` so it is unit-tested on the host.

- [ ] **Check:** once per 24 h (plus once ~2 min after boot), non-blocking, only when WiFi is up and
      free heap is above a guard. Fetch the manifest with `HTTPClient` (HTTP/1.0, same pattern as
      `weather.cpp`). Store `latestVersion`, `checkedAt` in RAM; do not write flash for this.
- [ ] **Variant:** the build defines which variant it is (`KIDS_MODE` → `kids`, else `rider`); a kids
      clock must never download the rider image and vice versa.
- [ ] **Install** (only on user request via an authenticated, CSRF-token-protected POST):
      stream to `Update`, verify size and SHA-256 against the manifest *and* the signature, then
      reboot. Reject images larger than free sketch space *before* downloading. Refuse downgrades
      unless explicitly forced.
- [ ] **Filesystem:** the `littlefs` image is *not* part of the pull update. Config lives there and
      must survive. Changes to `config.json` go through the existing version/migration field.
- [ ] **Safety:** never install while a weather fetch or config save (`updateConfig`) is in progress;
      pause the display loop and show "Updating…" with progress on the OLED; on any failure keep
      running the old image and log why (`Update.getError()`). The core's OTA is atomic: the old
      image stays until the new one is complete and verified.
- [ ] **Boot confirmation:** count boots after an update; if the new image crashes before it has
      completed one weather fetch, record it in a flash flag and stop offering that version.
      (ESP8266 has no hardware rollback, so this is best-effort; see risks.)

## Phase 4: web UI and display

- [ ] `GET /api/ota` → `{current, latest, checkedAt, updateAvailable, variant, canInstall, error}`.
- [ ] `POST /api/ota/check` and `POST /api/ota/install` (both authenticated + token).
- [ ] Update the *OTA Update* card in `webserver.cpp`: show current version + git hash, "Check now",
      "Update to X.Y.Z" with release notes, a progress/status line, and keep the manual upload as an
      "advanced" fallback.
- [ ] A small `UPD` status tag on the OLED when an update is available (same style as `OLD`/`TMR`).
      Config field `ota.autoCheck` (default on) and `ota.url`; add to the config schema, validation
      and `README.md` settings table.

## Phase 5: tests and docs

- [ ] Host tests (`test/test_logic`): version compare (`0.2.0 < 0.10.0`), manifest parsing (missing
      fields, wrong variant, oversize), downgrade refusal, check scheduling.
- [ ] CI: add a step that fails if the tag, `FW_VERSION` and manifest disagree; build still runs for
      all three envs and checks the size limit from Phase 0.
- [ ] Hardware test checklist (cannot be done in CI): update over WiFi, wrong signature rejected,
      WiFi dropped mid-download, power cut mid-download, kids ↔ rider mismatch, update with a full
      filesystem, large `config.json`.
- [ ] README: replace "Updating the firmware over the air" with the pull flow, the manual fallback,
      and a Security notes entry (signed images, plain-HTTP transport).

## Risks

- **Flash headroom on 1 MB** is the hard limit; Phase 0 gives the number and every later phase must
  respect it. If signing + manifest code pushes us over, shrink first (e.g. drop the debug-only code).
- **No hardware rollback** on ESP8266: a bad but validly signed image can still brick remotely. The
  fallback is serial flashing. Mitigation: only release tags that passed the hardware checklist.
- **Key management:** a leaked private key means anyone can sign firmware; a lost one strands devices.
- **Not run on hardware.** Like the earlier plans, nothing here is verified until flashed on a real
  ESP-01; the code must say so in its status notes.

## Suggested order and effort

1. Phase 0 spike (half a day) → decision gate.
2. Phase 1 + 3 (core path, with host tests) → first signed manual-install flow, no relay yet,
   by pointing `ota.url` at a local HTTP server.
3. Phase 2 relay, then Phase 4 UI, then Phase 5 hardware checklist and docs.

## Open questions for the owner

1. Is running a small relay acceptable (free Cloudflare Worker or a home server), or should we try
   TLS on the device first?
2. One shared signing key in a GitHub secret, or a key kept offline and signed locally?
3. Should a release include the filesystem image, or stay firmware-only (recommended)?
