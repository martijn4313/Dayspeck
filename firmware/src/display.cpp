// Dayspeck Bedside Display — Display Rendering Implementation
// Composite rendering engine for SSD1306 128x64 OLED

#include "display.h"
#include "bitmaps.h"
#include "weather.h"

// Rain animation state
static RainDrop rainDrops[MAX_RAIN_DROPS];
static Splash splashes[MAX_SPLASHES];
static float currentRainIntensity = 5.0f;  // Track current rain intensity
static int currentWindSpeed = 0;  // Track current wind speed
static Gust gusts[MAX_GUSTS];
static Leaf leaves[MAX_LEAVES];



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

// Night overlay — four stars in the strip above the scene (the lit street lamp is part of the night scene)
// Using seed for deterministic but random-looking stars
void applyNightOverlay(Adafruit_SSD1306 &display) {
    // Seed-based pseudo-random using simple hash
    unsigned int seed = 12345; // Fixed seed for deterministic stars
    
    // Draw single white pixels randomly in the sky strip (x: 64-127, y: 0-9)
    for (int i = 0; i < 5; i++) {
        seed = seed * 1103515245 + 12345; // LCG
        int starX = 64 + (seed % 64);
        seed = seed * 1103515245 + 12345;
        int starY = seed % 10;
        if (i < 4) { // Only 4 stars
            display.drawPixel(starX, starY, SSD1306_WHITE);
        }
    }
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

// Procedural snow — scattered pixels + a line of snow on the street
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
    
    // At intensity >= 2: draw a horizontal white line at the pavement edge
    if (intensity >= 2) {
        for (int x = 64; x < 127; x++) {
            display.drawPixel(x, HORIZON_Y, SSD1306_WHITE);
        }
    }
}

