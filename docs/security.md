# Security notes

- The web UI, the API and OTA all need the admin password (HTTP Basic). Basic auth is **not
  encrypted**: use a network you trust, and change the default password.
- Firmware updates travel over plain HTTP but must carry a valid RSA signature from the release key
  (once `ota_pubkey.h` holds a key). Someone between the device and the relay can delay or block
  updates, but cannot install their own firmware or an older release.
- The ESP8266 is too slow for TLS, so weather requests are plain HTTP. No account, key or
  personal data is sent — only your configured coordinates.
- Writes need a per-boot token, which protects against forged requests from other web pages.
- DNS rebinding is not blocked.
