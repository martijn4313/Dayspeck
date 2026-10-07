#!/usr/bin/env python3
"""Draw the manual's pictures of the picture screens, the village, the countdown and the weather report with the
firmware's own code: docs/images/kids-clothes.png, kids-weather.png, kids-countdown.png, kids-village.png,
report.png, kids-wind.gif, report-wind.gif, and hero.png (four screens, one from rider-today.png, so run
tools/docs_screenshots.py first when the ride screens change).

It compiles firmware/src/display.cpp, firmware/lib/motologic and the Adafruit GFX library for the host, with the
small stand-ins in tools/screenshots_host/mock for the Arduino core and the SSD1306 driver, and runs the scenes in
tools/screenshots_host/screens.cpp. Needs a C++17 compiler (g++ or clang++) and the libraries PlatformIO
downloads (any firmware build fetches them, or `pio pkg install -e esp01_1m`).

    python tools/screen_pictures.py [--out docs/images] [--cxx g++]
"""
import argparse
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bitmaptool.bezel import device_image  # noqa: E402
from docs_screenshots import GAP, SCREEN_H, SCREEN_W, row, save_gif  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "tools" / "screenshots_host"
SOURCES = [HOST / "screens.cpp", ROOT / "firmware" / "src" / "display.cpp",
           ROOT / "firmware" / "lib" / "motologic" / "motologic.cpp"]


def gfx_library() -> Path:
    found = sorted((ROOT / ".pio" / "libdeps").glob("*/Adafruit GFX Library/Adafruit_GFX.cpp"))
    if not found:
        raise SystemExit("Adafruit GFX not found in .pio/libdeps: run `pio pkg install -e esp01_1m` (or any build) first")
    return found[0].parent


def build_and_run(cxx: str) -> dict:
    """Compile the host build, run it and return {picture: [frame, ...]}, a frame being 64 rows of 128 bools."""
    gfx = gfx_library()
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "screens"
        cmd = [cxx, "-std=c++17", "-O1", "-DARDUINO=10800", "-I", str(HOST / "mock"),
               "-I", str(ROOT / "firmware" / "include"), "-I", str(ROOT / "firmware" / "lib" / "motologic"),
               "-I", str(gfx), "-o", str(exe), *map(str, SOURCES), str(gfx / "Adafruit_GFX.cpp")]
        subprocess.run(cmd, check=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    pictures = defaultdict(list)
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if line.startswith("=== "):
            name = line.split()[1]
            pictures[name].append([[c == "#" for c in r] for r in lines[i + 1:i + 65]])
    return pictures


def grid(images: list, columns: int) -> Image.Image:
    rows = [row(images[i:i + columns]) for i in range(0, len(images), columns)]
    out = Image.new("RGBA", (rows[0].width, len(rows) * SCREEN_H + (len(rows) - 1) * GAP), (0, 0, 0, 0))
    for i, r in enumerate(rows):
        out.paste(r, (0, i * (SCREEN_H + GAP)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "docs" / "images"))
    ap.add_argument("--cxx", default="g++", help="C++ compiler (default g++)")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    pictures = build_and_run(args.cxx)
    screens = {name: [device_image(f) for f in frames] for name, frames in pictures.items()
               if not name.endswith("-wind")}
    layout = {"kids-clothes": 3, "kids-weather": 2, "kids-dinner": 3, "kids-night": 2, "kids-sunset": 2,
              "kids-umbrella": 1,
              "kids-countdown": 2,
              "kids-village": 3, "report": 2}
    for name, columns in layout.items():
        grid(screens[name], columns).save(out / f"{name}.png", optimize=True)
        print("wrote", out / f"{name}.png")
    for name in ("kids-wind", "report-wind", "kids-rain", "kids-snow", "kids-timelapse"):
        save_gif([device_image(f, scale=3) for f in pictures[name]], out / f"{name}.gif")
        print("wrote", out / f"{name}.gif")

    # hero: the ride rating, the village, the weather in pictures and the weather report
    ride = Image.open(ROOT / "docs" / "images" / "rider-today.png").convert("RGBA").crop((0, 0, SCREEN_W, SCREEN_H))
    hero = grid([ride, screens["kids-village"][0], screens["kids-weather"][0], screens["report"][0]], 2)
    hero.save(out / "hero.png")
    print("wrote", out / "hero.png")


if __name__ == "__main__":
    main()
