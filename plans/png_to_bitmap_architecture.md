# `png_to_bitmap.py` — Script Architecture Memo

## 1. Purpose

`tools/png_to_bitmap.py` is a standalone Python GUI application with two responsibilities:

1. **Asset Pipeline** — Import 1-bit PNG pixel art and convert it to C-style `PROGMEM` byte arrays for direct use in `firmware/include/bitmaps.h`.
2. **Design Sandbox** — Simulate the SSD1306 128×64 OLED display in a desktop window so static assets, procedural weather effects, and the full composite scene can be visually designed and verified *before* any firmware is flashed.

Launch: `python tools/png_to_bitmap.py`

---

## 2. Dependencies

| Library | Version | Notes |
|---|---|---|
| `Pillow` | `>=10.0` | PNG load, threshold to 1-bit, pixel access |
| `tkinter` | stdlib | GUI window, widgets, Canvas, file dialogs |

```
# tools/requirements.txt
Pillow>=10.0
```

---

## 3. Window Layout

```
┌─────────────────────────────────────────────────────────────────────────┐
│  MotoWeather Bitmap Tool                                                │
├──────────────────────┬──────────────────────────┬───────────────────────┤
│  ASSET BROWSER       │  OLED PREVIEW (4× scale) │  CONTROLS            │
│                      │  ┌──────────────────────┐ │                      │
│  [Import PNG]        │  │ 512 × 256 px canvas  │ │  Badge:              │
│  [Open firmware dir] │  │ (simulates 128×64)   │ │  ○ ✓  ○ !  ○ X      │
│                      │  └──────────────────────┘ │                      │
│  ┌──────────────────┐│                           │  Weather:            │
│  │ skyline_base     ││  C ARRAY OUTPUT           │  ☐ Rain  [===] 1–3  │
│  │ sun              ││  ┌──────────────────────┐ │  ☐ Snow  [===] 1–3  │
│  │ moon             ││  │ const uint8_t PROGMEM│ │  ☐ Wind  [===] km/h │
│  │ cloud            ││  │ skyline_base_bmp[] = │ │  ☐ Night mode       │
│  │ (from bitmaps.h) ││  │ { 0x00, 0xFF, ... }; │ │                      │
│  └──────────────────┘│  └──────────────────────┘ │  Temp overlay:       │
│                      │  [Copy]  [Save to .h]      │  [  3°C  ]          │
│  Position on canvas: │                           │                      │
│  X: [__]  Y: [__]    │                           │  [Refresh preview]   │
└──────────────────────┴──────────────────────────┴───────────────────────┘
```

---

## 4. Module Structure

