# Updating the firmware over the air

The device checks the latest GitHub Release once a day. When a newer version is out, the main screen
shows `UPD` in the top-left corner, and the web UI's *Firmware Update* card offers **Install X.Y.Z**.
Nothing installs by itself. The install takes about a minute (the display shows the progress), then
the device restarts. Settings are kept.

How it fits on an ESP-01 with 1 MB flash (details in the [OTA plan](https://github.com/martijn4313/WeatherWise/blob/main/plans/ota_plan.md)):

- **No TLS on the device**: it would not fit next to a second firmware image. A small relay
  (`tools/ota-relay`, a free Cloudflare Worker, or any plain HTTP server) passes the release on
  over HTTP.
- **Signed images**: the release workflow signs the manifest and every image with an RSA key.
  The device only installs an image whose signature matches the public key compiled into it, and
  only the exact image the signed manifest names. The relay does not need to be trusted.
- **Compressed images**: updates are gzip-compressed; the bootloader unpacks them.
- **64 KB filesystem** (`board_build.ldscript` in `platformio.ini`), so a compressed update fits next
  to the running firmware. CI fails if the firmware grows too large for that.

## One-time setup

1. **Signing key.** Run `python tools/ota_tool.py keygen` (needs `pip install cryptography`). Keep
   `ota_private.pem` safe and out of git (`*.pem` is ignored), add it as the repository secret
   `OTA_SIGNING_KEY` (`gh secret set OTA_SIGNING_KEY < ota_private.pem`), and commit the generated
   `firmware/include/ota_pubkey.h`. A build without that file cannot install pull updates.
   If the key is lost, generate a new one; devices then need one manual upload (or a serial flash)
   of a build with the new public key.
2. **Relay.** Deploy `tools/ota-relay` (see its [README](https://github.com/martijn4313/WeatherWise/blob/main/tools/ota-relay/README.md)) and put its `http://` address in the web UI
   under *Update server*, or in `OTA_DEFAULT_URL` in `config.h`.
3. **Serial flash, once.** Devices built before the 64 KB filesystem layout have too little free
   flash for any over-the-air update: flash them over serial (`pio run -t upload` and
   `pio run -t uploadfs`, or `motoclock-rider-serial.bin` and `motoclock-fs-serial.bin` from a
   release). The filesystem moves, so the WiFi settings, location and password start from scratch.

## Publishing a release

1. Set `FW_VERSION` in `firmware/include/version.h` to the new version and commit.
2. Tag it: `git tag -a v0.3.0 -m "One-line release notes shown in the web UI"` and push the tag.
3. `.github/workflows/release.yml` checks that the tag matches `FW_VERSION` and that the secret
   matches the committed public key, runs the tests, builds the rider and kids firmware, signs them
   and publishes the release. Devices see it within a day (or at once with *Check now*).

Only tag versions you have tried on a device: the ESP8266 cannot roll back to the old firmware if
a new one installs fine but then misbehaves (a serial flash fixes it).

## Manual upload

The *Manual upload* form in the web UI takes a signed `motoclock-rider.bin.gz` (or `-kids`) from a
release. To upload your own build, sign it with your key first:
`python tools/ota_tool.py sign --key ota_private.pem --in .pio/build/esp01_1m/firmware.bin --out fw.bin.gz`.
A build without a key accepts unsigned images; upload a compressed one
(`gzip -9 -k .pio/build/esp01_1m/firmware.bin`), as an uncompressed image does not fit.
