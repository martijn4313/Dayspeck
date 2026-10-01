"""The asset slots the tool knows about."""

# ──────────────────────────────────────────────────────────────────────────────
# Known asset slots (name, expected size in pixels, description)
# ──────────────────────────────────────────────────────────────────────────────

# Each asset slot: (name, size, description, default_x, default_y)
ASSET_SLOTS = [
    # ── Today view ────────────────────────────────────────────────────────
    ("skyline_base",  "64×30",  "City skyline silhouette (30px tall)", 64, 12),
    ("sun",           "10×10",  "Sun icon (daytime sky)", 116, 2),
    ("moon",          "10×10",  "Moon icon (night sky)", 116, 2),
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
]

# Rain sprite slot names for easy lookup
RAIN_SPRITE_SLOTS = ["rain_drop_1", "rain_drop_2", "rain_drop_3", "rain_drop_4"]
SPLASH_SPRITE_SLOTS = ["splash_1", "splash_2", "splash_3", "splash_4"]