```
tools/png_to_bitmap.py
│
├── OLEDCanvas                     # In-memory 128×64 pixel buffer + draw primitives
│   ├── pixels: list[list[bool]]   # [y][x], True = white
│   ├── clear()
│   ├── set_pixel(x, y, on)
│   ├── draw_line(x0, y0, x1, y1)
│   ├── draw_rect(x, y, w, h, fill)
│   ├── draw_circle(cx, cy, r, fill)
│   ├── blit(x, y, bitmap_2d)      # Paste a 2D bool array at offset
│   └── to_photoimage(scale) -> tk.PhotoImage   # For tkinter Canvas
│
├── Converter                      # PNG ↔ C array
│   ├── load_png(path) -> list[list[bool]]
│   ├── to_c_array(bitmap, name) -> str
│   └── parse_bitmaps_h(path) -> dict[str, list[list[bool]]]
│
├── Procedural                     # Python mirrors of firmware C++ functions
│   ├── draw_giant_badge(canvas, type: str)
│   ├── apply_night_overlay(canvas, seed: int)
│   ├── draw_procedural_rain(canvas, intensity: int)        # Legacy procedural rain
│   ├── draw_sprite_rain(canvas, intensity: int, seed: int) # New sprite-based rain
│   ├── draw_procedural_snow(canvas, intensity: int, seed: int)
│   └── draw_procedural_wind(canvas, speed: int)
│
├── SceneComposer                  # Orchestrates layer rendering
│   ├── compose(canvas, state: SceneState)
│   └── SceneState dataclass
│       ├── badge_type: str        # "check" | "warn" | "x"
│       ├── skyline_bmp: 2D list
│       ├── weather: str           # "rain" | "snow" | "wind" | "clear"
│       ├── intensity: int
│       ├── night: bool
│       ├── wind_speed: int
│       ├── temp_str: str
│       ├── seed: int
│       ├── use_sprite_rain: bool  # Toggle between procedural and sprite rain
│       ├── rain_drops: list       # List of RainDrop objects
│       ├── rain_splashes: list    # List of Splash objects
│       ├── rain_sprites: list     # List of rain drop sprite bitmaps
│       └── splash_sprite: list    # Splash sprite bitmap
│
├── RainAnimation                  # Sprite-based rain animation system
│   ├── RainDrop dataclass
│   │   ├── x: int                # Current x position (64-127)
│   │   ├── y: int                # Current y position (-6 to 41)
│   │   ├── speed: int            # Fall speed (1-2 pixels/frame)
│   │   ├── sprite_variant: int   # Which sprite to use (0-3)
│   │   └── active: bool          # Is this drop currently falling?
│   ├── Splash dataclass
│   │   ├── x: int                # Splash x position
│   │   ├── y: int                # Splash y position (always at horizon)
│   │   ├── frame_counter: int    # Frames remaining for splash animation
│   │   └── active: bool          # Is this splash currently visible?
│   ├── init_rain_animation(canvas, seed: int) -> tuple[list[RainDrop], list[Splash]]
│   ├── update_rain_animation(drops: list[RainDrop], splashes: list[Splash], seed: int)
│   └── draw_rain_animation(canvas, drops: list[RainDrop], splashes: list[Splash], rain_sprites: list, splash_sprite: list)
│
└── App (tk.Tk subclass)           # Main GUI window
    ├── _build_left_panel()        # Asset browser + position inputs
    ├── _build_centre_panel()      # OLED preview canvas + C array output
    ├── _build_right_panel()       # Controls
    ├── _on_import_png()           # File dialog → Converter.load_png → asset list
    ├── _on_open_firmware_dir()    # Folder dialog → parse_bitmaps_h → asset list
    ├── _on_asset_select()         # Update active bitmap in browser
    ├── _refresh_preview()         # OLEDCanvas → SceneComposer → to_photoimage
    └── _on_export()               # to_c_array → text box + save dialog
```

---

## 5. `OLEDCanvas` Class

The central data structure is a `128 × 64` boolean array (`True` = white, `False` = black), mirroring the SSD1306 framebuffer.

**Draw primitives** use Bresenham's line algorithm and midpoint circle algorithm, matching Adafruit GFX output pixel-for-pixel. This ensures the preview matches the real display exactly.

`to_photoimage(scale: int) -> tk.PhotoImage` upscales the buffer for the tkinter Canvas widget. Each OLED pixel becomes a `scale × scale` block. Default scale: `4` → 512×256 px window.

**Phosphor-green colour mapping:**
| OLED state | RGB |
|---|---|
| White pixel (on) | `#39FF14` (bright phosphor green) |
| Black pixel (off) | `#051405` (near-black green tint) |

---

## 6. Converter Functions

### `load_png(path: str) -> list[list[bool]]`
1. Open with `Pillow`.
2. Convert to `"L"` (grayscale).
3. Threshold at 128: pixel ≥ 128 → `True`, else `False`.
4. Return as `[row][col]` 2D list.

### `to_c_array(bitmap: list[list[bool]], name: str) -> str`
Packs bits into bytes per **Adafruit GFX `drawBitmap()` convention**:
- Row-major, MSB = leftmost pixel, rows padded to full bytes.

Output:
```c
// name  WxH
const uint8_t PROGMEM name_bmp[] = {
    0xFE, 0x01, 0x82, ...
};
#define NAME_BMP_W  64
#define NAME_BMP_H  20
```

