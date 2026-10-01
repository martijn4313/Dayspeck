"""SceneState and SceneComposer: the full 128x64 screen as the firmware draws it.

The drawing order and geometry mirror firmware/src/display.cpp (renderPrimaryView,
renderSkylineCard, renderBottomCard, renderWeeklyMatrix) so the preview matches the device.
"""

from dataclasses import dataclass, field
from typing import Optional
import random

from .canvas import OLEDCanvas
from .constants import *  # noqa: F403
from .procedural import Procedural
from .rain import RainAnimation

# View modes
VIEW_TODAY = "today"      # Current day with giant badge
VIEW_WEEKLY = "weekly"    # 7-day AM/PM matrix

DAY_LETTERS = "SMTWTFS"   # Sunday first, like the firmware


@dataclass
class SceneState:
    badge_type: str = "check"   # "check", "warn", "x" or "" (unknown: ring only)
    skyline_bmp: Optional[list[list[bool]]] = None
    extra_bmps: list = field(default_factory=list)
    # Sun/moon icons
    sun_bmp: Optional[list[list[bool]]] = None
    moon_bmp: Optional[list[list[bool]]] = None
    # Weather / bottom card
    weather: str = "clear"      # "clear", "rain", "snow" or "wind"
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
    view_mode: str = VIEW_TODAY
    # Weekly forecast data (7 values, each "check", "warn", "x" or "" for unknown)
    week_am: list = field(default_factory=lambda: ["check", "check", "warn", "x", "check", "warn", ""])
    week_pm: list = field(default_factory=lambda: ["warn", "check", "x", "check", "check", "", ""])
    week_start_dow: int = 4     # weekday of the first column, 0 = Sunday (4 = Thursday)
    # ── Rain animation state ────────────────────────────────────────────────
    use_sprite_rain: bool = True          # True = sprite-based, False = procedural lines
    rain_drops: list = field(default_factory=list)   # List[RainDrop]
    rain_splashes: list = field(default_factory=list)  # List[Splash]
    rain_sprites: list = field(default_factory=list)   # List of 2D bool arrays (one per variant)
    splash_sprites: list = field(default_factory=list)  # List of 2D bool arrays (splash variants)
    _rain_rng: Optional[random.Random] = field(default=None, repr=False, compare=False)
    show_horizon: bool = False            # Simulator aid: draw the virtual horizon line (not in firmware)
    horizon_y: int = 41                   # Adjustable horizon line position (0-63)
    rain_intensity_mmh: float = 5.0       # Rain intensity in mm/h (controls number of drops)
    rain_fps: int = 15                    # Rain animation FPS


