// MotoWeather Bedside Display — Display Rendering Implementation
// Composite rendering engine for SSD1306 128x64 OLED

#include "display.h"
#include "bitmaps.h"
#include "weather.h"

// Rain animation state
static RainDrop rainDrops[MAX_RAIN_DROPS];
static Splash splashes[MAX_SPLASHES];
static float currentRainIntensity = 5.0f;  // Track current rain intensity
static int currentWindSpeed = 0;  // Track current wind speed



// Helper: draw vertical line
static void drawVLine(Adafruit_SSD1306 &display, int x, int y, int h) {
    for (int i = 0; i < h; i++) {
        display.drawPixel(x, y + i, SSD1306_WHITE);
    }
}

// Helper: draw filled circle using Bresenham
static void drawFilledCircle(Adafruit_SSD1306 &display, int cx, int cy, int r) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                display.drawPixel(cx + x, cy + y, SSD1306_WHITE);
            }
        }
    }
}

#ifdef DISPLAY_STATUS_DEBUG
void drawDebugStatus(Adafruit_SSD1306 &display, const char* message) {
    display.fillRect(0, 20, 128, 24, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(message, 0, 0, &x1, &y1, &w, &h);
    
    display.setCursor((128 - w) / 2, 26);
    display.print(message);
    display.display();
}
#endif

// Giant badge — triple-ring border with check/warn/X
void drawGiantBadge(Adafruit_SSD1306 &display, char type) {
    int cx = 32, cy = 32;
    
    // Three concentric circles for a thick border ring
    for (int r = 28; r >= 26; r--) {
        display.drawCircle(cx, cy, r, SSD1306_WHITE);
    }
    
    // Draw the badge symbol inside based on type
    if (type == BADGE_CHECK) {
        // Thick checkmark: a short stroke down-right and a long stroke up-right, 3 px thick
        for (int o = -1; o <= 1; o++) {
            display.drawLine(cx - 14, cy + o, cx - 5, cy + 10 + o, SSD1306_WHITE);
            display.drawLine(cx - 5, cy + 10 + o, cx + 14, cy - 12 + o, SSD1306_WHITE);
        }
    } else if (type == BADGE_WARN) {
        // Thick exclamation: vertical bar + bottom dot
        // Vertical bar (center)
        drawVLine(display, cx - 1, cy - 14, 20);
        drawVLine(display, cx, cy - 14, 20);
        drawVLine(display, cx + 1, cy - 14, 20);
        // Bottom dot
        drawFilledCircle(display, cx, cy + 10, 3);
    } else if (type == BADGE_X) {
        // Two diagonal lines crossing at centre
        // Top-left to bottom-right
        for (int i = -12; i <= 12; i++) {
            display.drawPixel(cx + i, cy + i, SSD1306_WHITE);
            display.drawPixel(cx + i + 1, cy + i, SSD1306_WHITE);
            display.drawPixel(cx + i, cy + i + 1, SSD1306_WHITE);
        }
        // Top-right to bottom-left
        for (int i = -12; i <= 12; i++) {
            display.drawPixel(cx + i, cy - i, SSD1306_WHITE);
            display.drawPixel(cx + i - 1, cy - i, SSD1306_WHITE);
            display.drawPixel(cx + i, cy - i + 1, SSD1306_WHITE);
        }
    }
}

// Night overlay — streetlight glow + random stars
// Using seed for deterministic but random-looking stars
void applyNightOverlay(Adafruit_SSD1306 &display) {
    // Seed-based pseudo-random using simple hash
    unsigned int seed = 12345; // Fixed seed for deterministic stars
    
    // Draw 3-5 single white pixels randomly in sky region (x: 64-127, y: 0-11)
    for (int i = 0; i < 5; i++) {
        seed = seed * 1103515245 + 12345; // LCG
        int starX = 64 + (seed % 64);
        seed = seed * 1103515245 + 12345;
        int starY = seed % 12;
        if (i < 4) { // Only 4 stars
            display.drawPixel(starX, starY, SSD1306_WHITE);
        }
    }
    
    // Draw 2x2 filled white rectangle at streetlight position
    display.drawPixel(STREETLIGHT_BX, STREETLIGHT_BY, SSD1306_WHITE);
    display.drawPixel(STREETLIGHT_BX + 1, STREETLIGHT_BY, SSD1306_WHITE);
    display.drawPixel(STREETLIGHT_BX, STREETLIGHT_BY + 1, SSD1306_WHITE);
    display.drawPixel(STREETLIGHT_BX + 1, STREETLIGHT_BY + 1, SSD1306_WHITE);
}

// Procedural rain — diagonal line loop
void drawProceduralRain(Adafruit_SSD1306 &display, int intensity) {
    int numLines;
    if (intensity == 1) numLines = 3;
    else if (intensity == 2) numLines = 6;
    else numLines = 10;
    
    // Draw diagonal lines spread across x: 64-127, y: 0-27
    for (int i = 0; i < numLines; i++) {
        int startX = 64 + (i * 127 / numLines);
        int startY = (i * 27 / numLines);
        display.drawLine(startX, startY, startX - 2, startY + 4, SSD1306_WHITE);
    }
}

// Procedural snow — scattered pixels + roof line
void drawProceduralSnow(Adafruit_SSD1306 &display, int intensity) {
    // Scatter intensity*4 white pixels randomly in x: 64-127, y: 0-31
    unsigned int seed = 54321;
    for (int i = 0; i < intensity * 4; i++) {
        seed = seed * 1103515245 + 12345;
        int x = 64 + (seed % 64);
        seed = seed * 1103515245 + 12345;
        int y = seed % 32;
        display.drawPixel(x, y, SSD1306_WHITE);
    }
    
    // At intensity >= 2: draw horizontal white line at church roof
    if (intensity >= 2) {
        for (int x = 64; x < 127; x++) {
            display.drawPixel(x, CHURCH_ROOF_Y, SSD1306_WHITE);
        }
    }
}

// Procedural wind — horizontal swoosh dashes
void drawProceduralWind(Adafruit_SSD1306 &display, int speed) {
    // Only active when speed >= 25 km/h
    if (speed < 25) return;
    
    // Three dashed horizontal lines at y: 5, 9, 14 across x: 64-127
    int yPositions[] = {5, 9, 14};
    // Dash pattern: 4 on, 2 off, 3 on, 1 off, 2 on (12 px), repeated across the card
    int pattern[] = {4, 2, 3, 1, 2};
    for (int row = 0; row < 3; row++) {
        int y = yPositions[row];
        int x = 64;
        while (x < 128) {
            for (int p = 0; p < 5; p++) {
                bool on = (p % 2 == 0);   // entries 0, 2, 4 are dashes, 1 and 3 are gaps
                for (int i = 0; i < pattern[p]; i++) {
                    if (on && x < 128) display.drawPixel(x, y, SSD1306_WHITE);
                    x++;
                }
            }
        }
    }
}

// Reset a rain drop to random position at top
// Reset a single rain drop to a new random position above the skyline
void resetRainDrop(RainDrop &drop) {
    // Calculate extended spawn zone based on wind for better coverage
    // Average fall frames = HORIZON_Y / avg_speed (roughly 9 frames)
    // x_spawn_extend = abs(wind_drift) * 9
    int xSpawnExtend = 0;  // Simplified for reset
    
    drop.x = RAIN_AREA_X_START + random(RAIN_AREA_X_END - RAIN_AREA_X_START + 1 + xSpawnExtend);
    drop.y = random(-8, -1);  // Start just above top edge
    drop.targetY = HORIZON_Y + random(10);  // Slight variation in ground level
    drop.speed = 3 + random(4);  // Speed: 3-6 pixels per frame (matching Python)
    drop.spriteIdx = random(4);  // 0-3 sprite variants
    drop.active = true;
}

// Initialize rain animation
void initRainAnimation() {
    randomSeed(analogRead(0));
    for (int i = 0; i < MAX_RAIN_DROPS; i++) {
        // Reset first to initialize all fields
        rainDrops[i].x = RAIN_AREA_X_START + random(64);
        rainDrops[i].y = random(-30, HORIZON_Y);  // Spread across screen vertically
        rainDrops[i].targetY = HORIZON_Y + random(10);
        rainDrops[i].speed = 3 + random(4);  // 3-6 px/frame
        rainDrops[i].spriteIdx = random(4);
        rainDrops[i].active = true;
    }
    // Initialize splashes as inactive
    for (int i = 0; i < MAX_SPLASHES; i++) {
        splashes[i].active = false;
        splashes[i].frameCounter = 0;
        splashes[i].spriteIdx = 0;
    }
}

// Calculate wind drift based on wind speed (matching Python logic)
// Wind 0-10 km/h: straight down (no drift)
// Wind 10-80 km/h: progressively more drift
// Wind >80 km/h: max drift
static int computeWindDrift(int windSpeed) {
    if (windSpeed <= 10) return 0;
    // Scale: 10 km/h = 0 drift, 80 km/h = max drift (-1 to -3)
    float driftScaled = (windSpeed - 10) / 70.0;
    int drift = -int(1 + driftScaled * 2);
    if (drift < -3) drift = -3;
    if (drift > -1) drift = -1;
    return drift;
}

// Calculate target active drops based on rain intensity (mm/h)
// Matching Python: 0 mm/h = 0 drops, 10 mm/h = MAX_RAIN_DROPS
static int computeTargetDrops(float rainIntensityMMH) {
    int baseTarget = int(rainIntensityMMH * (MAX_RAIN_DROPS / 10.0));
    return min(MAX_RAIN_DROPS, baseTarget);
}

// Update rain animation — move drops, check collision, create splashes
// windSpeed: 0-60 km/h
// rainIntensity: 0-20 mm/h (controls how many drops are active)
void updateRainAnimation(int windSpeed, float rainIntensity) {
    // Store current values for drawRainAnimation to use
    currentWindSpeed = windSpeed;
    currentRainIntensity = rainIntensity;
    
    int windDrift = computeWindDrift(windSpeed);
    int targetDrops = computeTargetDrops(rainIntensity);
    
    // Count active drops
    int activeCount = 0;
    for (int i = 0; i < MAX_RAIN_DROPS; i++) {
        if (rainDrops[i].active) activeCount++;
    }
    
    // Activate or deactivate drops to match target
    if (activeCount < targetDrops) {
        for (int i = 0; i < MAX_RAIN_DROPS; i++) {
            if (!rainDrops[i].active) {
                resetRainDrop(rainDrops[i]);
                activeCount++;
                if (activeCount >= targetDrops) break;
            }
        }
    } else if (activeCount > targetDrops) {
        for (int i = 0; i < MAX_RAIN_DROPS; i++) {
            if (rainDrops[i].active) {
                rainDrops[i].active = false;
                activeCount--;
                if (activeCount <= targetDrops) break;
            }
        }
    }
    
    // Move rain drops
    for (int i = 0; i < MAX_RAIN_DROPS; i++) {
        if (!rainDrops[i].active) continue;
        
        // Move drop: vertical fall + horizontal wind drift
        rainDrops[i].x += windDrift;
        rainDrops[i].y += rainDrops[i].speed;
        
        // Check if drop hit the ground (targetY)
        if (rainDrops[i].y >= rainDrops[i].targetY) {
            // Create a splash
            for (int j = 0; j < MAX_SPLASHES; j++) {
                if (!splashes[j].active) {
                    splashes[j].x = rainDrops[i].x;
                    splashes[j].y = rainDrops[i].targetY;
                    splashes[j].frameCounter = 3;  // 3 frames (matching Python SPLASH_FRAMES)
                    splashes[j].spriteIdx = random(4);  // Random splash variant
                    splashes[j].active = true;
                    break;
                }
            }
            // Reset the drop
            resetRainDrop(rainDrops[i]);
            continue;
        }
        
        // Reset if off left edge or below canvas
        if (rainDrops[i].x < RAIN_AREA_X_START || rainDrops[i].y >= 64) {
            resetRainDrop(rainDrops[i]);
        }
    }
    
    // Update splash animations
    for (int i = 0; i < MAX_SPLASHES; i++) {
        if (splashes[i].active) {
            splashes[i].frameCounter--;
            if (splashes[i].frameCounter <= 0) {
                splashes[i].active = false;
            }
        }
    }
}

// Draw rain animation — render active drops and splashes with sprite support
void drawRainAnimation(Adafruit_SSD1306 &display) {
    // Draw rain drops (sprite-based if bitmaps available, else fallback to procedural)
    for (int i = 0; i < MAX_RAIN_DROPS; i++) {
        if (!rainDrops[i].active) continue;
        
        // Check if sprite bitmaps are available
        #ifdef RAIN_DROP_1_BMP_W
        // Use sprite-based rendering - select sprite based on variant
        const uint8_t* sprite = nullptr;
        int spriteW = 0, spriteH = 0;
        
        switch (rainDrops[i].spriteIdx % 4) {
            case 0: sprite = rain_drop_1_bmp; spriteW = RAIN_DROP_1_BMP_W; spriteH = RAIN_DROP_1_BMP_H; break;
            case 1: sprite = rain_drop_2_bmp; spriteW = RAIN_DROP_2_BMP_W; spriteH = RAIN_DROP_2_BMP_H; break;
            case 2: sprite = rain_drop_3_bmp; spriteW = RAIN_DROP_3_BMP_W; spriteH = RAIN_DROP_3_BMP_H; break;
            case 3: sprite = rain_drop_4_bmp; spriteW = RAIN_DROP_4_BMP_W; spriteH = RAIN_DROP_4_BMP_H; break;
        }
        
        if (sprite) {
            display.drawBitmap(rainDrops[i].x, rainDrops[i].y, sprite, spriteW, spriteH, SSD1306_WHITE);
        } else
        #endif
        {
            // Fallback: procedural 2x4 elongated pixel drop
            display.drawPixel(rainDrops[i].x, rainDrops[i].y, SSD1306_WHITE);
            display.drawPixel(rainDrops[i].x, rainDrops[i].y + 1, SSD1306_WHITE);
            display.drawPixel(rainDrops[i].x, rainDrops[i].y + 2, SSD1306_WHITE);
            display.drawPixel(rainDrops[i].x - 1, rainDrops[i].y + 1, SSD1306_WHITE);
        }
    }
    
    // Draw splashes (sprite-based if bitmaps available, else procedural)
    for (int i = 0; i < MAX_SPLASHES; i++) {
        if (!splashes[i].active) continue;
        
        #ifdef SPLASH_1_BMP_W
        // Use splash sprite
        const uint8_t* splashSprite = nullptr;
        int splashW = 0, splashH = 0;
        
        switch (splashes[i].spriteIdx % 4) {
            case 0: splashSprite = splash_1_bmp; splashW = SPLASH_1_BMP_W; splashH = SPLASH_1_BMP_H; break;
            case 1: splashSprite = splash_2_bmp; splashW = SPLASH_2_BMP_W; splashH = SPLASH_2_BMP_H; break;
            case 2: splashSprite = splash_3_bmp; splashW = SPLASH_3_BMP_W; splashH = SPLASH_3_BMP_H; break;
            case 3: splashSprite = splash_4_bmp; splashW = SPLASH_4_BMP_W; splashH = SPLASH_4_BMP_H; break;
        }
        
        if (splashSprite) {
            // Center splash on impact point
            display.drawBitmap(splashes[i].x - splashW/2, splashes[i].y - splashH, splashSprite, splashW, splashH, SSD1306_WHITE);
        } else
        #endif
        {
            // Fallback: procedural splash (width based on frame counter)
            int fx = splashes[i].frameCounter;  // 3 = wide, 1 = narrow
            display.drawPixel(splashes[i].x - fx, splashes[i].y, SSD1306_WHITE);
            display.drawPixel(splashes[i].x + fx, splashes[i].y, SSD1306_WHITE);
            if (splashes[i].frameCounter >= 2) {
                display.drawPixel(splashes[i].x - fx + 1, splashes[i].y - 1, SSD1306_WHITE);
                display.drawPixel(splashes[i].x + fx - 1, splashes[i].y - 1, SSD1306_WHITE);
            }
        }
    }
}

// Render skyline card — composite render of right top half
void renderSkylineCard(Adafruit_SSD1306 &display, bool isNight, int weatherCondition, int intensity, int windSpeed, const char *tempStr, char trendArrow) {
    // Layer 1: blit skyline_base_bmp at (SKYLINE_X, SKYLINE_Y)
    display.drawBitmap(SKYLINE_X, SKYLINE_Y, skyline_base_bmp, SKYLINE_BASE_BMP_W, SKYLINE_BASE_BMP_H, SSD1306_WHITE);
    
    // Layer 2: if night → applyNightOverlay + blit moon; else → blit sun
    if (isNight) {
        applyNightOverlay(display);
        display.drawBitmap(MOON_X, MOON_Y, moon_bmp, MOON_BMP_W, MOON_BMP_H, SSD1306_WHITE);
    } else {
        display.drawBitmap(SUN_X, SUN_Y, sun_bmp, SUN_BMP_W, SUN_BMP_H, SSD1306_WHITE);
    }
    
    // Layer 3: draw weather effect based on weatherCondition
    if (weatherCondition == WEATHER_RAIN) {
        // Use animated sprite rain if intensity > 0, else procedural
        if (intensity > 0) {
            drawRainAnimation(display);
        } else {
            drawProceduralRain(display, 1);
        }
    } else if (weatherCondition == WEATHER_SNOW) {
        drawProceduralSnow(display, intensity);
    } else if (weatherCondition == WEATHER_WIND) {
        drawProceduralWind(display, windSpeed);
    }
    
    // Layer 4: draw tempStr text at (TEMP_X, TEMP_Y) with black background box
    // First draw black box behind text for readability
    display.fillRect(TEMP_X - 1, TEMP_Y - 1, 20, 10, SSD1306_BLACK);
    
    // Draw temperature text
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(TEMP_X, TEMP_Y);
    display.print(tempStr);
    
    // Blit trend arrow at (ARROW_X, ARROW_Y) based on trend character
    // 'u' = up-right (rising), 'd' = down-right (falling), 'f' = right (flat)
    const uint8_t* arrowSprite = nullptr;
    int arrowW = 0, arrowH = 0;
    
    if (trendArrow == 'u') {
        arrowSprite = arrow_ur_bmp;
        arrowW = ARROW_UR_BMP_W;
        arrowH = ARROW_UR_BMP_H;
    } else if (trendArrow == 'd') {
        arrowSprite = arrow_dr_bmp;
        arrowW = ARROW_DR_BMP_W;
        arrowH = ARROW_DR_BMP_H;
    } else {
        // 'f' or default = flat/right arrow
        arrowSprite = arrow_r_bmp;
        arrowW = ARROW_R_BMP_W;
        arrowH = ARROW_R_BMP_H;
    }
    
    if (arrowSprite) {
        display.drawBitmap(ARROW_X, ARROW_Y, arrowSprite, arrowW, arrowH, SSD1306_WHITE);
    }
}

// Render bottom card — weather icon, wind and precipitation text (x: 64-127, y: 42-63)
void renderBottomCard(Adafruit_SSD1306 &display, int weatherCondition, int windSpeed, float precipMm) {
    // Icon area: x 66-90, y 44-62
    const int ix = 66, iy = 44;
    if (weatherCondition == WEATHER_RAIN || weatherCondition == WEATHER_SNOW) {
        // Cloud
        display.fillCircle(ix + 7, iy + 8, 5, SSD1306_WHITE);
        display.fillCircle(ix + 14, iy + 5, 6, SSD1306_WHITE);
        display.fillCircle(ix + 20, iy + 9, 4, SSD1306_WHITE);
        display.fillRect(ix + 7, iy + 8, 14, 5, SSD1306_WHITE);
        // Falling drops (rain) or flakes (snow)
        for (int i = 0; i < 3; i++) {
            int x = ix + 6 + i * 7;
            if (weatherCondition == WEATHER_RAIN) {
                display.drawLine(x + 1, iy + 15, x - 1, iy + 19, SSD1306_WHITE);
            } else {
                display.drawPixel(x, iy + 16, SSD1306_WHITE);
                display.drawPixel(x - 1, iy + 18, SSD1306_WHITE);
            }
        }
    } else if (weatherCondition == WEATHER_WIND) {
        // Three gusting lines
        for (int i = 0; i < 3; i++) {
            int y = iy + 4 + i * 6;
            int len = (i == 1) ? 22 : 16;
            display.drawFastHLine(ix, y, len, SSD1306_WHITE);
            display.drawCircleHelper(ix + len, y - 2, 2, 2, SSD1306_WHITE);
        }
    } else {
        // Clear: sun with rays
        const int cx = ix + 11, cy = iy + 9;
        display.fillCircle(cx, cy, 4, SSD1306_WHITE);
        for (int a = 0; a < 8; a++) {
            static const int8_t dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
            static const int8_t dy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
            display.drawLine(cx + dx[a] * 6, cy + dy[a] * 6, cx + dx[a] * 8, cy + dy[a] * 8, SSD1306_WHITE);
        }
    }

    // Text column: wind speed and precipitation
    char buf[12];
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    snprintf(buf, sizeof(buf), "%dkm/h", windSpeed);
    display.setCursor(93, 46);
    display.print(buf);
    snprintf(buf, sizeof(buf), "%.1fmm", precipMm);
    display.setCursor(93, 55);
    display.print(buf);
}

// Draw a ride rating glyph centred on (cx, cy), about 14px square
static void drawRatingGlyph(Adafruit_SSD1306 &display, int cx, int cy, char rating) {
    if (rating == RIDE_GOOD) {
        for (int o = 0; o <= 1; o++) {
            display.drawLine(cx - 6, cy + o, cx - 2, cy + 4 + o, SSD1306_WHITE);
            display.drawLine(cx - 2, cy + 4 + o, cx + 6, cy - 5 + o, SSD1306_WHITE);
        }
    } else if (rating == RIDE_CAUTION) {
        display.fillRect(cx - 1, cy - 7, 3, 9, SSD1306_WHITE);
        display.fillRect(cx - 1, cy + 4, 3, 3, SSD1306_WHITE);
    } else if (rating == RIDE_DONT) {
        for (int o = 0; o <= 1; o++) {
            display.drawLine(cx - 6 + o, cy - 6, cx + 6 + o, cy + 6, SSD1306_WHITE);
            display.drawLine(cx - 6 + o, cy + 6, cx + 6 + o, cy - 6, SSD1306_WHITE);
        }
    } else {
        display.drawFastHLine(cx - 4, cy, 9, SSD1306_WHITE);   // unknown
    }
}

// Render weekly matrix — full-screen 7-column AM/PM grid, first column is today
void renderWeeklyMatrix(Adafruit_SSD1306 &display, const char weekAM[7], const char weekPM[7], uint8_t startDow, int bestDay) {
    static const char dayLetters[7] = { 'S', 'M', 'T', 'W', 'T', 'F', 'S' };   // Sunday first
    const int labelW = 16, colW = 16;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Day headers (today underlined, best day in inverse video)
    for (int i = 0; i < 7; i++) {
        int x = labelW + i * colW + colW / 2 - 3;
        bool best = (i == bestDay);
        if (best) {
            display.fillRect(labelW + i * colW, 0, colW, 11, SSD1306_WHITE);
        }
        display.setTextColor(best ? SSD1306_BLACK : SSD1306_WHITE);
        display.setCursor(x, 1);
        display.print(dayLetters[(startDow + i) % 7]);
    }
    display.setTextColor(SSD1306_WHITE);
    display.drawFastHLine(labelW + 2, 10, colW - 4, bestDay == 0 ? SSD1306_BLACK : SSD1306_WHITE);   // underline today
    display.drawFastHLine(0, 12, 128, SSD1306_WHITE);

    // Row labels
    display.setCursor(1, 24);
    display.print("AM");
    display.setCursor(1, 49);
    display.print("PM");
    display.drawFastHLine(0, 38, 128, SSD1306_WHITE);

    // Ride badges
    for (int i = 0; i < 7; i++) {
        int cx = labelW + i * colW + colW / 2;
        drawRatingGlyph(display, cx, 26, weekAM[i]);
        drawRatingGlyph(display, cx, 51, weekPM[i]);
    }
}

// Render primary view — full composite display (caller flushes with display.display())
void renderPrimaryView(Adafruit_SSD1306 &display, char badgeType, bool isNight, int weatherCondition, int intensity, int windSpeed, const char *tempStr, char trendArrow, float precipMm) {
    // Clear the display first
    display.clearDisplay();
    
    // Left half: drawGiantBadge(badgeType)
    drawGiantBadge(display, badgeType);
    
    // Draw divider line
    display.drawLine(64, 0, 64, 63, SSD1306_WHITE);
    
    // Right top: renderSkylineCard(...)
    renderSkylineCard(display, isNight, weatherCondition, intensity, windSpeed, tempStr, trendArrow);
    
    // Draw horizontal divider between top and bottom cards
    display.drawLine(64, 41, 127, 41, SSD1306_WHITE);
    
    // Right bottom: renderBottomCard(...)
    renderBottomCard(display, weatherCondition, windSpeed, precipMm);
}

// Render loading view — rotating badge circle and status text
void renderLoadingView(Adafruit_SSD1306 &display, const char* line1, const char* line2, unsigned long timeMs) {
    display.clearDisplay();
    
    // Draw rotating badge circle
    int cx = 32, cy = 32;
    // 1 full rotation per second (360 degrees / 1000 ms)
    int startAngle = (timeMs % 1000) * 360 / 1000;
    int endAngle = startAngle + 270; // 270 degree arc
    
    // Sine/cosine table in 4 degree steps (x256), built once: float trig per pixel is slow
    static int16_t cosTable[90], sinTable[90];
    static bool tablesReady = false;
    if (!tablesReady) {
        for (int i = 0; i < 90; i++) {
            float rad = i * 4 * 3.14159f / 180.0f;
            cosTable[i] = (int16_t)(cos(rad) * 256);
            sinTable[i] = (int16_t)(sin(rad) * 256);
        }
        tablesReady = true;
    }

    for (int r = 28; r >= 26; r--) {
        for (int angle = startAngle; angle < endAngle; angle += 4) {
            int i = (angle % 360) / 4;
            display.drawPixel(cx + ((r * cosTable[i]) >> 8), cy + ((r * sinTable[i]) >> 8), SSD1306_WHITE);
        }
    }
    
    // Draw divider line
    display.drawLine(64, 0, 64, 63, SSD1306_WHITE);
    
    // Draw status text on the right
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    if (line1) {
        display.setCursor(68, 24);
        display.print(line1);
    }
    if (line2) {
        display.setCursor(68, 36);
        display.print(line2);
    }
    
}

// Render setup AP instructions — full 128x64 text screen
void renderApInfoView(Adafruit_SSD1306 &display, const char* ssid, const char* password, const char* ip) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("Setup: join WiFi");
    display.setCursor(0, 14);
    display.print(ssid);
    display.setCursor(0, 26);
    display.print("pw ");
    display.print(password);
    display.setCursor(0, 40);
    display.print("then open");
    display.setCursor(0, 52);
    display.print(ip);
}

