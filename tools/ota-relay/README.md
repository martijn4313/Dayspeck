# Dayspeck update relay

The ESP-01 has no room for TLS next to a second firmware image, and GitHub only serves HTTPS. This
Cloudflare Worker fetches the latest GitHub Release and hands it to the device over plain HTTP.

It does not need to be trusted. The device installs nothing unless both the manifest and the image
carry a valid signature from the release key (`tools/ota_tool.py`). A broken or hostile relay can
withhold updates, but it cannot install its own firmware or an older release.

## Deploy (free Cloudflare account)

```sh
cd tools/ota-relay
npx wrangler login
npx wrangler deploy
```

`wrangler deploy` prints the address, e.g. `https://dayspeck-ota.<you>.workers.dev`. On the device
use it with **`http://`**: in the web UI, *Firmware Update → Update server*. To give every new
device the address, set `OTA_DEFAULT_URL` in `firmware/include/config.h` (or in `secrets.h`).

Check that plain HTTP works: `curl -i http://dayspeck-ota.<you>.workers.dev/ota-manifest.txt`
must answer `200` with the manifest, not a redirect to `https://`. If it redirects, turn off
*Always Use HTTPS* for the zone, or use a custom domain without it.

For a fork, change `REPO` in `wrangler.toml`.

### Upgrading from the MotoClock-era relay

The project was renamed from MotoClock, so the worker is now called `dayspeck-ota` and the release files
`dayspeck-*.bin.gz`. The relay still serves the old `motoclock-*.bin.gz` names too, so deploy it **before**
you publish the first release under the new names. Deploying under the new name creates a new worker with a
new `workers.dev` address; the old `motoclock-ota` worker keeps running until you delete it. Devices keep the
address they were given, so either leave the old worker up for as long as you have devices pointing at it
(set its `REPO` to the current repository and redeploy it), or change *Update server* in each device's web UI.

## Without Cloudflare

Any plain HTTP server works if it serves the same layout:

```
<base>/ota-manifest.txt                      (from the latest release)
<base>/vX.Y.Z/dayspeck-rider.bin.gz
<base>/vX.Y.Z/dayspeck-kids.bin.gz
```

For example, on a Raspberry Pi: download the release assets into `vX.Y.Z/`, copy
`ota-manifest.txt` to the top folder, and run `python3 -m http.server 8080` there.
Replies must carry a `Content-Length` header (no chunked transfer encoding).
