"""Tests for the simulator: it must draw what the firmware draws."""
import subprocess
import sys
from pathlib import Path


from bitmaptool.canvas import OLEDCanvas
from bitmaptool.constants import (
    LEAF_MAX_Y, PRECIP_TEXT_Y, SCENE_H, SCENE_W, SCENE_X, SCENE_Y, WIND_AREA_X_END, WIND_AREA_X_START,
    WIND_AREA_Y_SPAN, WIND_AREA_Y_TOP, WIND_TEXT_X, WIND_TEXT_Y,
)
from bitmaptool.scene import SceneComposer, SceneState, VIEW_HOURLY, VIEW_WEEKLY

ROOT = Path(__file__).resolve().parents[2]


def render(**kwargs) -> OLEDCanvas:
    state = SceneState(**kwargs)
    canvas = OLEDCanvas()
    SceneComposer.compose(canvas, state)
    return canvas


def lit(canvas, x0, y0, x1, y1) -> int:
    return sum(canvas.pixels[y][x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1))


def test_divider_is_drawn_and_the_scene_has_no_inner_divider():
    c = render()
    assert all(c.pixels[y][64] for y in range(10, 64))   # vertical (top rows are covered by the temperature box)
    assert not all(c.pixels[41][x] for x in range(65, 128))   # the old card divider is gone


def test_badge_types_differ_and_unknown_is_ring_only():
    ring_only = render(badge_type="")
    for kind in ("check", "warn", "x"):
        assert lit(render(badge_type=kind), 15, 15, 49, 49) > 0
    assert lit(ring_only, 15, 15, 49, 49) == 0   # nothing inside the ring (r=26) except the symbol


def test_temperature_text_uses_the_5x7_font():
    c = render(temp_str="12C")
    assert lit(c, 65, 1, 82, 8) > 20      # three glyphs of the firmware font
    assert lit(render(temp_str=""), 65, 1, 82, 8) == 0


def test_street_band_shows_wind_and_rain_text():
    c = render(wind_speed=12, precip_mm=1.5)
    assert lit(c, WIND_TEXT_X, WIND_TEXT_Y, 127, WIND_TEXT_Y + 6) > 20
    assert lit(c, WIND_TEXT_X, PRECIP_TEXT_Y, 127, PRECIP_TEXT_Y + 6) > 20


def test_scene_is_blitted_below_the_temperature_row():
    scene = [[True] * SCENE_W for _ in range(SCENE_H)]
    c = render(scene_day_bmp=scene)
    assert lit(c, 64, SCENE_Y, 127, SCENE_Y + SCENE_H - 1) > SCENE_W * (SCENE_H - 8)
    assert lit(c, 66, 0, 127, SCENE_Y - 1) < 80          # only temperature, arrow and sun/moon strip


def test_night_uses_the_night_scene_and_stars_stay_above_it():
    day = [[False] * SCENE_W for _ in range(SCENE_H)]
    night = [[False] * SCENE_W for _ in range(SCENE_H)]
    night[5][5] = True
    c_night = render(night=True, scene_day_bmp=day, scene_night_bmp=night)
    assert c_night.pixels[SCENE_Y + 5][SCENE_X + 5]
    assert not render(night=False, scene_day_bmp=day, scene_night_bmp=night).pixels[SCENE_Y + 5][SCENE_X + 5]
    # the four stars sit in the strip above the scene, so the only lit pixel inside it is the art's own
    assert lit(c_night, 65, SCENE_Y, 127, WIND_TEXT_Y - 1) == 1
    assert lit(c_night, 65, 0, 127, SCENE_Y - 1) >= 1


def run_frames(frames, **kwargs):
    state = SceneState(**kwargs)
    canvas = OLEDCanvas()
    seen = []
    for _ in range(frames):
        SceneComposer.compose(canvas, state)
        seen.append([row[:] for row in canvas.pixels])
    return state, seen