// Centre a short number in a column of width w starting at x
static void printCentered(Adafruit_SSD1306 &display, int x, int w, int y, const char* text) {
    int textW = (int)strlen(text) * 6 - 1;
    display.setCursor(x + (w - textW) / 2, y);
    display.print(text);
}

// Render the next hours as a strip of columns
void renderHourlyView(Adafruit_SSD1306 &display, const HourSlice* hours, size_t count, int firstHour,
                      bool hasLeave, int leaveHour, bool leaveNow, int updHour, int updMinute) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (count == 0) {
        display.setCursor(16, 28);
        display.print("No hourly data");
        return;
    }

    const int colW = 21, barBottom = 44, barMax = 24;
    size_t cols = count < 6 ? count : 6;
    char buf[20];
    for (size_t i = 0; i < cols; i++) {
        const HourSlice& h = hours[i];
        int x = (int)i * colW + 1;

        snprintf(buf, sizeof(buf), "%02d", (firstHour + (int)i) % 24);
        printCentered(display, x, colW, 0, buf);

        if (!h.valid) {
            printCentered(display, x, colW, 22, "--");
            continue;
        }
        snprintf(buf, sizeof(buf), "%d", h.tempC);
        printCentered(display, x, colW, 10, buf);

        // Rain: bar for the amount, dotted line at the chance of rain
        if (h.rainTenthMm > 0) {
            int barH = 2 + (int)h.rainTenthMm * 6 / 10;
            if (barH > barMax) barH = barMax;
            display.fillRect(x + 6, barBottom - barH + 1, 9, barH, SSD1306_WHITE);
        }
        if (h.rainProb != 255 && h.rainProb > 0) {
            int y = barBottom - (int)h.rainProb * barMax / 100;
            for (int dx = 2; dx < 19; dx += 2) {
                display.drawPixel(x + dx, y, SSD1306_WHITE);
            }
        }

        snprintf(buf, sizeof(buf), "%d", h.gustKmh);
        printCentered(display, x, colW, 47, buf);
    }
    display.drawFastHLine(0, barBottom + 1, 128, SSD1306_WHITE);

    // Footer: best time to leave, time of the last update
    display.setCursor(0, 57);
    if (hasLeave) {
        if (leaveNow) {
            display.print("Leave now");
        } else {
            snprintf(buf, sizeof(buf), "Best %02d:00", leaveHour % 24);
            display.print(buf);
        }
    }
    if (updHour >= 0) {
        snprintf(buf, sizeof(buf), "upd %02d:%02d", updHour % 24, updMinute % 60);
        display.setCursor(128 - (int)strlen(buf) * 6 + 1, 57);
        display.print(buf);
    }
}

