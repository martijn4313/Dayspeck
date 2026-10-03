"""Layout, rain and colour constants shared by the simulator (mirror firmware/include/display.h)."""

# ──────────────────────────────────────────────────────────────────────────────
# Coordinate Constants (the village scene and the text on its street)
# ──────────────────────────────────────────────────────────────────────────────

SCENE_X = 64            # the 64x50 village scene fills the right half below the temperature row
SCENE_Y = 10
SCENE_W = 64
SCENE_H = 50
SUN_X, SUN_Y = 116, 0   # the 10x10 sun / moon sit in the strip above the scene
MOON_X, MOON_Y = 116, 0
TEMP_X, TEMP_Y = 65, 1
ARROW_X, ARROW_Y = 91, 1
WIND_TEXT_X = 82        # wind and precipitation text on the empty street band
WIND_TEXT_Y = 49
PRECIP_TEXT_Y = 57

# Rain animation constants
HORIZON_Y = 46          # The pavement edge of the scene (rain splashes here)
RAIN_AREA_X_START = 64  # Rain region left boundary
RAIN_AREA_X_END = 127   # Rain region right boundary
RAIN_AREA_Y_START = 0   # Rain region top boundary
RAIN_AREA_Y_END = 46    # Rain region bottom boundary (horizon)
MAX_RAIN_DROPS = 16     # Maximum simultaneous rain drops (MAX_RAIN_DROPS in firmware display.h)
MAX_SPLASHES = 8        # Maximum simultaneous splash effects (MAX_SPLASHES in firmware display.h)
SPLASH_FRAMES = 3       # How many animation frames a splash is visible
RAIN_FRAME_INTERVAL_MS = 66  # 15 FPS in milliseconds
AVG_FALL_SPEED = 4.5    # Average fall speed (px/frame), midpoint of randint(3, 6)
AVG_FALL_FRAMES = HORIZON_Y / AVG_FALL_SPEED  # ~9 frames from top to horizon

# Wind animation: gusts (curled streaks) and, in autumn, tumbling leaves (mirror firmware/include/display.h)
MAX_GUSTS = 3
MAX_LEAVES = 4
WIND_AREA_X_START = 65  # right of the divider at x=64
WIND_AREA_X_END = 127
WIND_AREA_Y_TOP = 11    # gusts and leaves stay between the temperature row and the street band
WIND_AREA_Y_SPAN = 33   # gust lanes: y = 11 .. 43
LEAF_SPAWN_X = WIND_AREA_X_START   # leaves enter at the left edge of the area
LEAF_SPAWN_Y = 12       # a new leaf starts at y = 12 .. 35
LEAF_SPAWN_SPAN = 24
LEAF_MIN_WIND_KMH = 20  # in autumn leaves blow from this wind speed on, even when it is not a "windy" day
LEAF_MAX_Y = 38         # a leaf whose base line passes this has landed

# The kids variant has no gusts; its leaves blow across the whole screen (KIDS_MODE in display.h)
KIDS_WIND_AREA_X_START = 0
KIDS_WIND_AREA_X_END = 127
KIDS_LEAF_SPAWN_X = -4  # enters a little off the left edge
KIDS_LEAF_SPAWN_Y = 3   # a new leaf starts at y = 3 .. 15 and stays above the temperature digits (y >= 46)
KIDS_LEAF_SPAWN_SPAN = 13
KIDS_LEAF_MAX_Y = 34    # base line limit: with the wobble and the sprite a leaf ends above y = 42

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


CANVAS_W = 128
CANVAS_H = 64
DEFAULT_SCALE = 4