def test_wind_without_leaves_animates_gusts_only():
    state, frames = run_frames(60, weather="wind", wind_speed=52)
    assert any(g.active for g in state.wind_gusts) or any(lit_pixels(f) for f in frames)
    assert not any(leaf.active for leaf in state.wind_leaves)
    assert frames[0] != frames[-1]


def lit_pixels(pixels):
    return sum(sum(row[65:]) for row in pixels[10:46])


def test_clear_calm_day_has_no_wind_animation():
    state, frames = run_frames(40, weather="clear", wind_speed=10, autumn=True)
    assert state.wind_gusts == [] and state.wind_leaves == []
    assert frames[0] == frames[-1]


def leaves_ever_active(**kwargs) -> bool:
    state = SceneState(**kwargs)
    canvas = OLEDCanvas()
    for _ in range(120):
        SceneComposer.compose(canvas, state)
        if any(leaf.active for leaf in state.wind_leaves):
            return True
    return False


def test_autumn_leaves_blow_from_20_kmh_without_a_windy_condition():
    sprites = [[[True] * 5 for _ in range(5)]] * 4
    assert leaves_ever_active(weather="clear", wind_speed=30, autumn=True, leaf_bmps=sprites)
    assert leaves_ever_active(weather="wind", wind_speed=52, autumn=True, leaf_bmps=sprites)
    # not in autumn, too little wind, or rain and snow: no leaves
    assert not leaves_ever_active(weather="clear", wind_speed=30, autumn=False, leaf_bmps=sprites)
    assert not leaves_ever_active(weather="clear", wind_speed=15, autumn=True, leaf_bmps=sprites)
    assert not leaves_ever_active(weather="snow", wind_speed=30, autumn=True, leaf_bmps=sprites)
    assert not leaves_ever_active(weather="rain", wind_speed=30, autumn=True, leaf_bmps=sprites)


def test_wind_animation_never_touches_the_left_half():
    leaf = [[True] * 5 for _ in range(5)]
    _, frames = run_frames(200, weather="wind", wind_speed=70, autumn=True, leaf_bmps=[leaf] * 4)
    plain = render(badge_type="check", weather="clear", wind_speed=70)
    for f in frames:
        for y in range(64):
            assert f[y][:64] == plain.pixels[y][:64]


def test_gusts_and_leaves_stay_in_their_area_and_move_right():
    from bitmaptool.wind import WindAnimation, target_gusts, target_leaves
    import random
    gusts, leaves = WindAnimation.init()
    rng = random.Random(1)
    for _ in range(300):
        WindAnimation.update(gusts, leaves, rng, 65, True, True)
        for g in gusts:
            if g.active:
                assert WIND_AREA_Y_TOP <= g.y < WIND_AREA_Y_TOP + WIND_AREA_Y_SPAN
                assert 8 <= g.length <= 16 and g.speed >= 2
        for f in leaves:
            if f.active:
                assert WIND_AREA_X_START <= f.x <= WIND_AREA_X_END + 4
                assert f.y_base <= LEAF_MAX_Y + 1
    assert (target_gusts(40), target_gusts(50), target_gusts(65)) == (1, 2, 3)
    assert (target_leaves(20), target_leaves(35), target_leaves(50), target_leaves(120)) == (2, 3, 4, 4)


def test_wind_animation_is_deterministic_for_a_seed():
    a, fa = run_frames(50, weather="wind", wind_speed=52, autumn=True, seed=3)
    b, fb = run_frames(50, weather="wind", wind_speed=52, autumn=True, seed=3)
    assert fa == fb


def test_night_overlay_is_deterministic_like_the_firmware():
    assert render(night=True).pixels == render(night=True).pixels


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


def test_best_day_is_shown_in_inverse_video():
    plain = render(view_mode=VIEW_WEEKLY)
    best = render(view_mode=VIEW_WEEKLY, week_best_day=2)
    assert lit(best, 48, 0, 63, 10) > 100          # column 2 header is a solid block with the letter cut out
    assert lit(plain, 48, 0, 63, 10) < 40


