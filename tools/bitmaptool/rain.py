"""Rain animation engine (mirrors the firmware rain system)."""

from dataclasses import dataclass
import random

from .canvas import OLEDCanvas
from .constants import *  # noqa: F403


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
            target_y = rng.randint(horizon_y - 3, horizon_y)
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
        drop.target_y = rng.randint(horizon_y - 3, horizon_y)
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


