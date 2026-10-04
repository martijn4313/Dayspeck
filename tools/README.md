# Bitmap tool

Converts pixel-art PNGs into the `PROGMEM` arrays in `firmware/include/bitmaps.h` and simulates the
128×64 OLED so a design can be checked before flashing. The simulator mirrors the firmware's drawing
code (`firmware/src/display.cpp`), including its 5×7 font, so the preview matches the device.

Requires Python ≥ 3.9 and Pillow (`pip install -r tools/requirements.txt`). The GUI also needs `tkinter`
(on Debian/Ubuntu: `sudo apt install python3-tk python3-pil.imagetk`).

## GUI

```sh
python tools/png_to_bitmap.py        # or: cd tools && python -m bitmaptool gui
```

- *Open firmware dir* loads the arrays of an existing `bitmaps.h`.
- Pick a slot, import a PNG (threshold and *Invert* options sit above the slot list; transparent
  pixels are off) or draw with the pixel editor, then *Save to .h*. The header is regenerated in its
  canonical form and the previous version is kept as `bitmaps.h.bak`.

## Command line

Run from `tools/` (or add it to `PYTHONPATH`):

```sh
python -m bitmaptool convert sun.png --name sun                        # print the C array
python -m bitmaptool convert sun.png --name sun --out ../firmware/include/bitmaps.h
python -m bitmaptool regen assets/manifest.json            # rebuild bitmaps.h from the PNG sources
python -m bitmaptool regen assets/manifest.json --check    # CI: fail if bitmaps.h is out of date
python -m bitmaptool export ../firmware/include/bitmaps.h assets   # bootstrap PNGs from a header
python -m bitmaptool render --view weekly --out weekly.png         # headless preview (no Tk needed)
python -m bitmaptool render --weather rain --night --badge warn --out rain.png
# an animation: windy autumn day, 20 warm-up frames then 60 frames at 15 FPS
python -m bitmaptool render --weather wind --wind 45 --autumn --gif --frames 20 --gif-frames 60 --out wind.gif
```

## Asset workflow

1. The PNGs in `tools/assets/` are the source of truth; `firmware/include/bitmaps.h` is generated from them
   through `assets/manifest.json`.
2. Edit or add a PNG (white on black, or use `--invert`), add it to the manifest if it is new.
3. `python -m bitmaptool regen assets/manifest.json`, then build the firmware.
4. CI runs `pytest tools/tests`, which includes a check that the committed header matches the PNGs.

Slots the firmware uses: the village `scene_day` and `scene_night` (64×50: the same scene, with the street
lamp lit at night; the empty street band at the bottom holds the wind and rain text), `sun`, `moon`,
`arrow_ur`, `arrow_dr`, `arrow_r`, the four autumn leaves `leaf_1..4` (5×5, the poses of a tumbling leaf), the
four rain drops `rain_drop_1..4` and the four splashes `splash_1..4`. Each rain drop and splash picks one of the
four variants at random. The sprites are optional: when `RAIN_DROP_1_BMP_W` / `SPLASH_1_BMP_W` are not
defined in `bitmaps.h` the firmware falls back to simple procedural drops and splashes.

## Pictures for the manual

`python tools/docs_screenshots.py` regenerates `docs/images/rider-today.png`, `hero.png` and the two animations
`rider-wind.gif` and `kids-wind.gif` from the simulator and `bitmaps.h`; `python tools/webui_screenshots.py`
does the web UI pictures. Run them again after changing the artwork or the drawing code. The kids screens
(`kids-clothes.png`, `kids-weather.png`, `kids-countdown.png`) are not part of the simulator: they are rendered
by compiling `firmware/src/display.cpp` on the host against the Adafruit GFX library.

## Layout

```
tools/png_to_bitmap.py      launcher for the GUI
tools/docs_screenshots.py   the manual's rider and kids pictures
tools/webui_screenshots.py  the manual's web UI pictures
tools/bitmaptool/
  convert.py                PNG <-> C array, header read/write, manifest regeneration (no GUI deps)
  canvas.py, font5x7.py     128x64 pixel buffer, drawing primitives, the firmware's text font
  procedural.py, rain.py    ports of the firmware's procedural effects and rain animation
  wind.py                   gusts and autumn leaves (rider and kids areas), mirrors the firmware
  bezel.py                  draws a canvas as an OLED module (bezel, dots, glow) for the manual
  scene.py                  the whole screen (today and weekly views)
  gui.py                    Tk application
  cli.py                    command line
tools/tests/                pytest (conversion, header round trip, simulator vs firmware geometry)
```

## Tests

```sh
pip install -r tools/requirements-dev.txt
cd tools && python -m pytest tests
```
