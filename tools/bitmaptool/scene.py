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
VIEW_HOURLY = "hourly"    # next hours strip
VIEW_CLOCK = "clock"      # time and date

DAY_LETTERS = "SMTWTFS"   # Sunday first, like the firmware


@dataclass
class Hour:
    """One forecast hour (mirrors the firmware's HourSlice)."""
    temp: int = 15
    rain10: int = 0       # precipitation in 0.1 mm
    gust: int = 10        # km/h
    prob: int = 255       # chance of rain in %, 255 = unknown
    valid: bool = True


def demo_hours() -> list:
    """Sample forecast for previews: dry, then a rain shower, then dry again."""
    temps = [14, 15, 15, 14, 13, 12]
    rain = [0, 0, 6, 25, 8, 0]
    prob = [5, 20, 60, 90, 50, 10]
    gust = [18, 22, 31, 40, 28, 20]
    return [Hour(t, r, g, p) for t, r, g, p in zip(temps, rain, gust, prob)]


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
    week_best_day: int = -1     # column highlighted as the best day, -1 = none
    # Next-hours view
    hours: list = field(default_factory=demo_hours)
    first_hour: int = 17        # local hour of hours[0]
    leave_text: str = "Best 17:00"   # "Leave now", "Best HH:00" or ""
    updated_text: str = "upd 14:05"  # "" = unknown
    # Clock screen
    clock_valid: bool = True    # False: "Time not set yet"
    clock_hour: int = 7
    clock_minute: int = 45
    clock_colon: bool = True    # the colon blinks once a second on the device
    clock_weekday: int = 4      # 0 = Sunday
    clock_day: int = 1
    clock_month: int = 10
    clock_year: int = 2026
    # Status marks
    tomorrow: bool = False      # showing tomorrow's ride (the "TMR" tag)
    wifi_bars: int = 4          # 0-4, -1 = not connected
    stale: bool = False         # data older than twice its refresh interval ("OLD" tag)
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
        elif state.view_mode == VIEW_HOURLY:
            SceneComposer._compose_hourly_view(canvas, state)
        elif state.view_mode == VIEW_CLOCK:
            SceneComposer._compose_clock_view(canvas, state)
        else:
            SceneComposer._compose_today_view(canvas, state)
            SceneComposer._draw_status_marks(canvas, state)
        if state.stale and state.view_mode != VIEW_CLOCK:
            canvas.draw_text(0, 0, "OLD")

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

        # Temperature on a black box (fillRect(TEMP_X - 1, TEMP_Y - 1, 26, 10, BLACK)), then the trend arrow
        canvas.fill_rect(TEMP_X - 1, TEMP_Y - 1, 26, 10, on=False)
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
            # Three gusts of different lengths, each ending in a curl
            for dy, length, r in ((6, 14, 3), (12, 20, 2), (18, 12, 2)):
                y = iy + dy
                canvas.draw_hline(ix, y, length)
                canvas.draw_circle_helper(ix + length, y - r, r, 2 | 4)
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
            best = (i == state.week_best_day)
            if best:
                canvas.fill_rect(label_w + i * col_w, 0, col_w, 11)     # best day in inverse video
            canvas.draw_text(label_w + i * col_w + col_w // 2 - 3, 1, DAY_LETTERS[(state.week_start_dow + i) % 7], on=not best)
        canvas.draw_hline(label_w + 2, 10, col_w - 4, on=state.week_best_day != 0)    # underline today
        canvas.draw_hline(0, 12, 128)

        canvas.draw_text(1, 24, "AM")
        canvas.draw_text(1, 49, "PM")
        canvas.draw_hline(0, 38, 128)

        for i in range(7):
            cx = label_w + i * col_w + col_w // 2
            SceneComposer._draw_rating_glyph(canvas, cx, 26, state.week_am[i] if i < len(state.week_am) else "")
            SceneComposer._draw_rating_glyph(canvas, cx, 51, state.week_pm[i] if i < len(state.week_pm) else "")

    # ── Hourly view and status marks ────────────────────────────────────────

    @staticmethod
    def _draw_centered(canvas: OLEDCanvas, x: int, w: int, y: int, text: str):
        text_w = len(text) * 6 - 1
        canvas.draw_text(x + (w - text_w) // 2, y, text)

    @staticmethod
    def _compose_hourly_view(canvas: OLEDCanvas, state: SceneState):
        """renderHourlyView: row labels on the left, up to 6 columns of hour, temperature, rain bar and
        chance-of-rain line, gusts."""
        hours = state.hours
        if not hours:
            canvas.draw_text(16, 28, "No hourly data")
            return
        label_w, col_w, bar_bottom, bar_max = 20, 18, 44, 24

        # Row labels: h, degree C, mm (solid bar), % (dotted line), kmh
        canvas.draw_text(0, 0, "h")
        canvas.draw_rect(0, 10, 3, 3)                 # degree sign
        canvas.draw_text(5, 10, "C")
        canvas.draw_text(0, 21, "mm")
        canvas.fill_rect(14, 22, 4, 6)                # sample bar
        canvas.draw_text(0, 32, "%")
        for dx in range(8, 17, 2):
            canvas.set_pixel(dx, 35)                  # sample dotted line
        canvas.draw_text(0, 47, "kmh")

        for i, h in enumerate(hours[:6]):
            x = label_w + i * col_w
            SceneComposer._draw_centered(canvas, x, col_w, 0, f"{(state.first_hour + i) % 24:02d}")
            if not h.valid:
                SceneComposer._draw_centered(canvas, x, col_w, 22, "--")
                continue
            SceneComposer._draw_centered(canvas, x, col_w, 10, str(h.temp))
            if h.rain10 > 0:
                bar_h = min(2 + h.rain10 * 6 // 10, bar_max)
                canvas.fill_rect(x + 5, bar_bottom - bar_h + 1, 8, bar_h)
            if h.prob != 255 and h.prob > 0:
                y = bar_bottom - h.prob * bar_max // 100
                for dx in range(2, 17, 2):
                    canvas.set_pixel(x + dx, y)
            SceneComposer._draw_centered(canvas, x, col_w, 47, str(h.gust))
        canvas.draw_hline(0, bar_bottom + 1, 128)
        if state.leave_text:
            canvas.draw_text(0, 57, state.leave_text)
        if state.updated_text:
            canvas.draw_text(128 - len(state.updated_text) * 6 + 1, 57, state.updated_text)

    @staticmethod
    def _draw_status_marks(canvas: OLEDCanvas, state: SceneState):
        """renderStatusMarks: "TMR" tag and WiFi signal bars in the corners of the left half."""
        if state.tomorrow:
            canvas.draw_text(0, 57, "TMR")
        if state.wifi_bars < 0:
            canvas.draw_line(55, 57, 61, 63)
            canvas.draw_line(61, 57, 55, 63)
            return
        for i in range(4):
            x, h = 51 + i * 3, 2 + i * 2
            if i < state.wifi_bars:
                canvas.fill_rect(x, 64 - h, 2, h)
            else:
                canvas.set_pixel(x, 63)

    # ── Clock ───────────────────────────────────────────────────────────────

    DAYS = ("Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat")
    MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")

    @staticmethod
    def _compose_clock_view(canvas: OLEDCanvas, state: SceneState):
        """renderClockView: HH:MM in large type, weekday and date below, year at the bottom."""
        if not state.clock_valid:
            canvas.draw_text(22, 22, "Time not set yet")
            canvas.draw_text(10, 36, "waiting for WiFi...")
            return
        sep = ":" if state.clock_colon else " "
        time_text = f"{state.clock_hour % 24:02d}{sep}{state.clock_minute % 60:02d}"
        canvas.draw_text((128 - (len(time_text) * 18 - 3)) // 2, 6, time_text, size=3)

        date_text = (f"{SceneComposer.DAYS[state.clock_weekday % 7]} {state.clock_day} "
                     f"{SceneComposer.MONTHS[(state.clock_month + 11) % 12]}")
        canvas.draw_text((128 - (len(date_text) * 12 - 2)) // 2, 37, date_text, size=2)

        year_text = str(state.clock_year)
        canvas.draw_text((128 - (len(year_text) * 6 - 1)) // 2, 56, year_text)