def test_hourly_view_draws_columns_bars_and_footer():
    c = render(view_mode=VIEW_HOURLY)
    assert lit(c, 20, 0, 37, 7) > 5                # first hour label (columns start after the 20 px label area)
    # the shower in column 3 (index 3, 2.5 mm) is a tall bar, the dry first column has none
    assert lit(c, 74 + 5, 20, 74 + 12, 44) > 100
    assert lit(c, 20 + 5, 25, 20 + 12, 40) == 0
    assert lit(c, 0, 57, 64, 63) > 20 and lit(c, 70, 57, 127, 63) > 20   # leave advice and update time
    assert all(c.pixels[45][x] for x in range(128))                       # baseline


def test_hourly_view_labels_name_every_row():
    c = render(view_mode=VIEW_HOURLY)
    assert lit(c, 0, 0, 5, 7) > 3                  # h
    assert lit(c, 0, 10, 11, 17) > 8               # degree sign and C
    assert lit(c, 0, 21, 11, 28) > 8               # mm
    assert lit(c, 14, 22, 17, 27) == 24            # sample bar
    assert lit(c, 0, 32, 5, 39) > 3                # %
    assert [c.pixels[35][x] for x in range(8, 18)] == [True, False] * 5   # sample dotted line
    assert lit(c, 0, 47, 17, 54) > 8               # kmh


def test_hourly_view_without_data_and_with_missing_hours():
    assert lit(render(view_mode=VIEW_HOURLY, hours=[]), 16, 28, 112, 36) > 20
    from bitmaptool.scene import Hour
    c = render(view_mode=VIEW_HOURLY, hours=[Hour(valid=False)])
    assert lit(c, 1, 22, 20, 29) > 5               # "--"


def test_status_marks():
    assert lit(render(tomorrow=True), 0, 57, 17, 63) > 10
    assert lit(render(tomorrow=False), 0, 57, 17, 63) == 0
    full, none = render(wifi_bars=4), render(wifi_bars=0)
    assert lit(full, 51, 55, 62, 63) > lit(none, 51, 55, 62, 63)
    assert lit(render(wifi_bars=-1), 55, 57, 61, 63) > 8       # cross when not connected
    assert lit(render(stale=True), 0, 0, 17, 7) > 10           # OLD tag


def test_clock_view_shows_time_date_and_year():
    from bitmaptool.scene import VIEW_CLOCK
    c = render(view_mode=VIEW_CLOCK)
    assert lit(c, 20, 6, 107, 26) > 150          # 07:45 in 3x type
    assert lit(c, 5, 37, 122, 52) > 100          # "Thu 1 Oct" in 2x type
    assert lit(c, 50, 56, 78, 63) > 20           # year
    assert lit(c, 0, 0, 127, 5) == 0             # nothing above the clock


def test_clock_colon_blinks_and_missing_time_is_explained():
    from bitmaptool.scene import VIEW_CLOCK
    on, off = render(view_mode=VIEW_CLOCK), render(view_mode=VIEW_CLOCK, clock_colon=False)
    assert lit(on, 56, 6, 72, 26) > lit(off, 56, 6, 72, 26)     # the colon sits between the digit pairs
    unset = render(view_mode=VIEW_CLOCK, clock_valid=False)
    assert lit(unset, 22, 22, 118, 29) > 20 and lit(unset, 20, 6, 107, 20) == 0


def test_clock_has_no_stale_tag():
    from bitmaptool.scene import VIEW_CLOCK
    assert lit(render(view_mode=VIEW_CLOCK, stale=True), 0, 0, 17, 5) == 0


