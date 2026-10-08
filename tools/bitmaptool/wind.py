"""Wind animation: curled gusts and, in autumn, tumbling leaves (mirrors the firmware's wind animation).

Integer maths only, so the C++ in firmware/src/display.cpp can follow it line by line. Gusts and leaves
travel from the left edge of the scene to the right, like the wind. They stop at the divider (x < 65 is
the ride badge) and rider leaves keep a thin black outline so they read over the line art of the village.
"""

from dataclasses import dataclass
import random

from .canvas import OLEDCanvas
from .constants import *  # noqa: F403

# Vertical wobble of a tumbling leaf over 16 frames (a rough sine, +-3 px)
LEAF_WOBBLE = (0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1)


@dataclass(frozen=True)
class WindArea:
    """Where the animation lives: the rider's right half, or the whole kids screen."""
    x_start: int = WIND_AREA_X_START
    x_end: int = WIND_AREA_X_END
    gust_y_top: int = WIND_AREA_Y_TOP
    gust_y_span: int = WIND_AREA_Y_SPAN
    leaf_x0: int = LEAF_SPAWN_X
    leaf_y0: int = LEAF_SPAWN_Y
    leaf_y_span: int = LEAF_SPAWN_SPAN
    leaf_max_y: int = LEAF_MAX_Y
    leaf_xor: bool = False      # kids: leaves invert what is behind them; rider: a thin black outline around each leaf


RIDER_AREA = WindArea()
KIDS_AREA = WindArea(KIDS_WIND_AREA_X_START, KIDS_WIND_AREA_X_END, WIND_AREA_Y_TOP, WIND_AREA_Y_SPAN,
                     KIDS_LEAF_SPAWN_X, KIDS_LEAF_SPAWN_Y, KIDS_LEAF_SPAWN_SPAN, KIDS_LEAF_MAX_Y, True)


@dataclass
class Gust:
    x: int = 0          # left end of the streak; the curl sits at x + length
    y: int = 0
    length: int = 10
    speed: int = 3      # px per frame
    curl: int = 1       # radius of the curl at the leading end (1 or 2)
    active: bool = False
    delay: int = 0      # frames to wait before the next gust in this slot


@dataclass
class Leaf:
    x: int = 0
    y_base: int = 0     # slowly sinks; the drawn y adds LEAF_WOBBLE
    age: int = 0        # frames since it appeared
    speed: int = 1      # px per frame
    phase: int = 0      # 0-15, offsets wobble and spin so leaves do not move in step
    active: bool = False
    delay: int = 0


def target_gusts(wind_kmh: int) -> int:
    return 3 if wind_kmh >= 65 else 2 if wind_kmh >= 50 else 1


def target_leaves(wind_kmh: int) -> int:
    return max(0, min(MAX_LEAVES, 2 + (wind_kmh - LEAF_MIN_WIND_KMH) // 15))


class WindAnimation:
    @staticmethod
    def init() -> tuple:
        """All slots idle, with staggered start delays so the first frames are not a burst."""
        gusts = [Gust(delay=i * 6) for i in range(MAX_GUSTS)]
        leaves = [Leaf(delay=i * 5) for i in range(MAX_LEAVES)]
        return gusts, leaves

    @staticmethod
    def update(gusts: list, leaves: list, rng: random.Random, wind_kmh: int, gusts_on: bool, leaves_on: bool,
               area: WindArea = RIDER_AREA):
        """Advance one frame. A slot beyond the target count finishes its run and is not reused."""
        want_gusts = target_gusts(wind_kmh) if gusts_on else 0
        for i, g in enumerate(gusts):
            if g.active:
                g.x += g.speed
                if g.x > area.x_end:
                    g.active = False
                    g.delay = rng.randrange(13)
            elif i < want_gusts:
                if g.delay > 0:
                    g.delay -= 1
                else:
                    g.length = 8 + rng.randrange(9)                    # 8-16
                    g.x = area.x_start - g.length                      # starts just off the left edge
                    g.y = area.gust_y_top + rng.randrange(area.gust_y_span)
                    g.curl = 1 + rng.randrange(2)
                    g.speed = 2 + wind_kmh // 25 + rng.randrange(2)
                    g.active = True

        want_leaves = target_leaves(wind_kmh) if leaves_on else 0
        for i, f in enumerate(leaves):
            if f.active:
                f.age += 1
                f.x += f.speed
                if f.age % 3 == 0 and f.y_base < area.leaf_max_y:
                    f.y_base += 1                                      # sinks slowly, down to leaf_max_y
                if f.x > area.x_end:                                   # and always blows off the right edge
                    f.active = False
                    f.delay = rng.randrange(21)
            elif i < want_leaves:
                if f.delay > 0:
                    f.delay -= 1
                else:
                    f.x = area.leaf_x0
                    f.y_base = area.leaf_y0 + rng.randrange(area.leaf_y_span)
                    f.age = 0
                    f.speed = 1 + wind_kmh // 30 + rng.randrange(2)
                    f.phase = rng.randrange(16)
                    f.active = True

    @staticmethod
    def draw(canvas: OLEDCanvas, gusts: list, leaves: list, leaf_sprites: list, area: WindArea = RIDER_AREA):
        for g in gusts:
            if not g.active:
                continue
            start = max(g.x, area.x_start)
            end = g.x + g.length
            if end >= start:
                canvas.draw_hline(start, g.y, end - start + 1)
            if end >= area.x_start:
                canvas.draw_circle_helper(end, g.y - g.curl, g.curl, 2 | 4)
        for f in leaves:
            if not f.active:
                continue
            y = f.y_base + LEAF_WOBBLE[(f.age + f.phase) & 15]
            sprite = leaf_sprites[((f.age >> 1) + f.phase) & 3] if leaf_sprites else None
            if area.leaf_xor:
                if sprite:
                    canvas.blit_xor(f.x, y, sprite)
                continue
            if sprite:
                # A thin black outline that follows the leaf's shape (no box), so the village's line art
                # stays whole around it; never over the divider
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        if dx or dy:
                            for r, sprite_row in enumerate(sprite):
                                for c, lit in enumerate(sprite_row):
                                    if lit and f.x + c + dx >= area.x_start:
                                        canvas.set_pixel(f.x + c + dx, y + r + dy, False)
                canvas.blit(f.x, y, sprite)
