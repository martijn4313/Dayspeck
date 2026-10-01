"""Layout, rain and colour constants shared by the simulator (mirror firmware/include/display.h)."""

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
ARROW_X, ARROW_Y = 91, 1
WIND_THRESHOLD = 25

# Rain animation constants
HORIZON_Y = 41          # Bottom of skyline buildings (rain collision boundary)
RAIN_AREA_X_START = 64  # Rain region left boundary
RAIN_AREA_X_END = 127   # Rain region right boundary
RAIN_AREA_Y_START = 0   # Rain region top boundary
RAIN_AREA_Y_END = 41    # Rain region bottom boundary (horizon)
MAX_RAIN_DROPS = 16     # Maximum simultaneous rain drops (MAX_RAIN_DROPS in firmware display.h)
MAX_SPLASHES = 8        # Maximum simultaneous splash effects (MAX_SPLASHES in firmware display.h)
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


CANVAS_W = 128
CANVAS_H = 64
DEFAULT_SCALE = 4