def test_rain_sprites_are_in_the_header_and_used():
    from bitmaptool.convert import Converter
    entries = {e.name: e for e in Converter.parse_entries((ROOT / "firmware/include/bitmaps.h").read_text())}
    drops = [entries[f"rain_drop_{i}_bmp"].pixels for i in range(1, 5)]
    splashes = [entries[f"splash_{i}_bmp"].pixels for i in range(1, 5)]
    assert len({str(d) for d in drops}) == 4 and len({str(s) for s in splashes}) == 4   # four different variants
    from bitmaptool.rain import RainAnimation, RainDrop, Splash
    c = OLEDCanvas()
    RainAnimation.draw(c, [RainDrop(x=80, y=5, target_y=41, active=True, sprite_variant=0)],
                       [Splash(x=100, y=41, frame_counter=3, active=True, sprite_variant=1)], drops, splashes)
    assert lit(c, 80, 5, 82, 10) == sum(map(sum, drops[0]))                 # drop sprite 1 drawn at its position
    assert lit(c, 97, 37, 103, 40) == sum(map(sum, splashes[1]))            # splash sprite 2 centred on x=100


def test_rain_lands_on_the_pavement_above_the_text():
    """Splashes land at the pavement edge of the scene, never down in the street band with the text."""
    from bitmaptool.rain import RainAnimation
    from bitmaptool.constants import HORIZON_Y
    import random
    drops, splashes = RainAnimation.init_rain_animation(7, HORIZON_Y)
    assert all(HORIZON_Y - 3 <= d.target_y <= HORIZON_Y for d in drops)
    rng = random.Random(3)
    for _ in range(200):
        RainAnimation.update(drops, splashes, rng, HORIZON_Y, 4, 10, 8.0)
    assert all(HORIZON_Y - 3 <= s.y <= HORIZON_Y for s in splashes if s.active)
    assert all(HORIZON_Y - 3 <= d.target_y <= HORIZON_Y for d in drops)
    assert HORIZON_Y < WIND_TEXT_Y


def test_kids_leaves_blow_across_the_whole_screen():
    from bitmaptool.wind import KIDS_AREA, WindAnimation
    import random
    gusts, leaves = WindAnimation.init()
    rng = random.Random(5)
    canvas = OLEDCanvas()
    sprites = [[[True] * 5 for _ in range(5)]] * 4
    left_half_seen = False
    for _ in range(300):
        WindAnimation.update(gusts, leaves, rng, 40, False, True, KIDS_AREA)
        assert not any(g.active for g in gusts)                  # the kids screens have no gusts
        canvas.clear()
        WindAnimation.draw(canvas, gusts, leaves, sprites, KIDS_AREA)
        left_half_seen = left_half_seen or lit(canvas, 0, 0, 63, 63) > 0
        for f in leaves:
            if f.active:
                assert -4 <= f.x <= 127 + 4 and 3 <= f.y_base <= KIDS_AREA.leaf_max_y + 1
    assert left_half_seen


def test_kids_leaves_invert_what_they_cross_and_keep_clear_of_the_digits():
    from bitmaptool.wind import KIDS_AREA, WindAnimation
    import random
    leaf = [[True] * 5 for _ in range(5)]
    gusts, leaves = WindAnimation.init()
    rng = random.Random(2)
    canvas = OLEDCanvas()
    base = [[(x + y) % 2 == 0 for x in range(128)] for y in range(64)]
    for _ in range(200):
        WindAnimation.update(gusts, leaves, rng, 40, False, True, KIDS_AREA)
        canvas.pixels = [row[:] for row in base]
        WindAnimation.draw(canvas, gusts, leaves, [leaf] * 4, KIDS_AREA)
        for y in range(46, 64):                                   # the digits' rows are never touched
            assert canvas.pixels[y] == base[y]
    # a leaf over a lit area shows as dark pixels: the picture is not erased around it
    canvas.pixels = [[True] * 128 for _ in range(64)]
    canvas.blit_xor(10, 10, leaf)
    assert not canvas.pixels[12][12] and canvas.pixels[12][16] and canvas.pixels[9][12]
