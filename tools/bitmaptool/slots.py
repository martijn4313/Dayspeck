"""The asset slots the tool knows about."""

# ──────────────────────────────────────────────────────────────────────────────
# Known asset slots (name, expected size in pixels, description)
# ──────────────────────────────────────────────────────────────────────────────

# Each asset slot: (name, size, description, default_x, default_y)
ASSET_SLOTS = [
    # ── Today view ────────────────────────────────────────────────────────
    ("scene_day",     "64×50",  "Village scene by day (street band at the bottom holds the text)", 64, 10),
    ("scene_night",   "64×50",  "Village scene at night (street lamp lit)", 64, 10),
    ("sun",           "10×10",  "Sun icon (daytime sky)", 116, 0),
    ("moon",          "10×10",  "Moon icon (night sky)", 116, 0),
    ("arrow_ur",      "7×7",    "Trend arrow — up-right", 91, 1),
    ("arrow_dr",      "7×7",    "Trend arrow — down-right", 91, 1),
    ("arrow_r",       "7×7",    "Trend arrow — flat", 91, 1),
    # ── Rain animation sprites ─────────────────────────────────────────────
    ("rain_drop_1",   "3×6",    "Rain drop variant 1", 64, 0),
    ("rain_drop_2",   "3×6",    "Rain drop variant 2", 64, 0),
    ("rain_drop_3",   "2×5",    "Rain drop variant 3", 64, 0),
    ("rain_drop_4",   "2×4",    "Rain drop variant 4", 64, 0),
    ("splash_1",      "7×4",    "Rain splash variant 1", 64, 46),
    ("splash_2",      "7×4",    "Rain splash variant 2", 64, 46),
    ("splash_3",      "7×4",    "Rain splash variant 3", 64, 46),
    ("splash_4",      "7×4",    "Rain splash variant 4", 64, 46),
    # ── Autumn leaf sprites (the leaf tumbles through the four poses) ──────
    ("leaf_1",        "5×5",    "Leaf, tip up-right", 64, 20),
    ("leaf_2",        "5×5",    "Leaf, lying flat", 64, 20),
    ("leaf_3",        "5×5",    "Leaf, tip up-left", 64, 20),
    ("leaf_4",        "5×5",    "Leaf, on its end", 64, 20),
]

# Rain sprite slot names for easy lookup
RAIN_SPRITE_SLOTS = ["rain_drop_1", "rain_drop_2", "rain_drop_3", "rain_drop_4"]
SPLASH_SPRITE_SLOTS = ["splash_1", "splash_2", "splash_3", "splash_4"]
LEAF_SPRITE_SLOTS = ["leaf_1", "leaf_2", "leaf_3", "leaf_4"]