// Status marks in the free corners of the primary view's left half
void renderStatusMarks(Adafruit_SSD1306 &display, bool showTomorrow, int wifiBars) {
    if (showTomorrow) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 57);
        display.print("TMR");
    }
    if (wifiBars < 0) {
        // Not connected: a small cross where the bars would be
        display.drawLine(55, 57, 61, 63, SSD1306_WHITE);
        display.drawLine(61, 57, 55, 63, SSD1306_WHITE);
        return;
    }
    for (int i = 0; i < 4; i++) {
        int x = 51 + i * 3;
        int h = 2 + i * 2;
        if (i < wifiBars) {
            display.fillRect(x, 64 - h, 2, h, SSD1306_WHITE);
        } else {
            display.drawPixel(x, 63, SSD1306_WHITE);
        }
    }
}

// ---- Kids variant: what to wear -------------------------------------------------------------------

// Filled convex quadrilateral
static void fillQuad(Adafruit_SSD1306 &d, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3) {
    d.fillTriangle(x0, y0, x1, y1, x2, y2, SSD1306_WHITE);
    d.fillTriangle(x0, y0, x2, y2, x3, y3, SSD1306_WHITE);
}

// T-shirt silhouette. Coordinates are given for a 48x30 box and scaled by num/den.
static void drawShirt(Adafruit_SSD1306 &d, int ox, int oy, int num, int den) {
    #define SX(v) (ox + (v) * num / den)
    #define SY(v) (oy + (v) * num / den)
    d.fillRect(SX(12), SY(1), SX(36) - SX(12), SY(30) - SY(1), SSD1306_WHITE);
    fillQuad(d, SX(13), SY(1), SX(0), SY(9), SX(5), SY(17), SX(13), SY(12));
    fillQuad(d, SX(35), SY(1), SX(48), SY(9), SX(43), SY(17), SX(35), SY(12));
    d.fillTriangle(SX(19), SY(0), SX(29), SY(0), SX(24), SY(8), SSD1306_BLACK);   // neck
    #undef SX
    #undef SY
}

