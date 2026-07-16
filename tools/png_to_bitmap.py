#!/usr/bin/env python3
"""
MotoWeather Bitmap Tool — PNG to PROGMEM C Array Converter + OLED Simulator
GUI application for converting pixel art PNGs to C byte arrays and previewing
the full 128x64 SSD1306 OLED display with procedural weather effects.

Usage:
    python tools/png_to_bitmap.py

See plans/png_to_bitmap_architecture.md for full design specification.
"""

import tkinter as tk
from tkinter import filedialog, messagebox, LabelFrame, Canvas, Button, Label, Frame
from tkinter import scrolledtext, Spinbox, Radiobutton, Checkbutton, Scale, Scrollbar, Entry, OptionMenu

BG_COLOR = "#1a1a1a"
FG_COLOR = "#ffffff"
ENTRY_BG = "#2b2b2b"
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional
import re
import random
import subprocess
import time

from PIL import Image, ImageTk


# ──────────────────────────────────────────────────────────────────────────────
# Coordinate Constants (adjust after finalising skyline art)
# ──────────────────────────────────────────────────────────────────────────────

SKYLINE_X = 64
SKYLINE_Y = 12
SKY_YMAX = 29          # 30 rows for town (y=12 to y=41)
BOTTOM_CARD_Y = 42     # Divider moved down to give 30px height
STREETLIGHT_BX = 120
STREETLIGHT_BY = 32     # Moved down with divider
CHURCH_ROOF_Y = 41     # Bottom of 30px town area (y=12 to y=41)
SUN_X, SUN_Y = 116, 2
MOON_X, MOON_Y = 116, 2
TEMP_X, TEMP_Y = 65, 1
ARROW_X, ARROW_Y = 87, 1
WIND_THRESHOLD = 25

# Rain animation constants
HORIZON_Y = 41          # Bottom of skyline buildings (rain collision boundary)
RAIN_AREA_X_START = 64  # Rain region left boundary
RAIN_AREA_X_END = 127   # Rain region right boundary
RAIN_AREA_Y_START = 0   # Rain region top boundary
RAIN_AREA_Y_END = 41    # Rain region bottom boundary (horizon)
MAX_RAIN_DROPS = 40     # Maximum simultaneous rain drops (raised to handle wind compensation)
MAX_SPLASHES = 30       # Maximum simultaneous splash effects (raised proportionally)
SPLASH_FRAMES = 3       # How many animation frames a splash is visible
RAIN_FRAME_INTERVAL_MS = 66  # 15 FPS in milliseconds
AVG_FALL_SPEED = 4.5    # Average fall speed (px/frame), midpoint of randint(3, 6)
AVG_FALL_FRAMES = HORIZON_Y / AVG_FALL_SPEED  # ~9 frames from top to horizon

# OLED colour schemes (ON, OFF pairs)
COLOR_SCHEMES = {
    "Green":  ("#39FF14", "#051405"),  # Classic phosphor-green
    "Blue":   ("#23a5cc", "#001515"),  # Cyan blue OLED
    "White":  ("#FFFFFF", "#1a1a1a"),  # White OLED
    "Yellow": ("#FFFF00", "#1a1a00"),  # Yellow OLED
    "Amber":  ("#FFBF00", "#1a1200"),  # Amber monochrome
}

# Default color scheme
DEFAULT_SCHEME = "Blue"

# Current colors (updated by dropdown)
OLED_ON = COLOR_SCHEMES[DEFAULT_SCHEME][0]
OLED_OFF = COLOR_SCHEMES[DEFAULT_SCHEME][1]

CANVAS_W = 128
CANVAS_H = 64
DEFAULT_SCALE = 4


# ──────────────────────────────────────────────────────────────────────────────
# OLEDCanvas — In-memory 128x64 pixel buffer
# ──────────────────────────────────────────────────────────────────────────────

class OLEDCanvas:
    """Mirrors the SSD1306 framebuffer as a 128x64 bool array."""

    def __init__(self):
        self.width = CANVAS_W
        self.height = CANVAS_H
        self.pixels = [[False] * self.width for _ in range(self.height)]

    def clear(self):
        self.pixels = [[False] * self.width for _ in range(self.height)]

    def set_pixel(self, x: int, y: int, on: bool = True):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y][x] = on

    def get_pixel(self, x: int, y: int) -> bool:
        if 0 <= x < self.width and 0 <= y < self.height:
            return self.pixels[y][x]
        return False

    def draw_line(self, x0: int, y0: int, x1: int, y1: int, on: bool = True):
        """Bresenham's line algorithm."""
        dx = abs(x1 - x0)
        dy = abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self.set_pixel(x0, y0, on)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    def draw_rect(self, x: int, y: int, w: int, h: int, fill: bool = False, on: bool = True):
        if fill:
            for dy in range(h):
                for dx in range(w):
                    self.set_pixel(x + dx, y + dy, on)
        else:
            for dx in range(w):
                self.set_pixel(x + dx, y, on)
                self.set_pixel(x + dx, y + h - 1, on)
            for dy in range(h):
                self.set_pixel(x, y + dy, on)
                self.set_pixel(x + w - 1, y + dy, on)

    def draw_circle(self, cx: int, cy: int, r: int, fill: bool = False, on: bool = True):
        """Midpoint circle algorithm."""
        x = r
        y = 0
        err = 1 - r
        while x >= y:
            if fill:
                for i in range(-x, x + 1):
                    self.set_pixel(cx + i, cy + y, on)
                    self.set_pixel(cx + i, cy - y, on)
                    self.set_pixel(cx + y, cy + i, on)
                    self.set_pixel(cx - y, cy + i, on)
            else:
                self.set_pixel(cx + x, cy + y, on)
                self.set_pixel(cx - x, cy + y, on)
                self.set_pixel(cx + x, cy - y, on)
                self.set_pixel(cx - x, cy - y, on)
                self.set_pixel(cx + y, cy + x, on)
                self.set_pixel(cx - y, cy + x, on)
                self.set_pixel(cx + y, cy - x, on)
                self.set_pixel(cx - y, cy - x, on)
            y += 1
            if err < 0:
                err += 2 * y + 1
            else:
                x -= 1
                err += 2 * (y - x) + 1

    def blit(self, x: int, y: int, bitmap: list[list[bool]]):
        """Paste a 2D bool array at offset (x, y)."""
        for row_idx, row in enumerate(bitmap):
            for col_idx, val in enumerate(row):
                if val:
                    self.set_pixel(x + col_idx, y + row_idx, True)

    def to_photoimage(self, scale: int = DEFAULT_SCALE) -> ImageTk.PhotoImage:
        """Convert to a PhotoImage for tk.Canvas display."""
        pil_img = Image.new("RGB", (self.width, self.height))
        for y in range(self.height):
            for x in range(self.width):
                color = OLED_ON if self.pixels[y][x] else OLED_OFF
                pil_img.putpixel((x, y), self._hex_to_rgb(color))
        if scale > 1:
            pil_img = pil_img.resize((self.width * scale, self.height * scale), Image.Resampling.NEAREST)
        return ImageTk.PhotoImage(pil_img)

    @staticmethod
    def _hex_to_rgb(hex_color: str) -> tuple:
        """Convert hex color string to RGB tuple."""
        hex_color = hex_color.lstrip('#')
        return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))


# ──────────────────────────────────────────────────────────────────────────────
# Converter — PNG ↔ C array
# ──────────────────────────────────────────────────────────────────────────────

