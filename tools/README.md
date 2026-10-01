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
```

## Asset workflow

1. The PNGs in `tools/assets/` are the source of truth; `firmware/include/bitmaps.h` is generated from them
   through `assets/manifest.json`.
2. Edit or add a PNG (white on black, or use `--invert`), add it to the manifest if it is new.
3. `python -m bitmaptool regen assets/manifest.json`, then build the firmware.
4. CI runs `pytest tools/tests`, which includes a check that the committed header matches the PNGs.

Slots the firmware uses today: `skyline_base`, `sun`, `moon`, `arrow_ur`, `arrow_dr`, `arrow_r`. The rain
drop and splash sprites are optional: when `RAIN_DROP_1_BMP_W` / `SPLASH_1_BMP_W` are defined in
`bitmaps.h` the firmware draws them, otherwise it uses its procedural fallback.

## Layout

```
tools/png_to_bitmap.py      launcher for the GUI
tools/bitmaptool/
  convert.py                PNG <-> C array, header read/write, manifest regeneration (no GUI deps)
  canvas.py, font5x7.py     128x64 pixel buffer, drawing primitives, the firmware's text font
  procedural.py, rain.py    ports of the firmware's procedural effects and rain animation
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
