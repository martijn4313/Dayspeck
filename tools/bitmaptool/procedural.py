"""Procedural drawing: Python mirrors of the firmware C++ functions in display.cpp."""

from .canvas import OLEDCanvas
from .constants import *  # noqa: F403


def _lcg(seed: int) -> int:
    """The firmware's 32-bit linear congruential generator (unsigned int arithmetic)."""
    return (seed * 1103515245 + 12345) & 0xFFFFFFFF


class Procedural:
    @staticmethod
    def draw_giant_badge(canvas: OLEDCanvas, badge_type: str):
        """drawGiantBadge: triple-ring border with check / warn / X in the left 64x64 half.
        Any other type (unknown rating) draws only the ring."""
        cx, cy = 32, 32
        for r in (28, 27, 26):
            canvas.draw_circle(cx, cy, r, fill=False, on=True)

        if badge_type == "check":
            for o in (-1, 0, 1):
                canvas.draw_line(cx - 14, cy + o, cx - 5, cy + 10 + o)
                canvas.draw_line(cx - 5, cy + 10 + o, cx + 14, cy - 12 + o)
        elif badge_type == "warn":
            for dx in (-1, 0, 1):
                for i in range(20):
                    canvas.set_pixel(cx + dx, cy - 14 + i)
            canvas.fill_disc(cx, cy + 10, 3)
        elif badge_type == "x":
            for i in range(-12, 13):
                canvas.set_pixel(cx + i, cy + i)
                canvas.set_pixel(cx + i + 1, cy + i)
                canvas.set_pixel(cx + i, cy + i + 1)
            for i in range(-12, 13):
                canvas.set_pixel(cx + i, cy - i)
                canvas.set_pixel(cx + i - 1, cy - i)
                canvas.set_pixel(cx + i, cy - i + 1)

    @staticmethod
    def apply_night_overlay(canvas: OLEDCanvas, seed: int = 42):
        """applyNightOverlay: four deterministic stars plus the streetlight glow.
        (`seed` is unused: the firmware always starts from 12345.)"""
        s = 12345
        for i in range(5):
            s = _lcg(s)
            star_x = 64 + (s % 64)
            s = _lcg(s)
            star_y = s % 12
            if i < 4:
                canvas.set_pixel(star_x, star_y)
        for dx in (0, 1):
            for dy in (0, 1):
                canvas.set_pixel(STREETLIGHT_BX + dx, STREETLIGHT_BY + dy)

    @staticmethod
    def draw_procedural_rain(canvas: OLEDCanvas, intensity: int):
        """drawProceduralRain: short diagonal strokes."""
        num_lines = 3 if intensity == 1 else 6 if intensity == 2 else 10
        for i in range(num_lines):
            start_x = 64 + (i * 127 // num_lines)
            start_y = i * 27 // num_lines
            canvas.draw_line(start_x, start_y, start_x - 2, start_y + 4)

    @staticmethod
    def draw_procedural_snow(canvas: OLEDCanvas, intensity: int, seed: int = 42):
        """drawProceduralSnow: scattered pixels plus a roof line at intensity >= 2."""
        s = 54321
        for _ in range(intensity * 4):
            s = _lcg(s)
            x = 64 + (s % 64)
            s = _lcg(s)
            y = s % 32
            canvas.set_pixel(x, y)
        if intensity >= 2:
            for x in range(64, 127):
                canvas.set_pixel(x, CHURCH_ROOF_Y)