class Converter:
    @staticmethod
    def load_png(path: str) -> list[list[bool]]:
        """Load a PNG and threshold to 1-bit bool array [row][col]."""
        img = Image.open(path).convert("L")
        w, h = img.size
        threshold = 128
        bitmap = [[bool((img.getpixel((x, y)) or 0) >= threshold) for x in range(w)] for y in range(h)]
        return bitmap

    @staticmethod
    def to_c_array(bitmap: list[list[bool]], name: str) -> str:
        """Convert 2D bool array to PROGMEM C array string (Adafruit GFX byte order)."""
        if not bitmap:
            return ""
        h = len(bitmap)
        w = len(bitmap[0])
        # Row-major, MSB = leftmost pixel, rows padded to full bytes
        bytes_per_row = (w + 7) // 8
        byte_data = []
        for row in bitmap:
            for byte_idx in range(bytes_per_row):
                byte_val = 0
                for bit in range(8):
                    px = byte_idx * 8 + bit
                    if px < w and row[px]:
                        byte_val |= (1 << (7 - bit))
                byte_data.append(byte_val)

        lines = []
        lines.append(f"// {name}  {w}x{h}")
        lines.append(f"const uint8_t PROGMEM {name}_bmp[] = {{")
        for i in range(0, len(byte_data), 12):
            chunk = byte_data[i : i + 12]
            hex_strs = [f"0x{b:02X}" for b in chunk]
            lines.append("    " + ", ".join(hex_strs) + ",")
        lines.append("};")
        lines.append(f"#define {name.upper()}_BMP_W  {w}")
        lines.append(f"#define {name.upper()}_BMP_H  {h}")
        return "\n".join(lines)

    @staticmethod
    def parse_bitmaps_h(path: str) -> dict[str, list[list[bool]]]:
        """Parse existing bitmaps.h and return dict of name → 2D bool array."""
        text = Path(path).read_text(encoding="utf-8").replace('\r\n', '\n').replace('\r', '\n')
        results = {}
        # Find all PROGMEM arrays - more flexible pattern
        # Match: optional comment, array declaration, then both #defines
        pattern = re.compile(
            r"const\s+uint8_t\s+PROGMEM\s+(\w+)\[\]\s*=\s*\{(.*?)\};\s*"  # array declaration
            r"#define\s+\w+_BMP_W\s+(\d+)\s*\n\s*#define\s+\w+_BMP_H\s+(\d+)",
            re.DOTALL,
        )
        for match in pattern.finditer(text):
            name = match.group(1)
            # Clean up hex data - remove all whitespace
            hex_data = ' '.join(match.group(2).split())
            w = int(match.group(3))
            h = int(match.group(4))
            # Parse hex bytes
            byte_list = [int(x.strip(), 16) for x in hex_data.split(",") if x.strip()]
            # Skip if we got wrong number of bytes
            expected_bytes = ((w + 7) // 8) * h
            if len(byte_list) != expected_bytes:
                print(f"Warning: {name} expected {expected_bytes} bytes, got {len(byte_list)}")
                continue
            # Unpack to 2D bool array
            bitmap = []
            bytes_per_row = (w + 7) // 8
            for row_idx in range(h):
                row = []
                for byte_idx in range(bytes_per_row):
                    byte_val = byte_list[row_idx * bytes_per_row + byte_idx]
                    for bit in range(8):
                        px = byte_idx * 8 + bit
                        if px < w:
                            row.append(bool(byte_val & (1 << (7 - bit))))
                bitmap.append(row)
            results[name] = bitmap
        return results


# ──────────────────────────────────────────────────────────────────────────────
# Rain Animation — Data structures and engine (mirrors firmware rain system)
# ──────────────────────────────────────────────────────────────────────────────

@dataclass
class RainDrop:
    """Mirrors firmware RainDrop struct — a single animated rain drop."""
    x: int = 0
    y: int = 0
    target_y: int = 41      # The y-coordinate where the drop hits the ground and splashes
    speed: int = 3          # Vertical fall speed in pixels per frame (3-6)
    sprite_variant: int = 0  # Which sprite to render (0-3)
    active: bool = True


@dataclass
class Splash:
    """Mirrors firmware Splash struct — brief impact effect at horizon."""
    x: int = 0
    y: int = 41
    frame_counter: int = SPLASH_FRAMES
    active: bool = False
    sprite_variant: int = 0  # Which splash sprite to render (0-3)


class RainAnimation:
    """Sprite-based rain physics engine — mirrors firmware rain animation system."""

    @staticmethod
    def init_rain_animation(seed: int = 42, horizon_y: int = 41) -> tuple[list, list]:
        """Initialise MAX_RAIN_DROPS drops at random positions above skyline.

        Returns:
            (drops, splashes) — two lists of RainDrop and Splash objects.
        """
        rng = random.Random(seed)
        drops = []
        for i in range(MAX_RAIN_DROPS):
            # Stagger initial positions so they are spread across the screen
            x = rng.randint(RAIN_AREA_X_START, RAIN_AREA_X_END)
            # Start drops at different vertical positions so screen fills immediately
            y = rng.randint(-30, horizon_y - 1)
            target_y = rng.randint(horizon_y, CANVAS_H - 1)
            speed = rng.randint(3, 6)
            variant = rng.randint(0, 3)
            drops.append(RainDrop(x=x, y=y, target_y=target_y, speed=speed, sprite_variant=variant, active=True))

        splashes = [Splash() for _ in range(MAX_SPLASHES)]
        return drops, splashes

    @staticmethod
    def reset_drop(drop: RainDrop, rng: random.Random, horizon_y: int = 41, wind_drift: int = 0):
        """Reset a single drop to a new random position above the skyline.
        
        The spawn zone is extended to the right based on wind drift to compensate
        for drops being blown off-screen during their fall.
        """
        # Compute how far right we need to spawn to ensure full visible coverage
        # Max horizontal travel = wind_drift × average fall frames
        avg_fall_frames = horizon_y / AVG_FALL_SPEED
        x_spawn_extend = int(abs(wind_drift) * avg_fall_frames)
        spawn_x_max = RAIN_AREA_X_END + x_spawn_extend
        
        drop.x = rng.randint(RAIN_AREA_X_START, spawn_x_max)
        drop.y = rng.randint(-8, -1)   # just above the top edge
        drop.target_y = rng.randint(horizon_y, CANVAS_H - 1)
        drop.speed = rng.randint(3, 6)
        drop.sprite_variant = rng.randint(0, 3)
        drop.active = True

    @staticmethod
    def compute_target_drops(rain_intensity_mmh: float, wind_drift: int, horizon_y: int = 41) -> int:
        """Return wind-compensated active drop count to maintain visible density.
        
        When wind blows drops sideways, the spawn zone widens. We need more drops
        overall to maintain the same visible density in the 64px skyline area.
        
        Args:
            rain_intensity_mmh: Rain intensity in mm/h (0-20)
            wind_drift: Horizontal wind drift in pixels per frame (0 to -3)
            horizon_y: Y position of the horizon line
            
        Returns:
            Compensated number of active drops to render.
        """
        # Base zone width (visible 64px)
        base_zone_width = RAIN_AREA_X_END - RAIN_AREA_X_START + 1  # 64
        
        # Extended spawn zone due to wind
        avg_fall_frames = horizon_y / AVG_FALL_SPEED
        x_spawn_extend = int(abs(wind_drift) * avg_fall_frames)
        spawn_zone_width = base_zone_width + x_spawn_extend
        
        # Base target from intensity: 0 mm/h = 0 drops, 10 mm/h = MAX_RAIN_DROPS
        base_target = int(rain_intensity_mmh * (MAX_RAIN_DROPS / 10.0))
        
        # Scale up to compensate for wider spawn zone
        adjusted = int(base_target * spawn_zone_width / base_zone_width)
        return min(MAX_RAIN_DROPS, adjusted)

    @staticmethod
    def _activate_splash(splashes: list, x: int, y: int, rng: random.Random, num_splash_variants: int = 1):
        """Find an inactive splash slot and activate it at the given position."""
        for splash in splashes:
            if not splash.active:
                splash.x = x
                splash.y = y
                splash.frame_counter = SPLASH_FRAMES
                splash.active = True
                splash.sprite_variant = rng.randint(0, max(0, num_splash_variants - 1))
                return

    @staticmethod
    def update(drops: list, splashes: list, rng: random.Random, horizon_y: int = 41, num_splash_variants: int = 1, wind_speed: int = 0, rain_intensity_mmh: float = 5.0):
        """Advance all drops and splashes by one animation frame.

        Movement: y += speed (vertical fall), x += wind (horizontal drift).
        Wind 0-10 km/h: straight down (no drift).
        Wind 10-80 km/h: progressively more drift.
        Wind >80 km/h: max drift.
        """
        # Calculate horizontal drift based on wind: 0 if wind <= 10, else scaled
        wind_drift = 0
        if wind_speed > 10:
            # Scale: 10 km/h = 0 drift, 80 km/h = max drift (-1 to -3 pixels per frame)
            # Linear scaling: (wind - 10) / 70 gives 0.0 at 10km/h, 1.0 at 80km/h
            drift_scaled = (wind_speed - 10) / 70.0
            wind_drift = -int(1 + drift_scaled * 2)
            wind_drift = max(-3, min(-1, wind_drift))
        
        # Determine how many drops should be active based on intensity
        # Use wind-compensated calculation to maintain visible density
        target_active_drops = RainAnimation.compute_target_drops(rain_intensity_mmh, wind_drift, horizon_y)
        
        # First, count active drops
        active_count = sum(1 for d in drops if d.active)
        
        # Activate or deactivate drops to match target
        if active_count < target_active_drops:
            for drop in drops:
                if not drop.active:
                    RainAnimation.reset_drop(drop, rng, horizon_y, wind_drift)
                    active_count += 1
                    if active_count >= target_active_drops:
                        break
        elif active_count > target_active_drops:
            for drop in drops:
                if drop.active:
                    drop.active = False
                    active_count -= 1
                    if active_count <= target_active_drops:
                        break
                        
        # Now update active drops
        for drop in drops:
            if not drop.active:
                continue
                
            # Move drop: vertical fall + horizontal wind drift
            drop.x += wind_drift
            drop.y += drop.speed

            # Collision with ground (target_y)
            if drop.y >= drop.target_y:
                RainAnimation._activate_splash(splashes, drop.x, drop.target_y, rng, num_splash_variants)
                RainAnimation.reset_drop(drop, rng, horizon_y, wind_drift)
                continue

            # Off-screen left edge or below canvas
            # Note: we DON'T kill drops that are off-screen right - those are drifting into view
            if drop.x < RAIN_AREA_X_START or drop.y >= CANVAS_H:
                RainAnimation.reset_drop(drop, rng, horizon_y, wind_drift)

        # Age splashes
        for splash in splashes:
            if splash.active:
                splash.frame_counter -= 1
                if splash.frame_counter <= 0:
                    splash.active = False

    @staticmethod
    def draw(canvas: OLEDCanvas, drops: list, splashes: list,
             rain_sprites: list, splash_sprites: list):
        """Render all active drops and splashes onto the canvas.

        Args:
            rain_sprites: list of 2D bool arrays (one per variant).  May be
                          empty or None — procedural pixels are used as fallback.
            splash_sprites: list of 2D bool arrays (splash variants).  May be
                            empty or None — procedural pixels are used as fallback.
        """
        for drop in drops:
            if not drop.active:
                continue
            if rain_sprites:
                variant_idx = drop.sprite_variant % len(rain_sprites)
                sprite = rain_sprites[variant_idx]
                canvas.blit(drop.x, drop.y, sprite)
            else:
                # Fallback: draw a 2×4 elongated pixel drop
                canvas.set_pixel(drop.x, drop.y, True)
                canvas.set_pixel(drop.x, drop.y + 1, True)
                canvas.set_pixel(drop.x, drop.y + 2, True)
                canvas.set_pixel(drop.x - 1, drop.y + 1, True)

        for splash in splashes:
            if not splash.active:
                continue
            if splash_sprites:
                variant_idx = splash.sprite_variant % len(splash_sprites)
                sprite = splash_sprites[variant_idx]
                canvas.blit(splash.x - len(sprite[0]) // 2, splash.y - len(sprite), sprite)
            else:
                # Fallback: small horizontal splash marks
                fx = splash.frame_counter  # 3 → wide, 1 → narrow
                canvas.set_pixel(splash.x - fx, splash.y, True)
                canvas.set_pixel(splash.x + fx, splash.y, True)
                if splash.frame_counter >= 2:
                    canvas.set_pixel(splash.x - fx + 1, splash.y - 1, True)
                    canvas.set_pixel(splash.x + fx - 1, splash.y - 1, True)


# ──────────────────────────────────────────────────────────────────────────────
# Procedural — Python mirrors of firmware C++ functions
# ──────────────────────────────────────────────────────────────────────────────

class Procedural:
    @staticmethod
    def draw_giant_badge(canvas: OLEDCanvas, badge_type: str):
        """Draw triple-ring badge with check/warn/X in left 64x64 half."""
        cx, cy = 32, 32
        for r in (28, 27, 26):
            canvas.draw_circle(cx, cy, r, fill=False, on=True)
        if badge_type == "check":
            canvas.draw_line(18, 32, 28, 44, on=True)
            canvas.draw_line(19, 32, 29, 44, on=True)
            canvas.draw_line(28, 44, 44, 18, on=True)
            canvas.draw_line(29, 44, 45, 18, on=True)
        elif badge_type == "warn":
            canvas.draw_rect(28, 16, 8, 24, fill=True, on=True)
            canvas.draw_rect(28, 44, 8, 8, fill=True, on=True)
        elif badge_type == "x":
            canvas.draw_line(18, 18, 46, 46, on=True)
            canvas.draw_line(19, 18, 47, 46, on=True)
            canvas.draw_line(46, 18, 18, 46, on=True)
            canvas.draw_line(47, 18, 19, 46, on=True)

    @staticmethod
    def apply_night_overlay(canvas: OLEDCanvas, seed: int = 42):
        """Draw stars in sky region and streetlight glow."""
        rng = random.Random(seed)
        for _ in range(rng.randint(3, 5)):
            x = rng.randint(SKYLINE_X, SKYLINE_X + 63)
            y = rng.randint(0, SKY_YMAX)
            canvas.set_pixel(x, y, True)
        # Streetlight glow (2x2)
        canvas.set_pixel(STREETLIGHT_BX, STREETLIGHT_BY, True)
        canvas.set_pixel(STREETLIGHT_BX + 1, STREETLIGHT_BY, True)
        canvas.set_pixel(STREETLIGHT_BX, STREETLIGHT_BY + 1, True)
        canvas.set_pixel(STREETLIGHT_BX + 1, STREETLIGHT_BY + 1, True)

    @staticmethod
    def draw_procedural_rain(canvas: OLEDCanvas, intensity: int):
        """Draw diagonal rain lines in skyline card."""
        count = {1: 3, 2: 6, 3: 10}.get(intensity, 3)
        for i in range(count):
            x = SKYLINE_X + int((i + 0.5) * 64 / count)
            y = int((i * 27) / count)
            canvas.draw_line(x, y, x - 2, y + 4, on=True)

    @staticmethod
    def draw_procedural_snow(canvas: OLEDCanvas, intensity: int, seed: int = 42):
        """Draw scattered snow pixels and roof accumulation."""
        rng = random.Random(seed)
        for _ in range(intensity * 4):
            x = rng.randint(SKYLINE_X, SKYLINE_X + 63)
            y = rng.randint(SKYLINE_Y, SKYLINE_Y + 28)  # 0 to 40 within 30px tall area
            canvas.set_pixel(x, y, True)
        if intensity >= 2:
            for x in range(SKYLINE_X, SKYLINE_X + 64):
                canvas.set_pixel(x, CHURCH_ROOF_Y, True)

    @staticmethod
    def draw_procedural_wind(canvas: OLEDCanvas, speed: int):
        """Draw horizontal swoosh dashes in sky area."""
        if speed < WIND_THRESHOLD:
            return
        for y_pos in (5, 9, 14):
            x = SKYLINE_X
            while x < SKYLINE_X + 64:
                for dx in range(min(4, SKYLINE_X + 64 - x)):
                    canvas.set_pixel(x + dx, y_pos, True)
                x += 6
                for dx in range(min(3, SKYLINE_X + 64 - x)):
                    canvas.set_pixel(x + dx, y_pos, True)
                x += 4
                for dx in range(min(2, SKYLINE_X + 64 - x)):
                    canvas.set_pixel(x + dx, y_pos, True)
                x += 3


# ──────────────────────────────────────────────────────────────────────────────
# SceneState & SceneComposer
# ──────────────────────────────────────────────────────────────────────────────

# View modes
VIEW_TODAY = "today"      # Current day with giant badge
VIEW_WEEKLY = "weekly"    # 7-day AM/PM matrix
VIEW_TIME = "time"        # Clock display


@dataclass
class SceneState:
    badge_type: str = "check"
    skyline_bmp: Optional[list[list[bool]]] = None
    extra_bmps: list = field(default_factory=list)
    # Sun/moon icons
    sun_bmp: Optional[list[list[bool]]] = None
    moon_bmp: Optional[list[list[bool]]] = None
    # Weather / bottom card
    cloud_bmp: Optional[list[list[bool]]] = None
    rain_cloud_bmp: Optional[list[list[bool]]] = None
    weather: str = "clear"
    intensity: int = 1
    night: bool = False
    wind_speed: int = 0
    temp_str: str = ""
    precip_mm: float = 0.0
    # Trend arrows
    arrow_ur_bmp: Optional[list[list[bool]]] = None
    arrow_dr_bmp: Optional[list[list[bool]]] = None
    arrow_r_bmp: Optional[list[list[bool]]] = None
    trend: str = "flat"         # "up", "down", or "flat"
    seed: int = 42
    view_mode: str = VIEW_TODAY  # today, weekly, or time
    # Weekly forecast data (7 values, each "check", "warn", "x", or "")
    week_am: list = field(default_factory=lambda: ["check", "check", "warn", "x", "check", "warn", ""])
    week_pm: list = field(default_factory=lambda: ["warn", "check", "x", "check", "check", "", ""])
    # Weekly cell bitmaps (optional custom art)
    cell_check_bmp: Optional[list[list[bool]]] = None
    cell_warn_bmp: Optional[list[list[bool]]] = None
    cell_x_bmp: Optional[list[list[bool]]] = None
    cell_empty_bmp: Optional[list[list[bool]]] = None
    # ── Rain animation state ────────────────────────────────────────────────
    use_sprite_rain: bool = True          # True = sprite-based, False = procedural lines
    rain_drops: list = field(default_factory=list)   # List[RainDrop]
    rain_splashes: list = field(default_factory=list)  # List[Splash]
    rain_sprites: list = field(default_factory=list)   # List of 2D bool arrays (one per variant)
    splash_sprites: list = field(default_factory=list)  # List of 2D bool arrays (splash variants)
    _rain_rng: Optional[random.Random] = field(default=None, repr=False, compare=False)
    show_horizon: bool = True             # Draw the virtual horizon line
    horizon_y: int = 41                   # Adjustable horizon line position (0-63)
    rain_intensity_mmh: float = 5.0       # Rain intensity in mm/h (controls number of drops)
    rain_fps: int = 15                    # Rain animation FPS


class SceneComposer:
    @staticmethod
    def compose(canvas: OLEDCanvas, state: SceneState):
        canvas.clear()

        if state.view_mode == VIEW_TODAY:
            SceneComposer._compose_today_view(canvas, state)
        elif state.view_mode == VIEW_WEEKLY:
            SceneComposer._compose_weekly_view(canvas, state)
        elif state.view_mode == VIEW_TIME:
            SceneComposer._compose_time_view(canvas, state)

    @staticmethod
    def _compose_today_view(canvas: OLEDCanvas, state: SceneState):
        """Render the main today/tomorrow view — matches firmware renderPrimaryView."""
        # ── Layer 1: Giant ride badge (left 64x64 half) ──────────────────
        Procedural.draw_giant_badge(canvas, state.badge_type)

        # ── Layer 2: Skyline bitmap at (SKYLINE_X, SKYLINE_Y) ────────────
        if state.skyline_bmp:
            canvas.blit(SKYLINE_X, SKYLINE_Y, state.skyline_bmp)
        for x, y, bmp in state.extra_bmps:
            canvas.blit(x, y, bmp)

        # ── Layer 2b: Virtual horizon line ───────────────────────────────
        if state.show_horizon:
            for x in range(RAIN_AREA_X_START, RAIN_AREA_X_END + 1):
                canvas.set_pixel(x, state.horizon_y, True)

        # ── Layer 3: Night / day sky elements ────────────────────────────
        if state.night:
            Procedural.apply_night_overlay(canvas, state.seed)
            if state.moon_bmp:
                canvas.blit(MOON_X, MOON_Y, state.moon_bmp)
        else:
            if state.sun_bmp:
                canvas.blit(SUN_X, SUN_Y, state.sun_bmp)

        # ── Layer 4: Weather effect overlay ─────────────────────────────
        if state.weather == "rain":
            if state.use_sprite_rain:
                # Ensure rain animation state is initialised
                if not state.rain_drops:
                    rng = state._rain_rng or random.Random(state.seed)
                    state._rain_rng = rng
                    drops, splashes = RainAnimation.init_rain_animation(state.seed, state.horizon_y)
                    state.rain_drops = drops
                    state.rain_splashes = splashes
                # Advance one frame
                rng = state._rain_rng or random.Random(state.seed)
                state._rain_rng = rng
                num_splash_variants = len(state.splash_sprites) if state.splash_sprites else 1
                RainAnimation.update(state.rain_drops, state.rain_splashes, rng, state.horizon_y, num_splash_variants, state.wind_speed, state.rain_intensity_mmh)
                RainAnimation.draw(canvas, state.rain_drops, state.rain_splashes,
                                   state.rain_sprites, state.splash_sprites)
            else:
                Procedural.draw_procedural_rain(canvas, state.intensity)
        elif state.weather == "snow":
            Procedural.draw_procedural_snow(canvas, state.intensity, state.seed)
        elif state.weather == "wind":
            Procedural.draw_procedural_wind(canvas, state.wind_speed)

        # ── UI text layer: temp + trend arrow (top-right area) ──────────
        if state.temp_str:
            SceneComposer._draw_temp_overlay(canvas, state.temp_str)
        if state.trend == "up" and state.arrow_ur_bmp:
            canvas.blit(ARROW_X, ARROW_Y, state.arrow_ur_bmp)
        elif state.trend == "down" and state.arrow_dr_bmp:
            canvas.blit(ARROW_X, ARROW_Y, state.arrow_dr_bmp)
        elif state.trend == "flat" and state.arrow_r_bmp:
            canvas.blit(ARROW_X, ARROW_Y, state.arrow_r_bmp)

        # ── Bottom card: right half lower 32 rows ───────────────────────
        SceneComposer._compose_bottom_card(canvas, state)

    @staticmethod
    def _draw_temp_overlay(canvas: OLEDCanvas, temp_str: str):
        """Draw temperature string at TEMP_X, TEMP_Y using a 3x5 pixel font.
        Preceded by a black (off) background box to erase skyline pixels below."""
        # Erase background box
        box_w = len(temp_str) * 4 + 2
        for dy in range(7):
            for dx in range(box_w):
                canvas.set_pixel(TEMP_X + dx, TEMP_Y + dy, False)
        # Draw characters using a minimal 3x5 font (subset: 0-9, °, C)
        font = SceneComposer._FONT_3X5
        cx = TEMP_X + 1
        for ch in temp_str:
            glyph = font.get(ch)
            if glyph:
                for row_idx, row_bits in enumerate(glyph):
                    for col_idx, bit in enumerate(row_bits):
                        if bit:
                            canvas.set_pixel(cx + col_idx, TEMP_Y + 1 + row_idx, True)
                cx += 4
            else:
                cx += 3  # space

    # Minimal 3x5 pixel font — each char is 5 rows of 3-pixel wide bitmasks
    _FONT_3X5: dict = {
        "0": [[1,1,1],[1,0,1],[1,0,1],[1,0,1],[1,1,1]],
        "1": [[0,1,0],[1,1,0],[0,1,0],[0,1,0],[1,1,1]],
        "2": [[1,1,1],[0,0,1],[0,1,0],[1,0,0],[1,1,1]],
        "3": [[1,1,1],[0,0,1],[0,1,1],[0,0,1],[1,1,1]],
        "4": [[1,0,1],[1,0,1],[1,1,1],[0,0,1],[0,0,1]],
        "5": [[1,1,1],[1,0,0],[1,1,1],[0,0,1],[1,1,1]],
        "6": [[1,1,1],[1,0,0],[1,1,1],[1,0,1],[1,1,1]],
        "7": [[1,1,1],[0,0,1],[0,1,0],[0,1,0],[0,1,0]],
        "8": [[1,1,1],[1,0,1],[1,1,1],[1,0,1],[1,1,1]],
        "9": [[1,1,1],[1,0,1],[1,1,1],[0,0,1],[1,1,1]],
        "-": [[0,0,0],[0,0,0],[1,1,1],[0,0,0],[0,0,0]],
        "°": [[0,1,0],[1,0,1],[0,1,0],[0,0,0],[0,0,0]],
        "C": [[0,1,1],[1,0,0],[1,0,0],[1,0,0],[0,1,1]],
        " ": [[0,0,0],[0,0,0],[0,0,0],[0,0,0],[0,0,0]],
    }

    @staticmethod
    def _compose_bottom_card(canvas: OLEDCanvas, state: SceneState):
        """Render bottom-right card (y=32-63): icon + wind + precip bars.
        Mirrors firmware renderBottomCard."""
        y0 = BOTTOM_CARD_Y  # 42 (moved down from 32)

        # Weather icon (cloud or rain_cloud)
        if state.weather in ("rain", "snow") and state.rain_cloud_bmp:
            canvas.blit(SKYLINE_X + 2, y0 + 4, state.rain_cloud_bmp)
        elif state.cloud_bmp:
            canvas.blit(SKYLINE_X + 2, y0 + 4, state.cloud_bmp)

        # Wind speed text (simple 3x5 font, right of icon)
        wind_txt = f"{state.wind_speed}kmh"
        if state.wind_speed > 0:
            cx = SKYLINE_X + 22
            for ch in wind_txt:
                glyph = SceneComposer._FONT_3X5.get(ch)
                if glyph:
                    for r, row_bits in enumerate(glyph):
                        for c, bit in enumerate(row_bits):
                            if bit:
                                canvas.set_pixel(cx + c, y0 + 4 + r, True)
                    cx += 4

        # Precipitation bar (proportional, max 5mm → full height)
        if state.precip_mm > 0:
            bar_h = min(int(state.precip_mm / 5.0 * 20), 20)
            bx = 124
            for dy in range(bar_h):
                canvas.set_pixel(bx, y0 + 30 - dy, True)
                canvas.set_pixel(bx + 1, y0 + 30 - dy, True)

    @staticmethod
    def _compose_weekly_view(canvas: OLEDCanvas, state: SceneState):
        """Render 7-day AM/PM matrix view — mirrors firmware renderWeeklyMatrix."""
        # ── Header row ───────────────────────────────────────────────────
        # Day labels M T W T F S S  (3-pixel font above each column)
        days_short = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"]
        col_w = 18   # column width per day
        col_x = [1 + i * col_w for i in range(7)]
        row_am_y = 10   # AM row top
        row_pm_y = 36   # PM row top
        cell_h = 24

        for i, (day_lbl, x) in enumerate(zip(days_short, col_x)):
            # Day label (2 chars of 3x5 font)
            cx = x + 1
            for ch in day_lbl:
                glyph = SceneComposer._FONT_3X5.get(ch.upper(), SceneComposer._FONT_3X5.get(ch))
                if glyph:
                    for r, row_bits in enumerate(glyph):
                        for c, bit in enumerate(row_bits):
                            if bit:
                                canvas.set_pixel(cx + c, 2 + r, True)
                cx += 4

            # AM cell
            SceneComposer._draw_weekly_cell(
                canvas, x, row_am_y, col_w - 2, cell_h,
                state.week_am[i] if i < len(state.week_am) else "",
                state,
            )
            # PM cell
            SceneComposer._draw_weekly_cell(
                canvas, x, row_pm_y, col_w - 2, cell_h,
                state.week_pm[i] if i < len(state.week_pm) else "",
                state,
            )

        # AM / PM row labels on far right
        for r, lbl in enumerate(["AM", "PM"]):
            cy = row_am_y + 8 if r == 0 else row_pm_y + 8
            cx = 122
            for ch in lbl:
                glyph = SceneComposer._FONT_3X5.get(ch)
                if glyph:
                    for row_idx, row_bits in enumerate(glyph):
                        for col_idx, bit in enumerate(row_bits):
                            if bit:
                                canvas.set_pixel(cx + col_idx, cy + row_idx, True)
                    cx += 4

    @staticmethod
    def _draw_weekly_cell(canvas: OLEDCanvas, x: int, y: int, w: int, h: int,
                           badge: str, state: SceneState):
        """Draw a single weekly matrix cell with border + badge symbol."""
        # Cell border
        canvas.draw_rect(x, y, w, h, fill=False, on=True)

        # Use custom bitmap if available
        bmp = None
        if badge == "check" and state.cell_check_bmp:
            bmp = state.cell_check_bmp
        elif badge == "warn" and state.cell_warn_bmp:
            bmp = state.cell_warn_bmp
        elif badge == "x" and state.cell_x_bmp:
            bmp = state.cell_x_bmp
        elif badge == "" and state.cell_empty_bmp:
            bmp = state.cell_empty_bmp

        if bmp:
            canvas.blit(x + 1, y + 1, bmp)
        else:
            # Draw procedural cell symbol
            cx = x + w // 2
            cy = y + h // 2
            if badge == "check":
                # Small checkmark in cell
                canvas.draw_line(cx - 3, cy, cx - 1, cy + 3, on=True)
                canvas.draw_line(cx - 1, cy + 3, cx + 4, cy - 4, on=True)
            elif badge == "warn":
                # Exclamation mark
                canvas.draw_line(cx, y + 3, cx, cy + 2, on=True)
                canvas.set_pixel(cx, cy + 5, True)
            elif badge == "x":
                # X mark
                canvas.draw_line(cx - 3, cy - 3, cx + 3, cy + 3, on=True)
                canvas.draw_line(cx + 3, cy - 3, cx - 3, cy + 3, on=True)
            # Empty cell: just the border (already drawn above)

    @staticmethod
    def _compose_time_view(canvas: OLEDCanvas, state: SceneState):
        """Render clock/time display view — large digital time."""
        import time as _time
        now = _time.localtime()
        hour = now.tm_hour
        minute = now.tm_min
        # Format as HH:MM
        digits = f"{hour:02d}{minute:02d}"

        # Draw each digit with a 7-segment style 10x16 px font
        # Digit starts: H1 at x=4, H2 at x=18, colon at x=32, M1 at x=38, M2 at x=52
        positions = [4, 18, 38, 52]
        colon_x = 32
        for seg_y in (18, 30):
            canvas.draw_rect(colon_x + 1, seg_y, 4, 4, fill=True, on=True)

        for idx, (d, dx) in enumerate(zip(digits, positions)):
            SceneComposer._draw_digit_large(canvas, dx, 8, int(d))

        # Night/day indicator on right side
        indicator_x = 68
        if state.night and state.moon_bmp:
            canvas.blit(indicator_x, 10, state.moon_bmp)
        elif not state.night and state.sun_bmp:
            canvas.blit(indicator_x, 10, state.sun_bmp)

        # Temp in bottom right corner
        if state.temp_str:
            cx = indicator_x
            cy = 44
            for ch in state.temp_str:
                glyph = SceneComposer._FONT_3X5.get(ch)
                if glyph:
                    for r, row_bits in enumerate(glyph):
                        for c, bit in enumerate(row_bits):
                            if bit:
                                canvas.set_pixel(cx + c, cy + r, True)
                    cx += 4

    @staticmethod
    def _draw_digit_large(canvas: OLEDCanvas, x: int, y: int, digit: int):
        """Draw a large 7-segment-style digit (10x16 px area)."""
        # Segments: top, top-left, top-right, middle, bottom-left, bottom-right, bottom
        segs = {
            0: "top,tl,tr,bl,br,bot",
            1: "tr,br",
            2: "top,tr,mid,bl,bot",
            3: "top,tr,mid,br,bot",
            4: "tl,tr,mid,br",
            5: "top,tl,mid,br,bot",
            6: "top,tl,mid,bl,br,bot",
            7: "top,tr,br",
            8: "top,tl,tr,mid,bl,br,bot",
            9: "top,tl,tr,mid,br,bot",
        }
        active = segs.get(digit, "").split(",")

        def seg(name):
            return name in active

        w, h = 10, 16
        m = 1  # margin
        # Horizontal segments
        if seg("top"):
            canvas.draw_line(x + m, y, x + w - m, y, on=True)
        if seg("mid"):
            canvas.draw_line(x + m, y + h // 2, x + w - m, y + h // 2, on=True)
        if seg("bot"):
            canvas.draw_line(x + m, y + h - 1, x + w - m, y + h - 1, on=True)
        # Vertical segments top half
        if seg("tl"):
            canvas.draw_line(x, y + m, x, y + h // 2 - m, on=True)
        if seg("tr"):
            canvas.draw_line(x + w, y + m, x + w, y + h // 2 - m, on=True)
        # Vertical segments bottom half
        if seg("bl"):
            canvas.draw_line(x, y + h // 2 + m, x, y + h - m, on=True)
        if seg("br"):
            canvas.draw_line(x + w, y + h // 2 + m, x + w, y + h - m, on=True)


# ──────────────────────────────────────────────────────────────────────────────
# Known asset slots (name, expected size in pixels, description)
# ──────────────────────────────────────────────────────────────────────────────

# Each asset slot: (name, size, description, default_x, default_y)
ASSET_SLOTS = [
    # ── Today view ────────────────────────────────────────────────────────
    ("skyline_base",  "64×30",  "City skyline silhouette (30px tall)", 64, 12),
    ("sun",           "10×10",  "Sun icon (daytime sky)", 116, 2),
    ("moon",          "8×8",    "Moon icon (night sky)", 116, 2),
    ("cloud",         "16×10",  "Cloud sprite", 66, 12),
    ("rain_cloud",    "16×12",  "Rain cloud sprite", 66, 12),
    ("arrow_ur",      "7×7",    "Trend arrow — up-right", 87, 1),
    ("arrow_dr",      "7×7",    "Trend arrow — down-right", 87, 1),
    ("arrow_r",       "7×7",    "Trend arrow — flat", 87, 1),
    # ── Rain animation sprites ─────────────────────────────────────────────
    ("rain_drop_1",   "3×6",    "Rain drop variant 1", 64, 0),
    ("rain_drop_2",   "3×6",    "Rain drop variant 2", 64, 0),
    ("rain_drop_3",   "2×5",    "Rain drop variant 3", 64, 0),
    ("rain_drop_4",   "2×4",    "Rain drop variant 4", 64, 0),
    ("splash_1",      "7×4",    "Rain splash variant 1", 64, 41),
    ("splash_2",      "7×4",    "Rain splash variant 2", 64, 41),
    ("splash_3",      "7×4",    "Rain splash variant 3", 64, 41),
    ("splash_4",      "7×4",    "Rain splash variant 4", 64, 41),
    # ── Weekly view ───────────────────────────────────────────────────────
    ("cell_check",    "14×20",  "Weekly cell — ride OK  (✓)", 0, 0),
    ("cell_warn",     "14×20",  "Weekly cell — caution  (!)", 0, 0),
    ("cell_x",        "14×20",  "Weekly cell — no ride  (X)", 0, 0),
    ("cell_empty",    "14×20",  "Weekly cell — no data", 0, 0),
]

# Rain sprite slot names for easy lookup
RAIN_SPRITE_SLOTS = ["rain_drop_1", "rain_drop_2", "rain_drop_3", "rain_drop_4"]
SPLASH_SPRITE_SLOTS = ["splash_1", "splash_2", "splash_3", "splash_4"]


# ──────────────────────────────────────────────────────────────────────────────
# BitmapEditorDialog — Simple click-to-toggle pixel editor
# ──────────────────────────────────────────────────────────────────────────────

class BitmapEditorDialog(tk.Toplevel):
    """Modal dialog for editing a bitmap with click-to-toggle pixels."""

    def __init__(self, parent, name: str, width: int, height: int,
                 existing_bitmap: Optional[list[list[bool]]] = None):
        super().__init__(parent)
        self.title(f"Edit: {name}")
        self.transient(parent)
        self.grab_set()

        self.name = name
        self.width = width
        self.height = height
        self.zoom = 10  # 10x zoom for each pixel
        self.result_bitmap: Optional[list[list[bool]]] = None
        self._max_history = 20
        self._history: list[list[list[bool]]] = []
        self._history_index = -1

        # Initialize or copy bitmap
        if existing_bitmap:
            self.bitmap = [row[:] for row in existing_bitmap]
        else:
            self.bitmap = [[False] * width for _ in range(height)]

        self._save_history()
        self._build_ui()
        self._render_grid()

    def _build_ui(self):
        frame = tk.Frame(self, padx=10, pady=10)
        frame.pack(fill=tk.BOTH, expand=True)

        # Canvas for zoomed pixel grid
        self.canvas = tk.Canvas(
            frame,
            width=self.width * self.zoom,
            height=self.height * self.zoom,
            bg="#222222",
            highlightthickness=1,
            highlightbackground="#444444",
        )
        self.canvas.pack()

        # Bind click to toggle pixel
        self.canvas.bind("<Button-1>", self._on_canvas_click)

        # Button row
        btn_frame = tk.Frame(frame)
        btn_frame.pack(fill=tk.X, pady=(10, 0))
        tk.Button(btn_frame, text="Undo (Ctrl+Z)", command=self._undo).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Redo (Ctrl+Y)", command=self._redo).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Clear All", command=self._on_clear).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Fill All", command=self._on_fill).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Cancel", command=self.destroy).pack(side=tk.RIGHT, padx=4)
        tk.Button(btn_frame, text="Save", command=self._on_save,
                  bg="#44aa44", fg="white").pack(side=tk.RIGHT, padx=4)

        # Keyboard shortcuts for undo/redo
        self.bind("<Control-z>", lambda e: self._undo())
        self.bind("<Control-Z>", lambda e: self._undo())
        self.bind("<Control-y>", lambda e: self._redo())
        self.bind("<Control-Y>", lambda e: self._redo())

    def _render_grid(self):
        """Draw the zoomed pixel grid."""
        self.canvas.delete("all")
        for y in range(self.height):
            for x in range(self.width):
                on = self.bitmap[y][x]
                color = OLED_ON if on else OLED_OFF
                self.canvas.create_rectangle(
                    x * self.zoom, y * self.zoom,
                    (x + 1) * self.zoom, (y + 1) * self.zoom,
                    fill=color, outline="#444444", width=1,
                )

    def _on_canvas_click(self, event):
        """Toggle pixel at click position."""
        x = event.x // self.zoom
        y = event.y // self.zoom
        if 0 <= x < self.width and 0 <= y < self.height:
            self.bitmap[y][x] = not self.bitmap[y][x]
            self._save_history()
            self._render_grid()

    def _on_clear(self):
        self.bitmap = [[False] * self.width for _ in range(self.height)]
        self._save_history()
        self._render_grid()

    def _on_fill(self):
        self.bitmap = [[True] * self.width for _ in range(self.height)]
        self._save_history()
        self._render_grid()

    def _save_history(self):
        """Save current bitmap state to history stack."""
        # Remove any states after current index (for redo)
        self._history = self._history[:self._history_index + 1]
        # Add current state
        self._history.append([row[:] for row in self.bitmap])
        # Limit history size
        if len(self._history) > self._max_history:
            self._history.pop(0)
        else:
            self._history_index += 1

    def _undo(self):
        """Undo last change."""
        if self._history_index > 0:
            self._history_index -= 1
            self.bitmap = [row[:] for row in self._history[self._history_index]]
            self._render_grid()

    def _redo(self):
        """Redo last undone change."""
        if self._history_index < len(self._history) - 1:
            self._history_index += 1
            self.bitmap = [row[:] for row in self._history[self._history_index]]
            self._render_grid()

    def _on_save(self):
        self.result_bitmap = self.bitmap
        self.destroy()


# ──────────────────────────────────────────────────────────────────────────────
# App — Main tkinter GUI
# ──────────────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("MotoWeather Bitmap Tool")
        self.geometry("1100x660")
        self.resizable(False, False)
        self.configure(bg=BG_COLOR)

        self.canvas = OLEDCanvas()
        self.state = SceneState()
        self.assets: dict[str, list[list[bool]]] = {}
        self.firmware_dir: Optional[Path] = None
        # Tracks currently selected asset slot name
        self._active_slot: Optional[str] = None
        # Holds refs to card widgets keyed by slot name
        self._cards: dict[str, dict] = {}
        # Stores X/Y position for each asset
        self._asset_positions: dict[str, dict] = {}

        # Keyboard shortcuts
        self.bind("<Control-o>", lambda e: self._on_open_firmware_dir())
        self.bind("<Control-O>", lambda e: self._on_open_firmware_dir())
        self.bind("<Control-s>", lambda e: self._on_save_h())
        self.bind("<Control-S>", lambda e: self._on_save_h())
        self.bind("<Control-e>", lambda e: self._on_edit_active_slot())
        self.bind("<Control-E>", lambda e: self._on_edit_active_slot())
        self.bind("<Control-c>", lambda e: self._on_copy())
        self.bind("<Control-C>", lambda e: self._on_copy())

        self._build_left_panel()
        self._build_centre_panel()
        self._build_right_panel()
        self._refresh_preview()
        # Start 15 FPS rain animation loop after GUI is ready
        self._start_rain_animation_loop()

    # ── Left panel: Asset browser ─────────────────────────────────────────

    def _build_left_panel(self):
        outer = LabelFrame(self, text="Asset Browser", padx=8, pady=8)
        outer.pack(side=tk.LEFT, fill=tk.Y, padx=8, pady=8)

        tk.Button(outer, text="Open firmware dir",
                  command=self._on_open_firmware_dir).pack(fill=tk.X, pady=(0, 6))

        # Label showing active bitmaps.h path
        self.firmware_path_lbl = tk.Label(outer, text="No firmware dir loaded",
                                           bg="#2b2b2b", fg="#666666",
                                           font=("TkDefaultFont", 8), anchor="w", wraplength=200)
        self.firmware_path_lbl.pack(fill=tk.X, pady=(0, 8))

        # Scrollable card list
        list_frame = tk.Frame(outer, bd=1, relief=tk.SUNKEN)
        list_frame.pack(fill=tk.BOTH, expand=True)

        # Procedural Graphics section
        proc_frame = LabelFrame(outer, text="Procedural Graphics", padx=8, pady=4)
        proc_frame.pack(fill=tk.X, pady=(8, 0))

        # Map procedural functions to their C++ file and function names
        self._procedural_functions = {
            "Giant Badge": ("firmware/src/display.cpp", "drawGiantBadge"),
            "Rain Effect": ("firmware/src/display.cpp", "drawProceduralRain"),
            "Snow Effect": ("firmware/src/display.cpp", "drawProceduralSnow"),
            "Wind Effect": ("firmware/src/display.cpp", "drawProceduralWind"),
        }

        for name, (filepath, funcname) in self._procedural_functions.items():
            btn = tk.Button(proc_frame, text=name, font=("TkDefaultFont", 8),
                           command=lambda n=name: self._on_edit_procedural(n))
            btn.pack(fill=tk.X, pady=1)

        self._scroll_canvas = tk.Canvas(list_frame, width=220, bg="#2b2b2b",
                                        highlightthickness=0)
        scrollbar = tk.Scrollbar(list_frame, orient=tk.VERTICAL,
                                 command=self._scroll_canvas.yview)
        self._scroll_canvas.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self._scroll_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self._cards_frame = tk.Frame(self._scroll_canvas, bg="#2b2b2b")
        self._cards_frame_id = self._scroll_canvas.create_window(
            (0, 0), window=self._cards_frame, anchor="nw"
        )
        self._cards_frame.bind("<Configure>", self._on_cards_frame_configure)

        # Build one card per known asset slot
        for slot in ASSET_SLOTS:
            slot_name, slot_size, slot_desc, slot_x, slot_y = slot
            self._build_asset_card(slot_name, slot_size, slot_desc, slot_x, slot_y)

        # Initialize asset positions dict
        self._asset_positions = {}

    def _on_cards_frame_configure(self, _event=None):
        self._scroll_canvas.configure(
            scrollregion=self._scroll_canvas.bbox("all")
        )

    def _on_edit_procedural(self, name: str):
        """Open VS Code to the C++ function for the selected procedural graphic."""
        if name not in self._procedural_functions:
            return
        filepath, funcname = self._procedural_functions[name]
        # Try to find the file relative to current directory
        import os
        # Check if we're in the MotoClock directory
        base_dir = Path("..") if Path("../firmware").exists() else Path(".")
        full_path = base_dir / filepath
        if not full_path.exists():
            # Try absolute path from current working directory
            full_path = Path.cwd() / filepath
        if not full_path.exists():
            messagebox.showerror("File not found", f"Could not find {filepath}")
            return
        # Open in VS Code at the function
        try:
            # Use code.cmd on Windows - open file without --goto (function name not valid for --goto)
            cmd = ["code.cmd", str(full_path)]
            subprocess.Popen(cmd)
        except FileNotFoundError:
            # Try without code.cmd
            try:
                subprocess.Popen(["code", str(full_path)])
            except FileNotFoundError:
                messagebox.showerror("VS Code not found", "Could not find VS Code (code or code.cmd in PATH)")

    def _get_slot_dims(self, name: str) -> tuple[int, int]:
        """Return (width, height) for a slot name from ASSET_SLOTS."""
        for slot in ASSET_SLOTS:
            slot_name = slot[0]
            slot_size = slot[1]
            if slot_name == name:
                # Parse "64x20" or "10x10" format
                parts = slot_size.lower().replace("×", "x").split("x")
                if len(parts) == 2:
                    return int(parts[0]), int(parts[1])
        return 8, 8  # fallback

    def _build_asset_card(self, name: str, size: str, desc: str, def_x: int = 0, def_y: int = 0):
        """Build a structured card widget for a known asset slot."""
        # Store default position
        if not hasattr(self, '_asset_positions'):
            self._asset_positions = {}
        self._asset_positions[name] = {"x": def_x, "y": def_y}

        card = tk.Frame(self._cards_frame, bg="#3c3c3c", bd=0,
                        padx=6, pady=4, cursor="hand2")
        card.pack(fill=tk.X, padx=4, pady=(4, 0))

        # Bold asset name
        name_lbl = tk.Label(card, text=name, bg="#3c3c3c", fg="#ffffff",
                             font=("TkDefaultFont", 9, "bold"), anchor="w")
        name_lbl.pack(fill=tk.X)

        # Size / description
        tk.Label(card, text=f"{size}  ·  {desc}", bg="#3c3c3c", fg="#aaaaaa",
                 font=("TkDefaultFont", 8), anchor="w").pack(fill=tk.X)

        # Position row
        pos_frame = tk.Frame(card, bg="#3c3c3c")
        pos_frame.pack(fill=tk.X, pady=(2, 0))
        tk.Label(pos_frame, text="X:", bg="#3c3c3c", fg="#888888",
                 font=("TkDefaultFont", 8)).pack(side=tk.LEFT)
        spin_x = tk.Spinbox(pos_frame, from_=0, to=127, width=4, font=("TkDefaultFont", 8))
        spin_x.delete(0, "end")
        spin_x.insert(0, str(def_x))
        spin_x.pack(side=tk.LEFT, padx=(0, 4))
        tk.Label(pos_frame, text="Y:", bg="#3c3c3c", fg="#888888",
                 font=("TkDefaultFont", 8)).pack(side=tk.LEFT)
        spin_y = tk.Spinbox(pos_frame, from_=0, to=63, width=4, font=("TkDefaultFont", 8))
        spin_y.delete(0, "end")
        spin_y.insert(0, str(def_y))
        spin_y.pack(side=tk.LEFT, padx=(0, 4))

        # Status line (updated when firmware dir is loaded)
        status_lbl = tk.Label(card, text="Not loaded", bg="#3c3c3c", fg="#666666",
                               font=("TkDefaultFont", 8, "italic"), anchor="w")
        status_lbl.pack(fill=tk.X)

        # Button row: "Select file…" | "Edit"
        btn_frame = tk.Frame(card, bg="#3c3c3c")
        btn_frame.pack(anchor="w", pady=(2, 0))

        btn = tk.Button(btn_frame, text="Select file…", font=("TkDefaultFont", 8),
                        bg="#555555", fg="#eeeeee", activebackground="#777777",
                        bd=0, padx=4, pady=1,
                        command=lambda n=name: self._on_select_file_for_slot(n))
        btn.pack(side=tk.LEFT, padx=(0, 4))

        edit_btn = tk.Button(btn_frame, text="Edit", font=("TkDefaultFont", 8),
                             bg="#555555", fg="#eeeeee", activebackground="#777777",
                             bd=0, padx=4, pady=1,
                             command=lambda n=name: self._on_edit_slot(n))
        edit_btn.pack(side=tk.LEFT)

        # Click anywhere on card to activate slot
        for widget in (card, name_lbl, status_lbl, pos_frame):
            widget.bind("<Button-1>", lambda _e, n=name: self._on_slot_click(n))

        self._cards[name] = {
            "frame": card, "status_lbl": status_lbl, "btn": btn,
            "spin_x": spin_x, "spin_y": spin_y,
                             "edit_btn": edit_btn}

    def _set_card_active(self, name: str):
        """Highlight the selected card, reset others."""
        for slot, refs in self._cards.items():
            active = (slot == name)
            bg = "#4a6fa5" if active else "#3c3c3c"
            refs["frame"].config(bg=bg)
            for child in refs["frame"].winfo_children():
                try:
                    child.configure(bg=bg)
                except Exception:
                    pass

    def _on_edit_slot(self, name: str):
        """Open the bitmap editor dialog for a slot."""
        w, h = self._get_slot_dims(name)
        existing = self.assets.get(name)
        dialog = BitmapEditorDialog(self, name, w, h, existing)
        self.wait_window(dialog)
        if dialog.result_bitmap:
            self.assets[name] = dialog.result_bitmap
            self._update_card_status(name, "Edited in tool", found=True)
            self._on_slot_click(name)

    def _on_edit_active_slot(self):
        """Open the bitmap editor for the currently active slot."""
        if self._active_slot:
            self._on_edit_slot(self._active_slot)
        elif self.assets:
            first_key = next(iter(self.assets))
            self._on_edit_slot(first_key)

    def _update_card_status(self, name: str, status: str, found: bool = False):
        """Update the status label text and colour for a slot card."""
        if name in self._cards:
            color = "#44cc88" if found else "#666666"
            self._cards[name]["status_lbl"].config(text=status, fg=color)

    # ── Centre panel: OLED preview + C array output ──────────────────────

    def _build_centre_panel(self):
        frame = tk.Frame(self)
        frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=8, pady=8)

        tk.Label(frame, text="OLED Preview (4x scale)").pack(anchor=tk.W)
        self.preview_canvas = tk.Canvas(
            frame,
            width=CANVAS_W * DEFAULT_SCALE,
            height=CANVAS_H * DEFAULT_SCALE,
            bg=OLED_OFF,
            highlightthickness=1,
            highlightbackground="#333",
        )
        self.preview_canvas.pack(pady=(0, 8))

        tk.Label(frame, text="C Array Output").pack(anchor=tk.W)
        self.c_array_text = scrolledtext.ScrolledText(frame, width=60, height=12, font=("Courier", 9))
        self.c_array_text.pack(fill=tk.BOTH, expand=True)

        btn_frame = tk.Frame(frame)
        btn_frame.pack(fill=tk.X, pady=(4, 0))
        tk.Button(btn_frame, text="Copy", command=self._on_copy).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Save to .h", command=self._on_save_h).pack(side=tk.LEFT, padx=4)

    # ── Right panel: Controls ────────────────────────────────────────────

    def _build_right_panel(self):
        # Create a canvas with scrollbar for the controls panel
        canvas = tk.Canvas(self, width=260, bg=BG_COLOR, highlightthickness=0)
        scrollbar = tk.Scrollbar(self, orient=tk.VERTICAL, command=canvas.yview, bg=BG_COLOR)
        self._controls_frame = tk.Frame(canvas, bg=BG_COLOR, padx=8, pady=8)
        
        canvas.pack(side=tk.RIGHT, fill=tk.Y, padx=8, pady=8)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.create_window((0, 0), window=self._controls_frame, anchor=tk.NW)
        self._controls_frame.bind("<Configure>", lambda _: canvas.configure(scrollregion=canvas.bbox("all")))
        
        # Use the _controls_frame for all controls
        frame = LabelFrame(self._controls_frame, text="Controls", padx=8, pady=8, 
                              bg=BG_COLOR, fg=FG_COLOR)
        frame.pack(fill=tk.X, pady=(0, 8))

        # Badge
        tk.Label(frame, text="Badge:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(0, 2))
        self.badge_var = tk.StringVar(value="check")
        for val, label in [("check", "✓"), ("warn", "!"), ("x", "X")]:
            tk.Radiobutton(frame, text=label, variable=self.badge_var, value=val,
                           command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                           selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)

        # Weather
        tk.Label(frame, text="Weather:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.weather_var = tk.StringVar(value="clear")
        for val in ("clear", "rain", "snow", "wind"):
            tk.Radiobutton(frame, text=val.title(), variable=self.weather_var, value=val,
                           command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                           selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)

        # Intensity
        tk.Label(frame, text="Intensity (1-3):", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.intensity_scale = tk.Scale(frame, from_=1, to=3, orient=tk.HORIZONTAL,
                                        bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.intensity_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.intensity_scale.pack(fill=tk.X)

        # Wind speed
        tk.Label(frame, text="Wind speed (km/h):", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.wind_scale = tk.Scale(frame, from_=0, to=60, orient=tk.HORIZONTAL,
                                   bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.wind_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.wind_scale.pack(fill=tk.X)

        # Night mode
        self.night_var = tk.BooleanVar(value=False)
        tk.Checkbutton(frame, text="Night mode", variable=self.night_var,
                       command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                       selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))

        # Rain animation controls
        tk.Label(frame, text="Rain animation:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.sprite_rain_var = tk.BooleanVar(value=True)
        tk.Checkbutton(frame, text="Sprite rain (vs lines)", variable=self.sprite_rain_var,
                       command=self._on_rain_mode_change, bg=BG_COLOR, fg=FG_COLOR,
                       selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)
        self.show_horizon_var = tk.BooleanVar(value=True)
        tk.Checkbutton(frame, text="Show horizon line", variable=self.show_horizon_var,
                       command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                       selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)
        tk.Label(frame, text="Horizon Y position:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(4, 0))
        self.horizon_y_scale = tk.Scale(frame, from_=0, to=63, orient=tk.HORIZONTAL,
                                        bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.horizon_y_scale.set(41)
        self.horizon_y_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.horizon_y_scale.pack(fill=tk.X)
        
        tk.Label(frame, text="Rain intensity (mm/h):", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(4, 0))
        self.rain_intensity_scale = tk.Scale(frame, from_=0, to=20, orient=tk.HORIZONTAL, resolution=0.5,
                                             bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.rain_intensity_scale.set(5.0)
        self.rain_intensity_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.rain_intensity_scale.pack(fill=tk.X)
        
        tk.Label(frame, text="Animation FPS:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(4, 0))
        self.rain_fps_scale = tk.Scale(frame, from_=1, to=60, orient=tk.HORIZONTAL,
                                       bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.rain_fps_scale.set(15)
        self.rain_fps_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.rain_fps_scale.pack(fill=tk.X)
        
        tk.Button(frame, text="Reset rain drops", font=("TkDefaultFont", 8),
                  command=self._on_reset_rain, bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(4, 0))

        # Color scheme
        tk.Label(frame, text="Color scheme:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.scheme_var = tk.StringVar(value=DEFAULT_SCHEME)
        scheme_menu = tk.OptionMenu(frame, self.scheme_var,
                                     *COLOR_SCHEMES.keys(),
                                     command=lambda _: self._on_scheme_change())
        scheme_menu.configure(width=12)
        scheme_menu.pack(anchor=tk.W)

        # View mode
        tk.Label(frame, text="View mode:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.view_var = tk.StringVar(value=VIEW_TODAY)
        for val, label in [(VIEW_TODAY, "Today"), (VIEW_WEEKLY, "Weekly"), (VIEW_TIME, "Clock")]:
            tk.Radiobutton(frame, text=label, variable=self.view_var, value=val,
                           command=self._on_view_change, bg=BG_COLOR, fg=FG_COLOR,
                           selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)

        # Temperature
        tk.Label(frame, text="Temp overlay:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.temp_entry = tk.Entry(frame, width=12, bg=BG_COLOR, fg=FG_COLOR, insertbackground=FG_COLOR)
        self.temp_entry.insert(0, "3°C")
        self.temp_entry.pack(fill=tk.X)
        self.temp_entry.bind("<Return>", lambda _: self._refresh_preview())

        # Trend arrow
        tk.Label(frame, text="Trend arrow:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.trend_var = tk.StringVar(value="flat")
        for val, label in [("up", "↑"), ("flat", "→"), ("down", "↓")]:
            tk.Radiobutton(frame, text=label, variable=self.trend_var, value=val,
                           command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                           selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)

        # Precipitation
        tk.Label(frame, text="Precip (mm):", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.precip_scale = tk.Scale(frame, from_=0, to=10, orient=tk.HORIZONTAL,
                                    bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.precip_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.precip_scale.set(0)
        self.precip_scale.pack(fill=tk.X)

        # Refresh
        tk.Button(frame, text="Refresh preview", command=self._refresh_preview,
                   bg=BG_COLOR, fg=FG_COLOR).pack(pady=(16, 0))

    # ── Event handlers ───────────────────────────────────────────────────

    # Mapping from slot name to the SceneState attribute it populates
    _SLOT_TO_STATE_ATTR = {
        "skyline_base":  "skyline_bmp",
        "sun":           "sun_bmp",
        "moon":          "moon_bmp",
        "cloud":         "cloud_bmp",
        "rain_cloud":    "rain_cloud_bmp",
        "arrow_ur":      "arrow_ur_bmp",
        "arrow_dr":      "arrow_dr_bmp",
        "arrow_r":       "arrow_r_bmp",
        "cell_check":    "cell_check_bmp",
        "cell_warn":     "cell_warn_bmp",
        "cell_x":        "cell_x_bmp",
        "cell_empty":    "cell_empty_bmp",
    }

    def _load_asset_into_state(self, name: str):
        """Push an asset bitmap from self.assets into the correct SceneState field."""
        if name not in self.assets:
            return
        bmp = self.assets[name]
        # Try exact match first, then try stripping _bmp suffix
        attr = self._SLOT_TO_STATE_ATTR.get(name)
        if not attr and name.endswith("_bmp"):
            attr = self._SLOT_TO_STATE_ATTR.get(name[:-4])  # strip "_bmp"
        if attr:
            setattr(self.state, attr, bmp)
            return

        # Handle rain drop sprites — collect all variants into the rain_sprites list
        if name in RAIN_SPRITE_SLOTS:
            # Rebuild full rain_sprites list from all loaded variants in order
            sprites = []
            for slot in RAIN_SPRITE_SLOTS:
                if slot in self.assets:
                    sprites.append(self.assets[slot])
            self.state.rain_sprites = sprites
            # Reset animation so new sprites take effect
            self.state.rain_drops = []
            self.state.rain_splashes = []
            return

        # Handle splash sprites — collect all variants into the splash_sprites list
        if name in SPLASH_SPRITE_SLOTS:
            # Rebuild full splash_sprites list from all loaded variants in order
            sprites = []
            for slot in SPLASH_SPRITE_SLOTS:
                if slot in self.assets:
                    sprites.append(self.assets[slot])
            self.state.splash_sprites = sprites
            # Reset animation so new sprites take effect
            self.state.rain_drops = []
            self.state.rain_splashes = []
            return

    def _on_slot_click(self, name: str):
        """Activate an asset slot card and load its bitmap into the preview."""
        self._active_slot = name
        self._set_card_active(name)

        # Update position from spinboxes if card exists
        if name in self._cards:
            card = self._cards[name]
            if "spin_x" in card and "spin_y" in card:
                try:
                    x = int(card["spin_x"].get())
                    y = int(card["spin_y"].get())
                    self._asset_positions[name] = {"x": x, "y": y}
                except ValueError:
                    pass  # Keep default if invalid

        if name in self.assets:
            self._load_asset_into_state(name)
            self._refresh_preview()
            c_code = Converter.to_c_array(self.assets[name], name)
            self.c_array_text.delete("1.0", tk.END)
            self.c_array_text.insert(tk.END, c_code)

    def _on_select_file_for_slot(self, name: str):
        """Open file dialog to import a PNG into a specific named slot."""
        path = filedialog.askopenfilename(
            title=f"Select PNG for '{name}'",
            filetypes=[("PNG files", "*.png"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            bitmap = Converter.load_png(path)
            self.assets[name] = bitmap
            self._update_card_status(name, f"Loaded from file", found=True)
            # Auto-activate this slot
            self._on_slot_click(name)
        except Exception as e:
            messagebox.showerror("Import Error", str(e))

    def _on_open_firmware_dir(self):
        dir_path = filedialog.askdirectory(title="Select firmware/include directory")
        if not dir_path:
            return
        self.firmware_dir = Path(dir_path)
        # Update the label showing the active path
        self.firmware_path_lbl.config(text=f"Dir: {self.firmware_dir.name}", fg="#44cc88")
        bitmaps_h = self.firmware_dir / "bitmaps.h"
        if bitmaps_h.exists():
            try:
                parsed = Converter.parse_bitmaps_h(str(bitmaps_h))
                count = 0
                for name, bitmap in parsed.items():
                    self.assets[name] = bitmap
                    # Map full name (sun_bmp) to short name (sun) for card lookup
                    short_name = name[:-4] if name.endswith("_bmp") else name
                    self._update_card_status(short_name, "Found in bitmaps.h", found=True)
                    # Also load into SceneState so preview updates immediately
                    self._load_asset_into_state(name)
                    count += 1
                if count == 0:
                    messagebox.showinfo("Info", "bitmaps.h found but contains no parseable arrays.")
                else:
                    self._refresh_preview()
            except Exception as e:
                messagebox.showerror("Parse Error", str(e))
        else:
            messagebox.showinfo("Info", "No bitmaps.h found in selected directory.")

    def _on_scheme_change(self):
        """Handle color scheme dropdown change."""
        global OLED_ON, OLED_OFF
        scheme = self.scheme_var.get()
        OLED_ON, OLED_OFF = COLOR_SCHEMES[scheme]
        self._refresh_preview()

    def _on_view_change(self):
        """Handle view mode radio button change."""
        self.state.view_mode = self.view_var.get()
        self._refresh_preview()

    def _update_state_from_positions(self):
        """Build extra_bmps list from asset positions stored in _asset_positions."""
        # Clear and rebuild extra_bmps
        self.state.extra_bmps = []
        
        # Assets that go into extra_bmps with custom positions
        position_assets = ["sun", "moon", "cloud", "rain_cloud", "arrow_ur", "arrow_dr", "arrow_r"]

        for name in position_assets:
            if name in self.assets and name in self._asset_positions:
                pos = self._asset_positions[name]
                x = pos.get("x", 0)
                y = pos.get("y", 0)
                self.state.extra_bmps.append((x, y, self.assets[name]))

    def _render_canvas(self):
        """Fast canvas rendering using to_photoimage - used by animation loop."""
        SceneComposer.compose(self.canvas, self.state)
        
        self._tk_photo = self.canvas.to_photoimage(scale=DEFAULT_SCALE)
        
        if not hasattr(self, '_preview_img_id'):
            self.preview_canvas.delete("all")
            self._preview_img_id = self.preview_canvas.create_image(0, 0, anchor=tk.NW, image=self._tk_photo)
        else:
            self.preview_canvas.itemconfig(self._preview_img_id, image=self._tk_photo)

    def _refresh_preview(self):
        # Update positions from spinboxes for all loaded assets
        for name, card in self._cards.items():
            if "spin_x" in card and "spin_y" in card:
                try:
                    x = int(card["spin_x"].get())
                    y = int(card["spin_y"].get())
                    self._asset_positions[name] = {"x": x, "y": y}
                except (ValueError, KeyError):
                    pass

        # Build extra_bmps from current positions
        self._update_state_from_positions()

        self.state.badge_type = self.badge_var.get()
        self.state.weather = self.weather_var.get()
        self.state.intensity = int(self.intensity_scale.get())
        self.state.wind_speed = int(self.wind_scale.get())
        self.state.night = self.night_var.get()
        self.state.temp_str = self.temp_entry.get()
        self.state.trend = self.trend_var.get()
        self.state.precip_mm = float(self.precip_scale.get())
        self.state.seed = 42
        self.state.use_sprite_rain = self.sprite_rain_var.get()
        self.state.show_horizon = self.show_horizon_var.get()
        self.state.horizon_y = int(self.horizon_y_scale.get())
        self.state.rain_intensity_mmh = float(self.rain_intensity_scale.get())
        self.state.rain_fps = int(self.rain_fps_scale.get())

        # Use fast canvas rendering
        self._render_canvas()

        # Update C array output using the active slot name
        if self.state.skyline_bmp and self._active_slot:
            c_code = Converter.to_c_array(
                self.state.skyline_bmp,
                self._active_slot,
            )
            self.c_array_text.delete("1.0", tk.END)
            self.c_array_text.insert(tk.END, c_code)

    def _on_rain_mode_change(self):
        """Reset rain drops when switching rain mode (sprite vs procedural)."""
        self.state.rain_drops = []
        self.state.rain_splashes = []
        self.state._rain_rng = None
        self._refresh_preview()

    def _on_reset_rain(self):
        """Clear rain animation state so it reinitialises on next render."""
        self.state.rain_drops = []
        self.state.rain_splashes = []
        self.state._rain_rng = None
        self._refresh_preview()

    def _start_rain_animation_loop(self):
        """Start the animation loop with fixed-timestep physics (15 Hz) and variable-rate rendering."""
        self._animation_running = True
        self._last_time = time.time()
        self._accumulator = 0.0
        self._tick_rain()

    def _tick_rain(self):
        """Advance rain physics at a fixed 15 Hz, render at the user-selected FPS."""
        if not getattr(self, '_animation_running', False):
            return

        current_time = time.time()
        delta_time = current_time - self._last_time
        self._last_time = current_time
        self._accumulator += delta_time

        FIXED_TIMESTEP = 1.0 / 15.0  # 66.666... ms per physics tick

        if self.state.weather == "rain" and self.state.use_sprite_rain:
            while self._accumulator >= FIXED_TIMESTEP:
                rng = self.state._rain_rng or random.Random(self.state.seed)
                self.state._rain_rng = rng
                num_splash_variants = len(self.state.splash_sprites) if self.state.splash_sprites else 1
                RainAnimation.update(
                    self.state.rain_drops,
                    self.state.rain_splashes,
                    rng,
                    self.state.horizon_y,
                    num_splash_variants,
                    self.state.wind_speed,
                    self.state.rain_intensity_mmh,
                )
                self._accumulator -= FIXED_TIMESTEP

        self._render_canvas()

        fps = max(1, self.state.rain_fps)
        interval_ms = int(1000 / fps)
        self.after(interval_ms, self._tick_rain)

    def _on_copy(self):
        self.clipboard_clear()
        self.clipboard_append(self.c_array_text.get("1.0", tk.END))

    def _on_save_h(self):
        # Use firmware_dir as initial directory if set
        initial_dir = str(self.firmware_dir) if hasattr(self, 'firmware_dir') and self.firmware_dir else None
        initial_file = "bitmaps.h" if initial_dir else None
        path = filedialog.asksaveasfilename(
            title="Save bitmap to .h file",
            defaultextension=".h",
            filetypes=[("C header", "*.h"), ("All files", "*.*")],
            initialdir=initial_dir,
            initialfile=initial_file,
        )
        if not path:
            return

        new_c_array = self.c_array_text.get("1.0", tk.END).strip()
        if not new_c_array:
            messagebox.showwarning("Nothing to save", "No bitmap data in the output area.")
            return

        # Extract the array name from the C code (e.g., "skyline_base_bmp")
        import re
        match = re.search(r"const\s+uint8_t\s+PROGMEM\s+(\w+)\[\]", new_c_array)
        if not match:
            messagebox.showerror("Invalid format", "Could not parse bitmap name from C array.")
            return
        array_name = match.group(1)

        # Read existing file content if it exists
        existing_content = ""
        if Path(path).exists():
            try:
                existing_content = Path(path).read_text(encoding="utf-8")
            except Exception as e:
                messagebox.showerror("Read error", f"Could not read existing file: {e}")
                return

        # Check if this bitmap already exists in the file
        pattern = rf"const\s+uint8_t\s+PROGMEM\s+{re.escape(array_name)}\[\]"
        existing_match = re.search(pattern, existing_content)

        if existing_match:
            # Bitmap exists - ask for confirmation
            if not messagebox.askyesno("Overwrite?",
                f"Bitmap '{array_name}' already exists in the file.\n\nOverwrite?"):
                return
            # Remove the old definition
            # Find the full old definition (array + #defines)
            old_def_pattern = (
                rf"const\s+uint8_t\s+PROGMEM\s+{re.escape(array_name)}\[\]\s*=\s*\{{[^}}]+\}};"
                rf"\s*#define\s+{re.escape(array_name.upper())}_BMP_W\s+\d+"
                rf"\s*#define\s+{re.escape(array_name.upper())}_BMP_H\s+\d+"
            )
            existing_content = re.sub(old_def_pattern, "", existing_content, flags=re.MULTILINE)
            # Clean up extra blank lines
            existing_content = re.sub(r"\n{3,}", "\n\n", existing_content)

        # Append or create the file
        if existing_content.strip():
            # Add blank line before new content if file not empty
            final_content = existing_content.rstrip() + "\n\n" + new_c_array
        else:
            # Create new file with header
            final_content = f"// Bitmap definitions\n// Generated by MotoWeather Bitmap Tool\n\n{new_c_array}\n"

        try:
            Path(path).write_text(final_content, encoding="utf-8")
            messagebox.showinfo("Saved", f"Bitmap '{array_name}' saved to {path}")
        except Exception as e:
            messagebox.showerror("Write error", f"Could not write file: {e}")


# ──────────────────────────────────────────────────────────────────────────────
# Entry point
# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    app = App()
    app.mainloop()