### `parse_bitmaps_h(path: str) -> dict[str, list[list[bool]]]`
Reads `firmware/include/bitmaps.h` and:
1. Finds all `const uint8_t PROGMEM xxx_bmp[] = { ... };` blocks via regex.
2. Finds the matching `#define XXX_BMP_W` and `#define XXX_BMP_H` values.
3. Unpacks the byte array back to a 2D bool array using the same Adafruit GFX bit order.
4. Returns a dict keyed by identifier name (e.g. `"skyline_base_bmp"`).

This allows the tool to render existing firmware assets in the preview when compositing a new bitmap.

---

## 7. Procedural Effect Functions

Each function draws into a provided `OLEDCanvas`. The skyline card occupies `x: 64–127`, `y: 0–31` on the full 128×64 canvas.

### `draw_giant_badge(canvas, type: str)`
- Target: left half `x: 0–63`, `y: 0–63`. Centre: `(32, 32)`.
- Three concentric circles at radii `28`, `27`, `26` → thick border ring.
- `"check"`: Two `draw_line()` calls forming a ✓.
- `"warn"`: Thick vertical bar + 3×3 bottom dot for `!`.
- `"x"`: Two diagonal lines crossing at `(32, 32)`.

### `apply_night_overlay(canvas, seed: int = 42)`
- Random number generator seeded with `seed` for stable preview.
- Draw 3–5 single white pixels randomly in sky region `y: 0–11`, `x: 64–127`.
- Draw a 2×2 filled white rectangle at the streetlight bulb coordinates (constants `STREETLIGHT_BX`, `STREETLIGHT_BY`).

### `draw_procedural_rain(canvas, intensity: int)` *(Legacy)*
- Intensity → number of lines: `1 → 3`, `2 → 6`, `3 → 10`.
- Each line: `draw_line(x, y, x-2, y+4)` where `x` values spread evenly across the 64-wide card (`x: 64–127`), `y` values in `0–27`.

### `draw_sprite_rain(canvas, intensity: int, seed: int = 42)`
- **New sprite-based rain animation system.**
- Uses `RainAnimation` module to manage rain drops and splashes.
- Intensity controls number of drops: `1 → 5 drops`, `2 → 6 drops`, `3 → 8 drops`.
- Each drop: random position, random speed (1-2 pixels/frame), random sprite variant (0-3).
- Drops fall diagonally: `x -= 1, y += speed` (wind-blown effect).
- When drop hits horizon (y=41): create splash effect, reset drop to top.
- Splash sprite appears briefly (2-3 frames) at impact position.
- Requires rain drop sprites and splash sprite to be loaded in `SceneState`.

### `draw_procedural_snow(canvas, intensity: int, seed: int = 42)`
- Scatter `intensity × 4` white pixels at random positions in the skyline card (`x: 64–127`, `y: 0–31`).
- At `intensity >= 2`: draw a horizontal white line at `y = CHURCH_ROOF_Y` (the church roof row).

### `draw_procedural_wind(canvas, speed: int)`
- Only active when `speed >= WIND_THRESHOLD` (default: 25 km/h).
- Draw 2–3 dashed horizontal lines in the sky area (`y: 5`, `9`, `14`), `x: 64–127`.
- Dash pattern: 4 on, 2 off, 3 on, 1 off, 2 on.

---

## 8. Scene Composer

### `SceneState` dataclass
```python
@dataclass
class SceneState:
    badge_type: str = "check"     # "check" | "warn" | "x"
    skyline_bmp: list = None      # 2D bool array or None
    extra_bmps: list = None       # list of (x, y, 2D bool array) for firmware bitmaps
    weather: str = "clear"        # "rain" | "snow" | "wind" | "clear"
    intensity: int = 1
    night: bool = False
    wind_speed: int = 0
    temp_str: str = ""
    trend: str = "flat"           # "up" | "down" | "flat"
    seed: int = 42
```