// Reset a rain drop to random position at top
// Reset a single rain drop to a new random position above the scene
void resetRainDrop(RainDrop &drop) {
    // Calculate extended spawn zone based on wind for better coverage
    // Average fall frames = HORIZON_Y / avg_speed (roughly 9 frames)
    // x_spawn_extend = abs(wind_drift) * 9
    int xSpawnExtend = 0;  // Simplified for reset
    
    drop.x = RAIN_AREA_X_START + random(RAIN_AREA_X_END - RAIN_AREA_X_START + 1 + xSpawnExtend);
    drop.y = random(-8, -1);  // Start just above top edge
    drop.targetY = HORIZON_Y - random(4);  // Ground level varies by 3 px, always above the text
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
        rainDrops[i].targetY = HORIZON_Y - random(4);
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

// ── Wind animation ───────────────────────────────────────────────────────────
// Curled gusts and, in autumn, tumbling leaves blow from the left edge of the scene to the right.
// Mirrors tools/bitmaptool/wind.py; keep the two in step.

// Vertical wobble of a tumbling leaf over 16 frames (a rough sine, +-3 px)
static const int8_t LEAF_WOBBLE[16] = { 0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1 };

void initWindAnimation() {
    // All slots idle, with staggered start delays so the first frames are not a burst
    for (int i = 0; i < MAX_GUSTS; i++) {
        gusts[i].active = false;
        gusts[i].delay = i * 6;
    }
    for (int i = 0; i < MAX_LEAVES; i++) {
        leaves[i].active = false;
        leaves[i].delay = i * 5;
    }
}

// windKmh: gust speed; leavesOn implies windKmh >= LEAF_MIN_WIND_KMH. A slot beyond the target count
// finishes its run and is not reused.
void updateWindAnimation(int windKmh, bool gustsOn, bool leavesOn) {
    int wantGusts = !gustsOn ? 0 : windKmh >= 65 ? 3 : windKmh >= 50 ? 2 : 1;
    for (int i = 0; i < MAX_GUSTS; i++) {
        Gust &g = gusts[i];
        if (g.active) {
            g.x += g.speed;
            if (g.x > WIND_AREA_X_END) {
                g.active = false;
                g.delay = random(13);
            }
        } else if (i < wantGusts) {
            if (g.delay > 0) {
                g.delay--;
            } else {
                g.length = 8 + random(9);                              // 8-16
                g.x = WIND_AREA_X_START - g.length;                    // starts just off the left edge
                g.y = WIND_AREA_Y_TOP + random(WIND_AREA_Y_SPAN);
                g.curl = 1 + random(2);
                g.speed = 2 + windKmh / 25 + random(2);
                g.active = true;
            }
        }
    }

    int wantLeaves = leavesOn ? min(MAX_LEAVES, 2 + (windKmh - LEAF_MIN_WIND_KMH) / 15) : 0;
    for (int i = 0; i < MAX_LEAVES; i++) {
        Leaf &f = leaves[i];
        if (f.active) {
            f.age++;
            f.x += f.speed;
            if (f.age % 3 == 0) f.yBase++;                             // sinks slowly
            if (f.x > WIND_AREA_X_END || f.yBase > LEAF_MAX_Y) {
                f.active = false;
                f.delay = random(21);
            }
        } else if (i < wantLeaves) {
            if (f.delay > 0) {
                f.delay--;
            } else {
                f.x = LEAF_SPAWN_X;
                f.yBase = LEAF_SPAWN_Y + random(LEAF_SPAWN_SPAN);
                f.age = 0;
                f.speed = 1 + windKmh / 30 + random(2);
                f.phase = random(16);
                f.active = true;
            }
        }
    }
}

void drawWindAnimation(Adafruit_SSD1306 &display) {
    for (int i = 0; i < MAX_GUSTS; i++) {
        const Gust &g = gusts[i];
        if (!g.active) continue;
        int start = max(g.x, WIND_AREA_X_START);
        int end = g.x + g.length;
        if (end >= start) display.drawFastHLine(start, g.y, end - start + 1, SSD1306_WHITE);
        if (end >= WIND_AREA_X_START) display.drawCircleHelper(end, g.y - g.curl, g.curl, 2 | 4, SSD1306_WHITE);
    }

    // The leaf is lying flat, tip up-right, ... as it tumbles
    static const uint8_t* const sprites[4] = { leaf_1_bmp, leaf_2_bmp, leaf_3_bmp, leaf_4_bmp };
    for (int i = 0; i < MAX_LEAVES; i++) {
        const Leaf &f = leaves[i];
        if (!f.active) continue;
        int y = f.yBase + LEAF_WOBBLE[(f.age + f.phase) & 15];
        const uint8_t* sprite = sprites[((f.age >> 1) + f.phase) & 3];
#ifdef KIDS_MODE
        // Inverted: visible on the dark background and on the filled pictures, and the digits are not erased
        display.drawBitmap(f.x, y, sprite, LEAF_1_BMP_W, LEAF_1_BMP_H, SSD1306_INVERSE);
#else
        int hx = max(f.x - 1, WIND_AREA_X_START);
        display.fillRect(hx, y - 1, f.x + 6 - hx, 7, SSD1306_BLACK);    // black halo, never over the divider
        display.drawBitmap(f.x, y, sprite, LEAF_1_BMP_W, LEAF_1_BMP_H, SSD1306_WHITE);
#endif
    }
}

// Render the scene card — composite render of the right half above the text
void renderSkylineCard(Adafruit_SSD1306 &display, bool isNight, int weatherCondition, int intensity, int windSpeed, const char *tempStr, char trendArrow) {
    // Layer 1: blit the village at (SCENE_X, SCENE_Y): the night version has the street lamp lit
    display.drawBitmap(SCENE_X, SCENE_Y, isNight ? scene_night_bmp : scene_day_bmp, SCENE_DAY_BMP_W, SCENE_DAY_BMP_H, SSD1306_WHITE);
    
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
    } else {
        drawWindAnimation(display);   // gusts and leaves, whatever is active (nothing when it is calm)
    }
    
    // Layer 4: draw tempStr text at (TEMP_X, TEMP_Y) with black background box
    // First draw black box behind text for readability
    display.fillRect(TEMP_X - 1, TEMP_Y - 1, 26, 10, SSD1306_BLACK);
    
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

// Render the street band — wind and precipitation text on the empty street at the bottom of the scene
// (the weather itself is shown by the sun or moon, the rain, the snow and the wind animation)
void renderBottomCard(Adafruit_SSD1306 &display, int weatherCondition, int windSpeed, float precipMm, bool isNight) {
    (void)weatherCondition;
    (void)isNight;
    char buf[12];
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    snprintf(buf, sizeof(buf), "%dkm/h", windSpeed);
    display.setCursor(WIND_TEXT_X, WIND_TEXT_Y);
    display.print(buf);
    snprintf(buf, sizeof(buf), "%.1fmm", precipMm);
    display.setCursor(WIND_TEXT_X, PRECIP_TEXT_Y);
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
    
    // Right half: the scene with temperature, sun or moon and weather effects
    renderSkylineCard(display, isNight, weatherCondition, intensity, windSpeed, tempStr, trendArrow);
    
    // Wind and precipitation text on the street band of the scene
    renderBottomCard(display, weatherCondition, windSpeed, precipMm, isNight);
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

// Render the next hours as a strip of columns. A label column on the left names the rows:
// h (hour), degree C, mm (rain amount, solid bar), % (chance of rain, dotted line), kmh (gusts).
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

    const int labelW = 20, colW = 18, barBottom = 44, barMax = 24;

    // Row labels
    display.setCursor(0, 0);
    display.print("h");
    display.drawRect(0, 10, 3, 3, SSD1306_WHITE);             // degree sign
    display.setCursor(5, 10);
    display.print("C");
    display.setCursor(0, 21);
    display.print("mm");
    display.fillRect(14, 22, 4, 6, SSD1306_WHITE);            // sample bar
    display.setCursor(0, 32);
    display.print("%");
    for (int dx = 8; dx <= 16; dx += 2) {
        display.drawPixel(dx, 35, SSD1306_WHITE);             // sample dotted line
    }
    display.setCursor(0, 47);
    display.print("kmh");

    size_t cols = count < 6 ? count : 6;
    char buf[20];
    for (size_t i = 0; i < cols; i++) {
        const HourSlice& h = hours[i];
        int x = labelW + (int)i * colW;

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
            display.fillRect(x + 5, barBottom - barH + 1, 8, barH, SSD1306_WHITE);
        }
        if (h.rainProb != 255 && h.rainProb > 0) {
            int y = barBottom - (int)h.rainProb * barMax / 100;
            for (int dx = 2; dx <= 16; dx += 2) {
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

// ---- Kids variant: what to wear, now and later -------------------------------------------------
// Layout: two 56 px wide halves (now at x 0, later at x 72) and a 16 px strip between them with an
// arrow and the time-of-day symbol of "later".

#define KIDS_NOW_X    0
#define KIDS_LATER_X  72
#define KIDS_HALF_W   56

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

// Shorts silhouette, 34 wide and `h` tall
static void drawShorts(Adafruit_SSD1306 &d, int ox, int oy, int h) {
    d.fillRect(ox, oy, 34, h, SSD1306_WHITE);
    d.fillTriangle(ox + 17, oy + h / 2 - 1, ox + 13, oy + h, ox + 21, oy + h, SSD1306_BLACK);   // between the legs
    d.drawFastHLine(ox, oy + 3, 34, SSD1306_BLACK);                                          // waistband
}

// Long-sleeved garment, 56 wide: body `len` tall, sleeves ending about 10 px above the hem
static void drawLongSleeved(Adafruit_SSD1306 &d, int ox, int oy, int len) {
    d.fillRect(ox + 14, oy + 1, 28, len, SSD1306_WHITE);
    fillQuad(d, ox + 15, oy + 1, ox + 6, oy + 6, ox + 1, oy + len - 10, ox + 9, oy + len - 8);
    fillQuad(d, ox + 41, oy + 1, ox + 50, oy + 6, ox + 55, oy + len - 10, ox + 47, oy + len - 8);
}

// Sweater: V neck, ribbed hem and cuffs, 56x52
static void drawSweater(Adafruit_SSD1306 &d, int ox, int oy) {
    drawLongSleeved(d, ox, oy, 50);
    d.fillTriangle(ox + 22, oy, ox + 34, oy, ox + 28, oy + 8, SSD1306_BLACK);     // neck
    d.drawFastHLine(ox + 14, oy + 45, 28, SSD1306_BLACK);                          // hem
    d.drawLine(ox + 1, oy + 36, ox + 8, oy + 38, SSD1306_BLACK);                   // cuffs
    d.drawLine(ox + 55, oy + 36, ox + 48, oy + 38, SSD1306_BLACK);
}

// Sun cap with the peak to the right, centred on cx, top at oy (about 22x9)
static void drawCap(Adafruit_SSD1306 &d, int cx, int oy) {
    d.fillCircle(cx, oy + 8, 8, SSD1306_WHITE);
    d.fillRect(cx - 9, oy + 9, 19, 8, SSD1306_BLACK);           // keep the top half: the crown
    d.fillRect(cx, oy + 7, 14, 2, SSD1306_WHITE);                // peak
    d.drawPixel(cx, oy, SSD1306_BLACK);                          // button on top
}

// Knitted hat with a pompom, centred on cx, top at oy (about 22x15)
static void drawBeanie(Adafruit_SSD1306 &d, int cx, int oy) {
    d.fillCircle(cx, oy + 13, 10, SSD1306_WHITE);
    d.fillRect(cx - 11, oy + 14, 23, 11, SSD1306_BLACK);        // keep the dome
    d.fillRect(cx - 11, oy + 10, 23, 5, SSD1306_WHITE);          // turned-up rim
    d.drawFastHLine(cx - 11, oy + 12, 23, SSD1306_BLACK);
    d.fillCircle(cx, oy + 2, 3, SSD1306_WHITE);                  // pompom
}

// Hooded rain coat with buttons, and rain boots, 56x64
static void drawRainOutfit(Adafruit_SSD1306 &d, int ox, int oy) {
    drawLongSleeved(d, ox, oy + 12, 40);
    d.fillCircle(ox + 28, oy + 8, 9, SSD1306_WHITE);             // hood
    d.fillCircle(ox + 28, oy + 10, 5, SSD1306_BLACK);            // face opening
    d.drawFastVLine(ox + 28, oy + 18, 35, SSD1306_BLACK);        // front opening
    for (int y = 22; y <= 44; y += 8) d.fillRect(ox + 31, oy + y, 2, 2, SSD1306_BLACK);   // buttons
    d.fillRect(ox + 15, oy + 55, 9, 9, SSD1306_WHITE);           // boots, toes outwards
    d.fillRect(ox + 10, oy + 60, 6, 4, SSD1306_WHITE);
    d.fillRect(ox + 32, oy + 55, 9, 9, SSD1306_WHITE);
    d.fillRect(ox + 40, oy + 60, 6, 4, SSD1306_WHITE);
    d.drawFastHLine(ox + 15, oy + 57, 9, SSD1306_BLACK);
    d.drawFastHLine(ox + 32, oy + 57, 9, SSD1306_BLACK);
}

// Padded winter coat with a zip, under a knitted hat, 56x64; `freezing` adds a scarf and mittens
static void drawWinterOutfit(Adafruit_SSD1306 &d, int ox, int oy, bool freezing) {
    drawBeanie(d, ox + 28, oy);
    drawLongSleeved(d, ox, oy + 16, 47);
    for (int y = 27; y <= 54; y += 9) d.drawFastHLine(ox, oy + y, 56, SSD1306_BLACK);   // padding
    d.drawFastVLine(ox + 28, oy + 17, 47, SSD1306_BLACK);                               // zip
    if (!freezing) return;
    d.fillRect(ox + 15, oy + 16, 26, 8, SSD1306_BLACK);          // scarf round the neck...
    d.fillRect(ox + 16, oy + 17, 24, 6, SSD1306_WHITE);
    d.fillRect(ox + 31, oy + 22, 8, 17, SSD1306_BLACK);          // ...and its hanging end
    d.fillRect(ox + 32, oy + 23, 6, 15, SSD1306_WHITE);
    d.drawFastHLine(ox + 32, oy + 30, 6, SSD1306_BLACK);
    d.drawFastHLine(ox + 32, oy + 34, 6, SSD1306_BLACK);
    for (int side = 0; side < 2; side++) {                        // mittens at the cuffs
        int cx = side ? ox + 51 : ox + 5;
        d.fillCircle(cx, oy + 57, 5, SSD1306_BLACK);
        d.fillCircle(cx, oy + 57, 4, SSD1306_WHITE);
        d.fillRect(side ? cx - 6 : cx + 3, oy + 53, 3, 3, SSD1306_WHITE);   // thumb
    }
}

// One outfit in a 56x64 half
static void drawOutfit(Adafruit_SSD1306 &d, int ox, int outfit) {
    switch (outfit) {
    case OUTFIT_HOT:
        drawCap(d, ox + 26, 0);
        drawShirt(d, ox + 4, 11, 1, 1);
        drawShorts(d, ox + 11, 43, 21);
        break;
    case OUTFIT_WARM:
        drawShirt(d, ox + 4, 2, 1, 1);
        drawShorts(d, ox + 11, 36, 24);
        break;
    case OUTFIT_COOL:
        drawSweater(d, ox, 6);
        break;
    case OUTFIT_RAIN:
        drawRainOutfit(d, ox, 0);
        break;
    case OUTFIT_COLD:
        drawWinterOutfit(d, ox, 0, false);
        break;
    case OUTFIT_FREEZING:
        drawWinterOutfit(d, ox, 0, true);
        break;
    default:   // OUTFIT_MILD
        drawShirt(d, ox, 14, 7, 6);
        break;
    }
}

// Unknown "later": a big question mark
static void drawUnknown(Adafruit_SSD1306 &d, int ox) {
    d.setTextSize(4);
    d.setCursor(ox + (KIDS_HALF_W - 22) / 2, 18);
    d.print('?');
}

// The strip between the halves: an arrow from now to later, and below it when "later" is
static void drawKidsMiddle(Adafruit_SSD1306 &d, int timeOfDaySymbol) {
    const int cx = 64;
    d.fillRect(57, 9, 7, 3, SSD1306_WHITE);                                    // arrow shaft
    d.fillTriangle(63, 5, 63, 15, 69, 10, SSD1306_WHITE);                      // arrow head

    const int y = 44;   // symbols live in a 14x14 box from (57, y - 7)
    switch (timeOfDaySymbol) {
    case KIDS_TIME_MORNING:
    case KIDS_TIME_EVENING: {
        // Half a sun on the horizon with rays, and beside it an arrow: up in the morning, down in the evening
        const int sx = cx - 3;
        d.fillCircle(sx, y + 4, 4, SSD1306_WHITE);
        d.fillRect(sx - 5, y + 5, 11, 5, SSD1306_BLACK);
        d.drawFastHLine(57, y + 5, 14, SSD1306_WHITE);
        d.drawPixel(sx - 6, y + 1, SSD1306_WHITE);
        d.drawPixel(sx + 5, y - 1, SSD1306_WHITE);
        d.drawPixel(sx - 4, y - 2, SSD1306_WHITE);
        d.drawPixel(sx, y - 3, SSD1306_WHITE);
        d.drawFastVLine(cx + 5, y - 5, 8, SSD1306_WHITE);
        if (timeOfDaySymbol == KIDS_TIME_MORNING) d.fillTriangle(cx + 5, y - 8, cx + 2, y - 5, cx + 8, y - 5, SSD1306_WHITE);
        else                                      d.fillTriangle(cx + 2, y + 1, cx + 8, y + 1, cx + 5, y + 4, SSD1306_WHITE);
        break;
    }
    case KIDS_TIME_AFTERNOON: {
        // Full sun, high in the sky
        d.fillCircle(cx, y, 3, SSD1306_WHITE);
        static const int8_t ray[8][2] = { {6,0}, {4,4}, {0,6}, {-4,4}, {-6,0}, {-4,-4}, {0,-6}, {4,-4} };
        for (int i = 0; i < 8; i++) d.drawPixel(cx + ray[i][0], y + ray[i][1], SSD1306_WHITE);
        break;
    }
    case KIDS_TIME_NIGHT:
        d.fillCircle(cx, y, 6, SSD1306_WHITE);
        d.fillCircle(cx + 3, y - 2, 5, SSD1306_BLACK);
        break;
    default: {
        // Tomorrow: a bed, "after sleeping"
        d.fillRect(57, y - 4, 2, 11, SSD1306_WHITE);                           // headboard
        d.fillRect(57, y + 2, 14, 3, SSD1306_WHITE);                           // mattress
        d.fillRect(69, y, 2, 7, SSD1306_WHITE);                                // foot end
        d.fillRect(60, y - 1, 4, 3, SSD1306_WHITE);                            // pillow
        d.setTextSize(1);
        d.setCursor(65, y - 11);
        d.print('z');
        break;
    }
    }
}

void renderKidsView(Adafruit_SSD1306 &d, int outfitNow, int outfitLater, int timeOfDaySymbol) {
    d.clearDisplay();
    d.setTextColor(SSD1306_WHITE);
    d.setTextWrap(false);
    drawOutfit(d, KIDS_NOW_X, outfitNow);
    if (outfitLater >= 0) drawOutfit(d, KIDS_LATER_X, outfitLater);
    else                  drawUnknown(d, KIDS_LATER_X);
    drawKidsMiddle(d, timeOfDaySymbol);
    drawWindAnimation(d);    // autumn leaves, when they blow
}

// ---- Kids weather pictures (56x44 box) ------------------------------------------------------------

// Cloud silhouette in a 40x26 box; `grow` fattens it (drawn in black first to cut it out of what is behind)
static void drawCloud(Adafruit_SSD1306 &d, int ox, int oy, int grow, int color) {
    d.fillCircle(ox + 10, oy + 16, 9 + grow, color);
    d.fillCircle(ox + 20, oy + 11, 11 + grow, color);
    d.fillCircle(ox + 31, oy + 17, 8 + grow, color);
    d.fillRect(ox + 10 - grow, oy + 16, 22 + 2 * grow, 9 + grow, color);
}

static void drawSun(Adafruit_SSD1306 &d, int cx, int cy, int r) {
    d.fillCircle(cx, cy, r, SSD1306_WHITE);
    static const int8_t dir[8][2] = { {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,-1}, {1,-1} };
    for (int i = 0; i < 8; i++) {
        int dx = dir[i][0], dy = dir[i][1];
        int a = r + 3, b = r + 3 + (r > 14 ? 7 : 4);
        if (dx && dy) { a = (a * 7) / 10; b = (b * 7) / 10; }
        for (int o = -1; o <= 0; o++) {   // 2 px thick
            d.drawLine(cx + dx * a + o, cy + dy * a, cx + dx * b + o, cy + dy * b, SSD1306_WHITE);
            d.drawLine(cx + dx * a, cy + dy * a + o, cx + dx * b, cy + dy * b + o, SSD1306_WHITE);
        }
    }
}

static void drawMoon(Adafruit_SSD1306 &d, int cx, int cy, int r) {
    d.fillCircle(cx, cy, r, SSD1306_WHITE);
    d.fillCircle(cx + r * 2 / 5, cy - r / 4, r * 5 / 6, SSD1306_BLACK);   // bite out of it: a crescent
}

static void drawRainDrops(Adafruit_SSD1306 &d, int ox, int oy) {
    for (int i = 0; i < 4; i++) {
        int x = ox + 4 + i * 9, y = oy + (i % 2) * 4;
        d.drawLine(x, y, x - 3, y + 7, SSD1306_WHITE);
        d.drawLine(x + 1, y, x - 2, y + 7, SSD1306_WHITE);
    }
}

static void drawSnowFlakes(Adafruit_SSD1306 &d, int ox, int oy) {
    for (int i = 0; i < 4; i++) {
        int x = ox + 6 + i * 9, y = oy + 2 + (i % 2) * 5;
        d.fillRect(x - 1, y - 1, 3, 3, SSD1306_WHITE);
        d.drawPixel(x, y - 3, SSD1306_WHITE);
        d.drawPixel(x, y + 3, SSD1306_WHITE);
        d.drawPixel(x - 3, y, SSD1306_WHITE);
        d.drawPixel(x + 3, y, SSD1306_WHITE);
    }
}

// Lightning bolt, about 16x22
static void drawLightning(Adafruit_SSD1306 &d, int ox, int oy) {
    d.fillTriangle(ox + 7, oy, ox + 16, oy, ox + 4, oy + 12, SSD1306_WHITE);
    d.fillTriangle(ox + 2, oy + 10, ox + 13, oy + 10, ox + 1, oy + 22, SSD1306_WHITE);
    d.fillTriangle(ox + 13, oy + 10, ox + 7, oy + 16, ox + 2, oy + 10, SSD1306_WHITE);
}

// Wind: three streaks that curl at the end
static void drawWindStreaks(Adafruit_SSD1306 &d, int ox, int oy) {
    static const int8_t rows[3][3] = { {0, 8, 36}, {7, 0, 40}, {14, 6, 30} };   // y/2, x start, x end
    for (int i = 0; i < 3; i++) {
        int y = oy + 8 + rows[i][0] * 2, x0 = ox + rows[i][1], x1 = ox + rows[i][2];
        for (int o = 0; o < 2; o++) d.drawFastHLine(x0, y + o, x1 - x0, SSD1306_WHITE);
        for (int r = 5; r <= 6; r++) d.drawCircleHelper(x1, y - 5, r, 2 | 4, SSD1306_WHITE);   // curl up and back
    }
}

// One weather picture in the 56x44 box at (ox, 0)
static void drawKidsWeather(Adafruit_SSD1306 &d, int ox, int weather, bool night) {
    switch (weather) {
    case KIDS_WEATHER_PARTLY:
        if (night) drawMoon(d, ox + 20, 13, 12); else drawSun(d, ox + 20, 14, 7);
        drawCloud(d, ox + 12, 16, 2, SSD1306_BLACK);
        drawCloud(d, ox + 12, 16, 0, SSD1306_WHITE);
        break;
    case KIDS_WEATHER_CLOUDY:
        drawCloud(d, ox + 8, 8, 4, SSD1306_WHITE);
        break;
    case KIDS_WEATHER_RAIN:
        drawCloud(d, ox + 8, 2, 2, SSD1306_WHITE);
        drawRainDrops(d, ox + 8, 33);
        break;
    case KIDS_WEATHER_STORM:
        drawCloud(d, ox + 8, 0, 2, SSD1306_WHITE);
        drawLightning(d, ox + 20, 22);
        break;
    case KIDS_WEATHER_SNOW:
        drawCloud(d, ox + 8, 2, 2, SSD1306_WHITE);
        drawSnowFlakes(d, ox + 6, 33);
        break;
    case KIDS_WEATHER_WIND:
        drawWindStreaks(d, ox + 6, 0);
        break;
    default:
        if (night) {
            drawMoon(d, ox + 26, 22, 18);
            d.drawPixel(ox + 49, 6, SSD1306_WHITE);
            d.fillRect(ox + 50, 18, 2, 2, SSD1306_WHITE);
            d.fillRect(ox + 4, 4, 2, 2, SSD1306_WHITE);
        } else {
            drawSun(d, ox + 28, 22, 10);
        }
    }
}

// Temperature under a picture: size 2 digits, centred in the half
static void drawKidsTemp(Adafruit_SSD1306 &d, int ox, int temp) {
    char num[8];
    snprintf(num, sizeof(num), "%d", temp);
    int w = (int)strlen(num) * 12 - 2;
    d.setTextSize(2);
    d.setCursor(ox + (KIDS_HALF_W - w) / 2, 48);
    d.print(num);
}

void renderKidsWeatherView(Adafruit_SSD1306 &d, int weatherNow, bool nightNow, int tempNow,
                           bool laterValid, int weatherLater, bool nightLater, int tempLater, int timeOfDaySymbol) {
    d.clearDisplay();
    d.setTextColor(SSD1306_WHITE);
    d.setTextWrap(false);
    drawKidsWeather(d, KIDS_NOW_X, weatherNow, nightNow);
    drawKidsTemp(d, KIDS_NOW_X, tempNow);
    if (laterValid) {
        drawKidsWeather(d, KIDS_LATER_X, weatherLater, nightLater);
        drawKidsTemp(d, KIDS_LATER_X, tempLater);
    } else {
        drawUnknown(d, KIDS_LATER_X);
    }
    drawKidsMiddle(d, timeOfDaySymbol);
    drawWindAnimation(d);    // autumn leaves, when they blow
}

// Clock screen: HH:MM in large type, weekday and date below, year at the bottom
void renderClockView(Adafruit_SSD1306 &display, bool timeValid, int hour, int minute, bool colon,
                     int weekday, int day, int month, int year) {
    static const char* const DAYS[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char* const MONTHS[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    if (!timeValid) {
        display.setTextSize(1);
        display.setCursor(22, 22);
        display.print("Time not set yet");
        display.setCursor(10, 36);
        display.print("waiting for WiFi...");
        return;
    }

    char buf[24];
    display.setTextSize(3);                         // 18 px per character
    snprintf(buf, sizeof(buf), colon ? "%02d:%02d" : "%02d %02d", hour % 24, minute % 60);
    display.setCursor((128 - ((int)strlen(buf) * 18 - 3)) / 2, 6);
    display.print(buf);

    display.setTextSize(2);                         // 12 px per character
    snprintf(buf, sizeof(buf), "%s %d %s", DAYS[weekday % 7], day, MONTHS[(month + 11) % 12]);
    int w = (int)strlen(buf) * 12 - 2;
    display.setCursor((128 - w) / 2, 37);
    display.print(buf);

    display.setTextSize(1);
    snprintf(buf, sizeof(buf), "%d", year);
    display.setCursor((128 - ((int)strlen(buf) * 6 - 1)) / 2, 56);
    display.print(buf);
}