class SceneComposer:
    @staticmethod
    def compose(canvas: OLEDCanvas, state: SceneState):
        canvas.clear()
        if state.view_mode == VIEW_WEEKLY:
            SceneComposer._compose_weekly_view(canvas, state)
        else:
            SceneComposer._compose_today_view(canvas, state)

    # ── Today view ──────────────────────────────────────────────────────────

    @staticmethod
    def _compose_today_view(canvas: OLEDCanvas, state: SceneState):
        """renderPrimaryView: badge, divider, skyline card, divider, bottom card."""
        Procedural.draw_giant_badge(canvas, state.badge_type)
        for y in range(64):
            canvas.set_pixel(64, y)              # vertical divider (drawLine(64, 0, 64, 63))

        SceneComposer._compose_skyline_card(canvas, state)

        canvas.draw_hline(64, 41, 64)            # horizontal divider (drawLine(64, 41, 127, 41))
        SceneComposer._compose_bottom_card(canvas, state)

    @staticmethod
    def _compose_skyline_card(canvas: OLEDCanvas, state: SceneState):
        """renderSkylineCard."""
        if state.skyline_bmp:
            canvas.blit(SKYLINE_X, SKYLINE_Y, state.skyline_bmp)
        for x, y, bmp in state.extra_bmps:
            canvas.blit(x, y, bmp)

        if state.show_horizon:
            for x in range(RAIN_AREA_X_START, RAIN_AREA_X_END + 1):
                canvas.set_pixel(x, state.horizon_y, True)

        if state.night:
            Procedural.apply_night_overlay(canvas, state.seed)
            if state.moon_bmp:
                canvas.blit(MOON_X, MOON_Y, state.moon_bmp)
        elif state.sun_bmp:
            canvas.blit(SUN_X, SUN_Y, state.sun_bmp)

        if state.weather == "rain":
            if state.use_sprite_rain:
                # Ensure rain animation state is initialised
                if not state.rain_drops:
                    rng = state._rain_rng or random.Random(state.seed)
                    state._rain_rng = rng
                    drops, splashes = RainAnimation.init_rain_animation(state.seed, state.horizon_y)
                    state.rain_drops = drops
                    state.rain_splashes = splashes
                rng = state._rain_rng or random.Random(state.seed)
                state._rain_rng = rng
                num_splash_variants = len(state.splash_sprites) if state.splash_sprites else 1
                RainAnimation.update(state.rain_drops, state.rain_splashes, rng, state.horizon_y,
                                     num_splash_variants, state.wind_speed, state.rain_intensity_mmh)
                RainAnimation.draw(canvas, state.rain_drops, state.rain_splashes,
                                   state.rain_sprites, state.splash_sprites)
            else:
                Procedural.draw_procedural_rain(canvas, state.intensity)
        elif state.weather == "snow":
            Procedural.draw_procedural_snow(canvas, state.intensity, state.seed)
        elif state.weather == "wind":
            Procedural.draw_procedural_wind(canvas, state.wind_speed)

        # Temperature on a black box (fillRect(TEMP_X - 1, TEMP_Y - 1, 20, 10, BLACK)), then the trend arrow
        canvas.fill_rect(TEMP_X - 1, TEMP_Y - 1, 20, 10, on=False)
        if state.temp_str:
            canvas.draw_text(TEMP_X, TEMP_Y, state.temp_str)
        arrow = {"up": state.arrow_ur_bmp, "down": state.arrow_dr_bmp}.get(state.trend, state.arrow_r_bmp)
        if arrow:
            canvas.blit(ARROW_X, ARROW_Y, arrow)

    @staticmethod
    def _compose_bottom_card(canvas: OLEDCanvas, state: SceneState):
        """renderBottomCard: procedural weather icon, wind speed and precipitation text."""
        ix, iy = 66, 44
        if state.weather in ("rain", "snow"):
            canvas.fill_disc(ix + 7, iy + 8, 5)
            canvas.fill_disc(ix + 14, iy + 5, 6)
            canvas.fill_disc(ix + 20, iy + 9, 4)
            canvas.fill_rect(ix + 7, iy + 8, 14, 5)
            for i in range(3):
                x = ix + 6 + i * 7
                if state.weather == "rain":
                    canvas.draw_line(x + 1, iy + 15, x - 1, iy + 19)
                else:
                    canvas.set_pixel(x, iy + 16)
                    canvas.set_pixel(x - 1, iy + 18)
        elif state.weather == "wind":
            for i in range(3):
                y = iy + 4 + i * 6
                length = 22 if i == 1 else 16
                canvas.draw_hline(ix, y, length)
                canvas.draw_circle_helper(ix + length, y - 2, 2, 2)
        else:
            cx, cy = ix + 11, iy + 9
            canvas.fill_disc(cx, cy, 4)
            dx = (1, 1, 0, -1, -1, -1, 0, 1)
            dy = (0, 1, 1, 1, 0, -1, -1, -1)
            for a in range(8):
                canvas.draw_line(cx + dx[a] * 6, cy + dy[a] * 6, cx + dx[a] * 8, cy + dy[a] * 8)

        canvas.draw_text(93, 46, f"{state.wind_speed}km/h")
        canvas.draw_text(93, 55, f"{state.precip_mm:.1f}mm")

    # ── Weekly view ─────────────────────────────────────────────────────────

    @staticmethod
    def _draw_rating_glyph(canvas: OLEDCanvas, cx: int, cy: int, rating: str):
        """drawRatingGlyph."""
        if rating == "check":
            for o in (0, 1):
                canvas.draw_line(cx - 6, cy + o, cx - 2, cy + 4 + o)
                canvas.draw_line(cx - 2, cy + 4 + o, cx + 6, cy - 5 + o)
        elif rating == "warn":
            canvas.fill_rect(cx - 1, cy - 7, 3, 9)
            canvas.fill_rect(cx - 1, cy + 4, 3, 3)
        elif rating == "x":
            for o in (0, 1):
                canvas.draw_line(cx - 6 + o, cy - 6, cx + 6 + o, cy + 6)
                canvas.draw_line(cx - 6 + o, cy + 6, cx + 6 + o, cy - 6)
        else:
            canvas.draw_hline(cx - 4, cy, 9)    # unknown

    @staticmethod
    def _compose_weekly_view(canvas: OLEDCanvas, state: SceneState):
        """renderWeeklyMatrix: 7 columns of 16 px starting with today, AM and PM rows."""
        label_w, col_w = 16, 16
        for i in range(7):
            canvas.draw_text(label_w + i * col_w + col_w // 2 - 3, 1, DAY_LETTERS[(state.week_start_dow + i) % 7])
        canvas.draw_hline(label_w + 2, 10, col_w - 4)    # underline today
        canvas.draw_hline(0, 12, 128)

        canvas.draw_text(1, 24, "AM")
        canvas.draw_text(1, 49, "PM")
        canvas.draw_hline(0, 38, 128)

        for i in range(7):
            cx = label_w + i * col_w + col_w // 2
            SceneComposer._draw_rating_glyph(canvas, cx, 26, state.week_am[i] if i < len(state.week_am) else "")
            SceneComposer._draw_rating_glyph(canvas, cx, 51, state.week_pm[i] if i < len(state.week_pm) else "")
