#!/usr/bin/env python3
"""Regenerate the rider and kids pictures of the manual: docs/images/rider-today.png, hero.png,
rider-wind.gif and kids-wind.gif.

The rider screens are drawn by the host simulator (tools/bitmaptool), which mirrors firmware/src/display.cpp,
with the artwork of firmware/include/bitmaps.h. The kids screens are not part of the simulator: the kids
animation takes the dots of the existing screenshot in docs/images/kids-weather.png and lets the leaves blow
over it with the same animation code as the firmware.

    python tools/docs_screenshots.py [--out docs/images]
"""
import argparse
import random
from pathlib import Path

from PIL import Image

from bitmaptool.bezel import device_image
from bitmaptool.canvas import OLEDCanvas
from bitmaptool.cli import load_scene_assets
from bitmaptool.constants import RAIN_FRAME_INTERVAL_MS
from bitmaptool.scene import SceneComposer, SceneState
from bitmaptool.wind import KIDS_AREA, WindAnimation

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


def grid_of(image: Image.Image, x0: int, y0: int) -> list:
    """The 128x64 dots of a module in a screenshot (a dot is 3x3 px on a 4 px pitch, 14 px bezel)."""
    px = image.convert("RGB").load()
    return [[px[x0 + 14 + 4 * x + 2, y0 + 14 + 4 * y + 2][1] > 150 for x in range(128)] for y in range(64)]


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


def kids_wind_gif(path: Path, source: Path, warmup: int = 20, count: int = 60):
    """The kids weather screen (an evening, rain tomorrow morning, a windy afternoon) with autumn leaves
    blowing over it: the fourth screen of kids-weather.png."""
    base = grid_of(Image.open(source), SCREEN_W + GAP, SCREEN_H + 16)
    state = rider_state()
    gusts, leaves = WindAnimation.init()
    rng = random.Random(11)
    frames = []
    for i in range(warmup + count):
        WindAnimation.update(gusts, leaves, rng, 30, False, True, KIDS_AREA)
        if i < warmup:
            continue
        canvas = OLEDCanvas()
        canvas.pixels = [row_[:] for row_ in base]
        WindAnimation.draw(canvas, gusts, leaves, state.leaf_bmps, KIDS_AREA)
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

    # hero: the rider screen next to the second kids clothes screen (an autumn morning)
    kids = Image.open(out / "kids-clothes.png").convert("RGBA").crop(
        (SCREEN_W + GAP, 0, 2 * SCREEN_W + GAP, SCREEN_H))
    row([screens[0], kids]).save(out / "hero.png")
    print("wrote", out / "hero.png")

    rider_wind_gif(out / "rider-wind.gif")
    print("wrote", out / "rider-wind.gif")
    kids_wind_gif(out / "kids-wind.gif", out / "kids-weather.png")
    print("wrote", out / "kids-wind.gif")


if __name__ == "__main__":
    main()
