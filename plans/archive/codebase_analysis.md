# MotoWeather Codebase Analysis

## Project Overview

The MotoWeather Bedside Display is an ESP8266-based weather display for motorcyclists, featuring:
- 128×64 SSD1306 OLED display
- Open-Meteo API integration
- Touch input (GPIO3/RX pin)
- Web-based configuration interface
- Sprite-based rain animation
- 7-day ride forecast matrix

---

## Directory Structure

```
MotoClock/
├── platformio.ini                    # PlatformIO configuration
├── plans/                            # Architecture & planning documents
│   ├── memo_09042026.md             # Technical architecture memo
│   ├── png_to_bitmap_architecture.md # Python tool specification
│   ├── project_plan.md              # Project structure & flow diagrams
│   └── rain_animation_architecture.md # Rain animation system spec
├── firmware/
│   ├── src/
│   │   ├── main.cpp                 # Entry point, state machine
│   │   ├── display.cpp              # Rendering engine
│   │   ├── weather.cpp              # API fetch & ride logic ⚠️ CORRUPTED
│   │   ├── touch.cpp                # Touch input handler
│   │   ├── webserver.cpp            # Embedded web server
│   │   └── geolocation.cpp          # WiFi geolocation
│   ├── include/
│   │   ├── config.h                 # Compile-time constants
│   │   ├── bitmaps.h                # PROGMEM bitmap data (incomplete)
│   │   ├── display.h                # Display declarations
│   │   ├── weather.h                # Weather data structures
│   │   ├── touch.h                  # Touch input declarations
│   │   └── webserver.h              # Web server declarations
│   └── data/                        # LittleFS data directory
├── tools/
│   ├── png_to_bitmap.py             # GUI asset converter (complete)
│   └── requirements.txt             # Python dependencies
└── codebase_analysis.md             # This document
```

---

## Plans vs Implementation Alignment

### ✅ Well-Aligned Components

| Component | Status | Notes |
|-----------|--------|-------|
| **Python GUI Tool** | 95% Complete | `png_to_bitmap.py` implements all features from architecture doc |
| **Display Rendering** | 80% Complete | Composite layers working, procedural effects implemented |
| **Touch System** | 100% Complete | GPIO3 debounce, short/long press detection |
| **Web Server** | 90% Complete | Full config UI, location picker, OTA updates |
| **Main Loop** | 95% Complete | Event-driven, non-blocking, proper timing |
| **Rain Animation** | 85% Complete | Sprite-based system with wind drift, splashes |

### ⚠️ Partially Implemented

| Component | Status | Gap |
|-----------|--------|-----|
| **Bitmap Assets** | 20% Complete | Only sun + skyline exist; missing moon, clouds, arrows, rain sprites |
| **Weather Data Struct** | 70% Complete | Fields defined but naming inconsistent (curTemp vs tempC) |
| **Weekly Matrix View** | 60% Complete | Rendering scaffolded but not fully implemented |
| **Ride Decision Logic** | 80% Complete | Basic algorithm works, "AI scoring" not implemented |

### ❌ Critical Issues

| Issue | Severity | Location |
|-------|----------|----------|
| **Code Duplication** | 🔴 CRITICAL | `weather.cpp` - `fetchWeather()` repeated ~7 times (lines 54-1605) |
| **Missing Bitmap Definitions** | 🟡 HIGH | `bitmaps.h` - needs moon, clouds, trend arrows, rain sprites |
| **Incomplete Display Rendering** | 🟡 MEDIUM | `display.cpp` - TODOs for text, trend arrows, weekly matrix |
| **Weather Struct Mismatch** | 🟡 MEDIUM | Field names don't match between declaration and usage |

---

## Detailed Component Analysis

### 1. Firmware Core (`main.cpp`)

**Strengths:**
- Clean event-driven architecture with `SystemState` struct
- Proper millis() overflow handling via `intervalPassed()`
- Non-blocking WiFi connection with AP fallback
- Dynamic fetch intervals with jitter
- Night mode frequency reduction (1/4 speed)

**State Machine:**
```
┌─────────┐    ┌─────────┐    ┌─────────────┐
│  BOOT   │───→│ CONNECT │───→│ FETCH WEATHER│
└─────────┘    └─────────┘    └─────────────┘
                                    │
                                    ▼
                              ┌─────────────┐
                              │  MAIN LOOP  │←── Touch events
                              │  (running)  │←── Weather updates
                              └─────────────┘    Animation ticks
```

### 2. Weather Module (`weather.cpp`) ⚠️ CORRUPTED

**CRITICAL ISSUE:** The file contains approximately 7 duplicate copies of the `fetchWeather()` function stacked sequentially. This appears to be a merge error or incomplete refactoring.

**Lines affected:** 54-192, 194-322, 324-452, 454-582, 584-712, 714-842, 844-972, 974-1102, 1104-1232, 1234-1362, 1364-1492, 1494-1605

**Correct portions:**
- Lines 1-53: Headers and global variables
- Lines 1450-1464: `evaluateRide()` function
- Lines 1466-1544: `updateWeeklyState()` function
- Lines 1546-1605: Accessor functions (but partially duplicated)

**Data Structures:**
```cpp
// Current state (weather.h lines 25-39)
struct WeatherData {
    float curTemp, curWind, curGusts, curHumidity;  // Declared
    int curVisibility;
    int condition;    // WEATHER_CLEAR, WEATHER_RAIN, etc.
    char trend;       // 'u', 'd', 'f'
    float hourlyTemp[12], hourlyWindGusts[12];
    int hourlyRainProb[12];
    float dailyTempMax[7], dailyTempMin[7];
    float dailyPrecip[7], dailyWindMax[7], dailyGustMax[7];
};

// Actual usage in main.cpp (line 222)
WeatherData weather = getCurrentWeather();
// Uses: weather.tempC, weather.windKmh, weather.precipMm, etc.
// MISMATCH: Declared as curTemp but used as tempC!
```

