// Dayspeck update relay: serves the latest GitHub Release over plain HTTP for the ESP-01,
// which has no room for TLS. The relay is not trusted: the device checks the signature of the
// manifest and of every image, so the worst a broken or hostile relay can do is withhold updates.
//
//   GET /ota-manifest.txt                     -> the latest release's ota-manifest.txt
//   GET /vX.Y.Z/dayspeck.bin.gz               -> that release's signed image
//   (older releases had dayspeck-rider / -kids, or the prefix motoclock-; they are still served)
//
// Configure the repository in wrangler.toml (REPO = "owner/name").

const MANIFEST = /^\/ota-manifest\.txt$/;
const IMAGE = /^\/(v\d{1,5}\.\d{1,5}\.\d{1,5})\/((?:dayspeck|motoclock)(?:-rider|-kids)?\.bin\.gz)$/;

export default {
  async fetch(request, env) {
    if (request.method !== "GET") {
      return new Response("Method not allowed\n", { status: 405 });
    }
    const path = new URL(request.url).pathname;
    let upstream;
    let ttl;
    let match;
    if (MANIFEST.test(path)) {
      upstream = `https://github.com/${env.REPO}/releases/latest/download/ota-manifest.txt`;
      ttl = 600;            // a new release reaches the devices within ten minutes
    } else if ((match = IMAGE.exec(path))) {
      upstream = `https://github.com/${env.REPO}/releases/download/${match[1]}/${match[2]}`;
      ttl = 86400;          // a released image never changes
    } else {
      return new Response("Not found\n", { status: 404 });
    }

    const reply = await fetch(upstream, { cf: { cacheEverything: true, cacheTtl: ttl } });
    if (!reply.ok) {
      return new Response("Upstream error\n", { status: reply.status === 404 ? 404 : 502 });
    }
    // Buffer the body so the device gets a Content-Length (its updater refuses chunked replies)
    const body = await reply.arrayBuffer();
    return new Response(body, {
      headers: {
        "Content-Type": path.endsWith(".txt") ? "text/plain" : "application/octet-stream",
        "Cache-Control": `public, max-age=${ttl}`,
      },
    });
  },
};
