# MotoWeather Project Todo List

## 🔴 Priority 1: Critical Bug Fixes

- [ ] Fix weather.cpp code duplication
  - Remove duplicate `fetchWeather()` function copies (lines 54-1448 approximately)
  - Keep one correct implementation
  - Verify the function ends at correct line before `evaluateRide()`
  
- [ ] Fix WeatherData struct field naming mismatch
  - Option A: Change struct fields from `curTemp`, `curWind`, etc. to `tempC`, `windKmh`, `precipMm`, `gustKmh`
  - Option B: Change usage in main.cpp (line 222-243) to use `curTemp`, `curWind`, etc.
  - Ensure consistency across weather.h, weather.cpp, and main.cpp

## 🟡 Priority 2: Core Features

- [ ] Create moon bitmap asset
  - Design crescent moon shape (8×8 or 10×10)
  - Use png_to_bitmap.py to convert
  - Add to bitmaps.h
  
- [ ] Create trend arrow bitmaps
  - arrow_ur_bmp (up-right, 7×7)
  - arrow_dr_bmp (down-right, 7×7)  
  - arrow_r_bmp (flat, 7×7)
  - Add to bitmaps.h
  
- [ ] Implement temperature text rendering in display.cpp
  - Complete TODO at line 416-417
  - Use Adafruit GFX text functions
  
- [ ] Implement trend arrow display in renderSkylineCard()
  - Complete TODO at line 420
  - Blit appropriate arrow based on weather.trend value

## 🟢 Priority 3: Visual Polish

- [ ] Create cloud bitmap assets
  - cloud_bmp (16×10)
  - rain_cloud_bmp (16×12)
  - Add to bitmaps.h
  
- [ ] Create rain animation sprites
  - rain_drop_1_bmp through rain_drop_4_bmp (3×6, 2×5, etc.)
  - splash_1_bmp through splash_4_bmp (7×4)
  - Add to bitmaps.h
  
- [ ] Complete weekly matrix rendering
  - Finish renderWeeklyMatrix() implementation (lines 442-467)
  - Draw day headers (M T W T F S S)
  - Draw AM/PM row labels
  - Draw badge symbols in grid cells
  
- [ ] Implement wind speed text in bottom card
  - Complete TODO at line 431

## 🔵 Priority 4: Advanced Features

- [ ] Implement AI heuristic scoring algorithm
  - Add to weather.cpp based on memo_09042026.md
  - 100 points baseline
  - Deduct for: temp deviation from 20°C, rain mm, wind above 20km/h
  - Weekend bonus: +15 points for Sat/Sun
  - Display best day highlighting in UI
  
- [ ] Create weekly cell bitmap assets (optional)
  - cell_check_bmp (14×20)
  - cell_warn_bmp (14×20)
  - cell_x_bmp (14×20)
  - cell_empty_bmp (14×20)

## ✅ Testing & Validation

- [ ] Build firmware after weather.cpp fix
- [ ] Test rain animation with sprites
- [ ] Test web server configuration interface
- [ ] Test OTA update functionality
- [ ] Test touch input (short tap / long press)
- [ ] Verify day/night detection with real sunrise/sunset
- [ ] Test WiFi geolocation fallback
- [ ] Verify all display modes (today/weekly/time)

## 📝 Documentation

- [ ] Update codebase_analysis.md when items complete
- [ ] Document any API changes
- [ ] Add inline comments for complex rain animation logic

---

## Quick Reference: File Locations

| Task | Primary File(s) |
|------|-----------------|
| Weather bug fix | `firmware/src/weather.cpp`, `firmware/include/weather.h` |
| Bitmap creation | `tools/png_to_bitmap.py` → `firmware/include/bitmaps.h` |
| Display rendering | `firmware/src/display.cpp` |
| Text/Arrows | `firmware/src/display.cpp` lines 416-420 |
| Weekly matrix | `firmware/src/display.cpp` lines 442-467 |
| AI scoring | `firmware/src/weather.cpp` |