// Sweater silhouette with long sleeves and a ribbed hem, in a 56x52 box
static void drawSweater(Adafruit_SSD1306 &d, int ox, int oy) {
    d.fillRect(ox + 14, oy + 1, 28, 50, SSD1306_WHITE);
    fillQuad(d, ox + 15, oy + 1, ox + 6, oy + 6, ox + 1, oy + 40, ox + 9, oy + 42);
    fillQuad(d, ox + 41, oy + 1, ox + 50, oy + 6, ox + 55, oy + 40, ox + 47, oy + 42);
    d.fillTriangle(ox + 22, oy, ox + 34, oy, ox + 28, oy + 8, SSD1306_BLACK);     // neck
    d.drawFastHLine(ox + 14, oy + 45, 28, SSD1306_BLACK);                          // hem
    d.drawLine(ox + 1, oy + 36, ox + 8, oy + 38, SSD1306_BLACK);                   // cuffs
    d.drawLine(ox + 55, oy + 36, ox + 48, oy + 38, SSD1306_BLACK);
}

// Shorts silhouette, 34x24
static void drawShorts(Adafruit_SSD1306 &d, int ox, int oy) {
    d.fillRect(ox, oy, 34, 24, SSD1306_WHITE);
    d.fillTriangle(ox + 17, oy + 11, ox + 13, oy + 24, ox + 21, oy + 24, SSD1306_BLACK);   // gap between the legs
    d.drawFastHLine(ox, oy + 3, 34, SSD1306_BLACK);                                        // waistband
}

// Centre text (size `size`) in the right half
static void printRightHalf(Adafruit_SSD1306 &d, const char* text, int size, int y) {
    int w = (int)strlen(text) * 6 * size;
    d.setTextSize(size);
    d.setCursor(64 + (64 - w) / 2, y);
    d.print(text);
}

void renderKidsView(Adafruit_SSD1306 &d, int clothing, int tempShown) {
    d.clearDisplay();
    d.setTextColor(SSD1306_WHITE);
    d.setTextWrap(false);

    const char* word;
    if (clothing == CLOTHES_WARM) {
        drawShirt(d, 8, 2, 1, 1);
        drawShorts(d, 15, 36);
        word = "WARM";
    } else if (clothing == CLOTHES_COOL) {
        drawSweater(d, 4, 6);
        word = "COOL";
    } else {
        drawShirt(d, 2, 13, 5, 4);
        word = "MILD";
    }

    d.drawLine(64, 0, 64, 63, SSD1306_WHITE);

    char num[8];
    snprintf(num, sizeof(num), "%d", tempShown);
    printRightHalf(d, num, strlen(num) > 2 ? 3 : 4, strlen(num) > 2 ? 10 : 6);   // big digits, easy to read
    printRightHalf(d, word, 2, 44);
}
