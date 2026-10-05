#!/usr/bin/env python3
"""Regenerate the ride screens' pictures of the manual: docs/images/rider-today.png and rider-wind.gif.

They are drawn by the host simulator (tools/bitmaptool), which mirrors firmware/src/display.cpp, with the
artwork of firmware/include/bitmaps.h. The other screens and the hero picture come from
tools/screen_pictures.py, a host build of the firmware's own drawing code; run it after this one.

    python tools/docs_screenshots.py [--out docs/images]
"""
import argparse
from pathlib import Path

from PIL import Image

from bitmaptool.bezel import device_image
from bitmaptool.canvas import OLEDCanvas
from bitmaptool.cli import load_scene_assets
from bitmaptool.constants import RAIN_FRAME_INTERVAL_MS
from bitmaptool.scene import SceneComposer, SceneState

ROOT = Path(__file__).resolve().parent.parent
SCREEN_W, SCREEN_H, GAP = 540, 284, 16     # one module at scale 4, and the space between two in a picture


def rider_state(**kwargs) -> SceneState:
    state = SceneState(**kwargs)
    load_scene_assets(state, ROOT / "firmware" / "include" / "bitmaps.h")
    return state


def rider_screen(frames: int, **kwargs) -> Image.Image:
    state = rider_state(**kwargs)
    canvas = OLEDCanvas()
    for _ in range(frames):                  # the rain and wind animations advance once per frame
        SceneComposer.compose(canvas, state)
    return device_image(canvas.pixels)


# The three rider screens: good and calm, caution in the rain (tomorrow), don't ride on a windy autumn night
RIDER_SCREENS = [
    (1, dict(badge_type="check", weather="clear", night=False, temp_str="18C", trend="up", wind_speed=12, precip_mm=0.0)),
    (14, dict(badge_type="warn", weather="rain", night=False, temp_str="9C", trend="down", wind_speed=28,
              precip_mm=0.8, tomorrow=True)),
    (44, dict(badge_type="x", weather="wind", night=True, temp_str="6C", trend="flat", wind_speed=52,
              precip_mm=0.0, autumn=True)),
]


def row(images: list) -> Image.Image:
    out = Image.new("RGBA", (len(images) * SCREEN_W + (len(images) - 1) * GAP, SCREEN_H), (0, 0, 0, 0))
    for i, img in enumerate(images):
        out.paste(img, (i * (SCREEN_W + GAP), 0))
    return out


def save_gif(frames: list, path: Path):
    quantized = [f.convert("RGBA").convert("RGB").quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
                 for f in frames]
    quantized[0].save(path, save_all=True, append_images=quantized[1:], duration=RAIN_FRAME_INTERVAL_MS, loop=0,
                      optimize=True)


def rider_wind_gif(path: Path, warmup: int = 20, count: int = 60):
    state = rider_state(badge_type="check", weather="wind", night=False, temp_str="11C", trend="flat",
                        wind_speed=45, precip_mm=0.0, autumn=True)
    canvas = OLEDCanvas()
    for _ in range(warmup):
        SceneComposer.compose(canvas, state)
    frames = []
    for _ in range(count):
        SceneComposer.compose(canvas, state)
        frames.append(device_image(canvas.pixels, scale=3))
    save_gif(frames, path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "docs" / "images"))
    args = ap.parse_args()
    out = Path(args.out)

    screens = [rider_screen(frames, **kw) for frames, kw in RIDER_SCREENS]
    row(screens).save(out / "rider-today.png")
    print("wrote", out / "rider-today.png")

    rider_wind_gif(out / "rider-wind.gif")
    print("wrote", out / "rider-wind.gif")


if __name__ == "__main__":
    main()