### `SceneComposer.compose(canvas, state)`
Renders in order:
1. `canvas.clear()`
2. `draw_giant_badge(canvas, state.badge_type)` — left half.
3. If `state.skyline_bmp`: `canvas.blit(64, 12, state.skyline_bmp)`.
4. For each `(x, y, bmp)` in `state.extra_bmps`: `canvas.blit(x, y, bmp)`.
5. If `state.night`: `apply_night_overlay(canvas, state.seed)`, blit moon at `(MOON_X, MOON_Y)`.
6. Else: blit sun at `(SUN_X, SUN_Y)`.
7. If `state.weather == "rain"`:
   - If `state.use_sprite_rain` and `state.rain_sprites` loaded: `draw_sprite_rain(canvas, state.intensity, state.seed)`.
   - Else: `draw_procedural_rain(canvas, state.intensity)` (legacy fallback).
8. If `state.weather == "snow"`: `draw_procedural_snow(canvas, state.intensity, state.seed)`.
9. If `state.weather == "wind"`: `draw_procedural_wind(canvas, state.wind_speed)`.
10. Draw `state.temp_str` text at `(TEMP_X, TEMP_Y)` with a 2 px black background box.
11. Blit trend arrow bitmap at `(ARROW_X, ARROW_Y)`.
12. Draw bottom card: blit weather icon at `(64, 33)`, draw wind speed text, draw precipitation bars.

---

## 9. Coordinate Constants

Defined at the top of `png_to_bitmap.py` and adjusted once `skyline_base.png` art is finalised:

```python
# Adjust after finalising skyline art
SKYLINE_X         = 64    # Left edge of skyline card on full canvas
SKYLINE_Y         = 12    # Top of skyline bitmap within top card
SKY_YMAX          = 11    # Bottom of sky region (stars, wind lines)
HORIZON_Y         = 41    # Bottom of skyline buildings (rain collision boundary)
STREETLIGHT_BX    = 120   # Streetlight bulb X (full canvas coords)
STREETLIGHT_BY    = 22    # Streetlight bulb Y (full canvas coords)
CHURCH_ROOF_Y     = 14    # Y row for snow accumulation line
BOTTOM_CARD_Y     = 32    # Top of bottom card within right half
SUN_X,  SUN_Y     = 116,  2
MOON_X, MOON_Y    = 116,  2
TEMP_X, TEMP_Y    = 65,   1   # Temperature text top-left
ARROW_X, ARROW_Y  = 91,   1   # Trend arrow (right of a 4 character temperature)
WIND_THRESHOLD    = 25        # km/h minimum to draw wind effect
RAIN_AREA_X_START = 64       # Rain region left boundary
RAIN_AREA_X_END   = 127      # Rain region right boundary
RAIN_AREA_Y_START = 0        # Rain region top boundary
RAIN_AREA_Y_END   = 41       # Rain region bottom boundary (horizon)
MAX_RAIN_DROPS    = 8        # Maximum simultaneous rain drops
MAX_SPLASHES      = 8        # Maximum simultaneous splash effects
```

---

## 10. GUI Interactions

| Widget | Type | Action |
|---|---|---|
| "Import PNG" | Button | `filedialog.askopenfilename` → `load_png` → add to asset list |
| "Open firmware dir" | Button | `filedialog.askdirectory` → `parse_bitmaps_h` → add to asset list |
| Asset listbox | Listbox | Select active bitmap; update X/Y position spinboxes |
| X / Y spinboxes | Spinbox | Set `(x, y)` blit position for selected asset in composite |
| Badge radio buttons | Radiobutton | Set `state.badge_type`, trigger `_refresh_preview` |
| Rain / Snow / Wind checkboxes | Checkbutton | Set `state.weather`, trigger refresh |
| Sprite Rain checkbox | Checkbutton | Set `state.use_sprite_rain`, trigger refresh |
| Intensity sliders | Scale | Set `state.intensity`, trigger refresh |
| Wind speed slider | Scale | Set `state.wind_speed`, trigger refresh |
| Night mode checkbox | Checkbutton | Set `state.night`, trigger refresh |
| Temp overlay entry | Entry | Set `state.temp_str`, trigger refresh |
| "Refresh preview" | Button | Force `_refresh_preview()` call |
| "Copy" | Button | Copy C array text box contents to clipboard |
| "Save to .h" | Button | `filedialog.asksaveasfilename` → write C array output |

All slider / checkbox change events are bound to `_refresh_preview` so the OLED preview updates live without needing to click Refresh manually.