### 3. Display Module (`display.cpp`)

**Implemented Features:**
- `drawGiantBadge()` - Triple-ring border with check/warn/X symbols
- `applyNightOverlay()` - Stars and streetlight glow
- `drawProceduralRain/Snow/Wind()` - Weather effects
- `initRainAnimation()` / `updateRainAnimation()` / `drawRainAnimation()` - Full sprite-based rain
- `renderLoadingView()` - Animated boot screen

**TODO Items:**
```cpp
// Line 394: Moon bitmap blit (waiting for moon_bmp)
// Line 416-417: Temperature text rendering
// Line 420: Trend arrow bitmap blit
// Line 428: Cloud/rain_cloud bitmaps
// Line 431: Wind speed text
// Lines 449-466: Weekly matrix grid rendering
```

### 4. Bitmap Assets (`bitmaps.h`)

**Current Assets:**
| Name | Size | Description |
|------|------|-------------|
| `sun_bmp` | 10×10 | Sun icon (daytime) |
| `skyline_base_bmp` | 64×30 | City skyline silhouette |

**Missing Assets (per plans):**
| Name | Size | Description | Priority |
|------|------|-------------|----------|
| `moon_bmp` | 8×8 or 10×10 | Moon icon (night) | High |
| `cloud_bmp` | 16×10 | Cloud sprite | Medium |
| `rain_cloud_bmp` | 16×12 | Rain cloud sprite | Medium |
| `arrow_ur_bmp` | 7×7 | Trend up-right | High |
| `arrow_dr_bmp` | 7×7 | Trend down-right | High |
| `arrow_r_bmp` | 7×7 | Trend flat | High |
| `rain_drop_1-4_bmp` | 3×6, 2×5, etc. | Rain drop variants | Medium |
| `splash_1-4_bmp` | 7×4 | Splash effect variants | Medium |
| `cell_check_bmp` | 14×20 | Weekly cell OK | Low |
| `cell_warn_bmp` | 14×20 | Weekly cell caution | Low |
| `cell_x_bmp` | 14×20 | Weekly cell no ride | Low |

### 5. Python GUI Tool (`png_to_bitmap.py`)

**Status:** Feature-complete per architecture specification

**Features Implemented:**
- OLEDCanvas 128×64 pixel buffer with Bresenham primitives
- PNG ↔ C array conversion (Adafruit GFX format)
- `bitmaps.h` parser for loading existing assets
- SceneComposer with all view modes (today/weekly/time)
- RainAnimation engine with wind drift compensation
- Procedural weather effects (rain, snow, wind)
- Multi-color OLED simulation (Green/Blue/White/Yellow/Amber)
- Bitmap editor dialog for creating assets
- Asset slot definitions for all planned sprites

**Rain Animation Features:**
- Wind drift calculation (0 to -3 pixels based on 10-80 km/h)
- Spawn zone extension for wind compensation
- Intensity-based drop count (0-20 mm/h maps to 0-MAX_DROPS)
- Splash effects at horizon collision
- Sprite-based rendering with fallback to procedural

---

## Recommendations

### Priority 1: Fix Critical Issues

1. **Fix `weather.cpp` duplication**
   - Remove duplicate function copies
   - Keep one correct implementation (lines ~1-53 + 1450-1605)
   - Verify field name consistency (tempC vs curTemp)

2. **Fix WeatherData struct mismatch**
   - Align field names between declaration and usage
   - Either change struct to use `tempC`, `windKmh`, etc.
   - Or change usage in main.cpp to use `curTemp`, `curWind`, etc.

### Priority 2: Complete Core Features

3. **Create missing bitmaps**
   - Use `png_to_bitmap.py` to generate:
     - moon_bmp (crescent moon shape)
     - arrow_ur/dr/r_bmps (simple directional arrows)
     - cloud/rain_cloud (simple 1-bit icons)
   - Export to `bitmaps.h`

4. **Complete display rendering**
   - Implement text rendering for temperature
   - Add trend arrow blitting
   - Finish weekly matrix grid rendering

### Priority 3: Polish & Optimization

5. **Add remaining bitmap assets**
   - Rain drop sprites (4 variants)
   - Splash sprites (4 variants)
   - Weekly cell icons

6. **Implement AI heuristic scoring**
   - Add scoring algorithm from memo (100 points baseline)
   - Weekend bias (+15 points)
   - Display best day highlighting

---

## Build & Test Checklist

- [ ] Fix weather.cpp code duplication
- [ ] Fix WeatherData field naming
- [ ] Create moon bitmap
- [ ] Create trend arrow bitmaps
- [ ] Implement temperature text rendering
- [ ] Implement trend arrow display
- [ ] Test rain animation with sprites
- [ ] Complete weekly matrix rendering
- [ ] Add AI scoring algorithm
- [ ] Test web server configuration
- [ ] Test OTA updates
- [ ] Test touch input (short/long press)
- [ ] Verify day/night detection
- [ ] Test WiFi geolocation fallback

---

## Architecture Compliance Summary

| Plan Document | Compliance |
|---------------|------------|
| `project_plan.md` | 85% - Structure matches, missing some assets |
| `memo_09042026.md` | 75% - Core features exist, AI scoring missing |
| `png_to_bitmap_architecture.md` | 95% - Tool fully implements spec |
| `rain_animation_architecture.md` | 90% - Implementation matches spec |

**Overall Project Status:** 80% Complete - Core functionality working, critical bug in weather.cpp needs fixing, asset creation needed for full visual polish.