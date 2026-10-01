"""Tests for the simulator: it must draw what the firmware draws."""
import subprocess
import sys
from pathlib import Path


from bitmaptool.canvas import OLEDCanvas
from bitmaptool.scene import SceneComposer, SceneState, VIEW_WEEKLY

ROOT = Path(__file__).resolve().parents[2]


def render(**kwargs) -> OLEDCanvas:
    state = SceneState(**kwargs)
    canvas = OLEDCanvas()
    SceneComposer.compose(canvas, state)
    return canvas


def lit(canvas, x0, y0, x1, y1) -> int:
    return sum(canvas.pixels[y][x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1))


def test_dividers_are_drawn():
    c = render()
    assert all(c.pixels[y][64] for y in range(10, 64))   # vertical (top rows are covered by the temperature box)
    assert all(c.pixels[41][x] for x in range(65, 128))  # horizontal


def test_badge_types_differ_and_unknown_is_ring_only():
    ring_only = render(badge_type="")
    for kind in ("check", "warn", "x"):
        assert lit(render(badge_type=kind), 15, 15, 49, 49) > 0
    assert lit(ring_only, 15, 15, 49, 49) == 0   # nothing inside the ring (r=26) except the symbol


def test_temperature_text_uses_the_5x7_font():
    c = render(temp_str="12C")
    assert lit(c, 65, 1, 82, 8) > 20      # three glyphs of the firmware font
    assert lit(render(temp_str=""), 65, 1, 82, 8) == 0


def test_bottom_card_shows_wind_and_rain_text():
    c = render(wind_speed=12, precip_mm=1.5)
    assert lit(c, 93, 46, 127, 53) > 20 and lit(c, 93, 55, 127, 62) > 20


def test_weather_icons_differ():
    icons = {w: render(weather=w, wind_speed=30) for w in ("clear", "rain", "snow", "wind")}
    areas = [lit(c, 66, 44, 90, 63) for c in icons.values()]
    assert len(set(areas)) == 4


def test_wind_effect_needs_25_kmh():
    assert lit(render(weather="wind", wind_speed=24), 84, 5, 127, 5) == 0
    assert lit(render(weather="wind", wind_speed=40), 84, 5, 127, 5) > 10


def test_wind_dashes_repeat_across_the_card_with_gaps():
    # x 64-83 is covered by the temperature box (as on the device), so look at the second repeat
    row = [render(weather="wind", wind_speed=40).pixels[5][x] for x in range(88, 100)]
    assert row == [True] * 4 + [False] * 2 + [True] * 3 + [False] + [True] * 2


def test_night_overlay_is_deterministic_like_the_firmware():
    assert render(night=True).pixels == render(night=True).pixels
    # streetlight glow
    assert render(night=True).pixels[32][120]


def test_weekly_view_marks_today_and_rows():
    c = render(view_mode=VIEW_WEEKLY, week_start_dow=4)
    assert all(c.pixels[10][x] for x in range(18, 30))     # today underlined
    assert all(c.pixels[12][x] for x in range(128))        # header rule
    assert lit(c, 0, 24, 11, 31) > 5                       # "AM" label
    am_check = lit(c, 16, 18, 31, 33)
    assert am_check > 10                                   # Thursday AM is a check mark


def test_cli_render_writes_png(tmp_path):
    out = tmp_path / "scene.png"
    subprocess.run(
        [sys.executable, "-m", "bitmaptool", "render", "--bitmaps", str(ROOT / "firmware/include/bitmaps.h"),
         "--weather", "rain", "--out", str(out)],
        cwd=ROOT / "tools", check=True, capture_output=True,
    )
    assert out.stat().st_size > 0
