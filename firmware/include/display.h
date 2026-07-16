// MotoWeather Bedside Display — Display Rendering Declarations
// Composite rendering engine for SSD1306 128x64 OLED

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

// Ride badge types
#define BADGE_CHECK   'C'
#define BADGE_WARN    '!'
#define BADGE_X       'X'

// Display regions
#define BADGE_X0      0
#define BADGE_Y0      0
#define BADGE_X1      63
#define BADGE_Y1      63

#define SKYLINE_X     64
#define SKYLINE_Y     12
#define SKY_YMAX      29      // 30 rows for town (y=12 to y=41)
#define BOTTOM_CARD_Y 42      // Divider moved down to give 30px height

// Coordinate constants (adjust after finalising skyline art)
#define STREETLIGHT_BX  120
#define STREETLIGHT_BY  32    // Moved down with divider
#define CHURCH_ROOF_Y   41      // Bottom of 30px town area (y=12 to y=41)
#define SUN_X           116
#define SUN_Y           2
#define MOON_X          116
#define MOON_Y          2
#define TEMP_X          65
#define TEMP_Y          1
#define ARROW_X         87
#define ARROW_Y         1

// Rain animation constants
#define HORIZON_Y            41      // Bottom of town area (y=12 to y=41)
#define RAIN_AREA_X_START    64      // Rain region left boundary
#define RAIN_AREA_X_END      127     // Rain region right boundary
#define MAX_RAIN_DROPS       16      // Increased from 8 for better visual density
#define MAX_SPLASHES         8       // Increased from 4 for better splash effects
#define MAX_RAIN_DROP_SPEED  6       // Maximum drop speed (pixels per frame)
#define RAIN_FRAME_INTERVAL  66      // 15 FPS in milliseconds
#define RAIN_INTENSITY_MIN   0       // Minimum rain intensity (mm/h)
#define RAIN_INTENSITY_MAX   20      // Maximum rain intensity (mm/h)

// Rain drop and splash structures

#ifdef DISPLAY_STATUS_DEBUG
/**
 * Draw debug status message on screen
 * Shows centered text overlay during loading/error states
 */
void drawDebugStatus(Adafruit_SSD1306 &display, const char* message);
#else
// Compile out completely when debug is disabled - zero overhead
#define drawDebugStatus(display, message) ((void)0)
#endif
struct RainDrop {
    int x;              // Current x position (64-127)
    int y;              // Current y position (-6 to 41)
    int targetY;        // Y position where drop hits ground
    int speed;          // Fall speed (3-6 pixels per frame)
    int spriteIdx;      // Which sprite variant (0-3)
    bool active;        // Is this drop currently falling?
};

struct Splash {
    int x;              // Splash x position
    int y;              // Splash y position (at horizon)
    int frameCounter;   // Frames remaining for splash animation
    bool active;        // Is this splash currently visible?
    int spriteIdx;      // Which splash sprite variant
};

// Rain animation functions
void initRainAnimation();
void updateRainAnimation(int windSpeed, float rainIntensity);
void drawRainAnimation(Adafruit_SSD1306 &display);
void resetRainDrop(RainDrop &drop);

// Procedural drawing functions
void drawGiantBadge(Adafruit_SSD1306 &display, char type);
void applyNightOverlay(Adafruit_SSD1306 &display);
void drawProceduralRain(Adafruit_SSD1306 &display, int intensity);
void drawProceduralSnow(Adafruit_SSD1306 &display, int intensity);
void drawProceduralWind(Adafruit_SSD1306 &display, int speed);

// Composite renderers
void renderSkylineCard(Adafruit_SSD1306 &display, bool isNight, int weatherCondition, int intensity, int windSpeed, const char *tempStr, char trendArrow);
void renderBottomCard(Adafruit_SSD1306 &display, int weatherCondition, int windSpeed, float precipMm);
void renderWeeklyMatrix(Adafruit_SSD1306 &display, const char weekAM[7], const char weekPM[7]);
void renderPrimaryView(Adafruit_SSD1306 &display, char badgeType, bool isNight, int weatherCondition, int intensity, int windSpeed, const char *tempStr, char trendArrow, float precipMm);
void renderLoadingView(Adafruit_SSD1306 &display, const char* line1, const char* line2, unsigned long timeMs);

#endif // DISPLAY_H
