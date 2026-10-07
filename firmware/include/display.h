// Dayspeck Bedside Display — Display Rendering Declarations
// Composite rendering engine for SSD1306 128x64 OLED

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "motologic.h"

// Ride badge types
#define BADGE_CHECK   'C'
#define BADGE_WARN    '!'
#define BADGE_X       'X'

// Display regions
#define BADGE_X0      0
#define BADGE_Y0      0
#define BADGE_X1      63
#define BADGE_Y1      63

// The 64x50 village scene fills the right half below the temperature row (y = 10 .. 59); its street
// band at the bottom is empty so that the wind and precipitation text sits on it
#define SCENE_X       64
#define SCENE_Y       10

#define SUN_X           116
#define SUN_Y           0     // the 10x10 sun / moon sit in the strip above the scene
#define MOON_X          116
#define MOON_Y          0
#define TEMP_X          65
#define TEMP_Y          1
#define ARROW_X         91    // right of a 4 character temperature such as "-10C"
#define ARROW_Y         1
#define WIND_TEXT_X     82    // right of the street lamp
#define WIND_TEXT_Y     49
#define PRECIP_TEXT_Y   57

// Rain animation constants
#define HORIZON_Y            46      // Pavement edge of the scene: rain splashes here, above the text
#define RAIN_AREA_X_START    64      // Rain region left boundary
#define RAIN_AREA_X_END      127     // Rain region right boundary
#define MAX_RAIN_DROPS       16      // Increased from 8 for better visual density
#define MAX_SPLASHES         8       // Increased from 4 for better splash effects
#define MAX_RAIN_DROP_SPEED  6       // Maximum drop speed (pixels per frame)
#define RAIN_FRAME_INTERVAL  66      // 15 FPS in milliseconds
#define RAIN_INTENSITY_MIN   0       // Minimum rain intensity (mm/h)
#define RAIN_INTENSITY_MAX   20      // Maximum rain intensity (mm/h)

// Wind animation constants (gusts and, in autumn, tumbling leaves; mirrored in tools/bitmaptool/constants.py)
#define MAX_GUSTS            3
#define MAX_LEAVES           4
// Where gusts and leaves blow (mirrored in tools/bitmaptool/constants.py and wind.py)
struct WindArea {
    int16_t xStart, xEnd;          // gusts and leaves stay between these columns
    int16_t gustYTop, gustYSpan;   // gust lanes
    int16_t leafX0;                // a new leaf enters here
    int16_t leafY0, leafYSpan;     // ... at y = leafY0 .. leafY0 + leafYSpan - 1
    int16_t leafMaxY;              // a leaf that sinks past this base line has landed
    bool    inverseLeaves;         // drawn inverted (over filled pictures) instead of with a black outline
};
// The ride screen: the right half, right of the divider at x=64 (the ride badge is never touched)
extern const WindArea WIND_AREA_RIDE;
// The kids screens: no gusts, leaves across the whole screen above the temperature digits (y >= 45)
extern const WindArea WIND_AREA_KIDS;

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

struct Gust {
    int x;              // left end of the streak; the curl sits at x + length
    int y;
    int length;         // 8-16
    int speed;          // px per frame
    int curl;           // radius of the curl at the leading end (1 or 2)
    int delay;          // frames to wait before the next gust in this slot
    bool active;
};

struct Leaf {
    int x;
    int yBase;          // sinks slowly; the drawn y adds a wobble
    int age;            // frames since it appeared (drives wobble and spin)
    int speed;          // px per frame
    int phase;          // 0-15, so leaves do not move in step
    int delay;
    bool active;
};

// Wind animation: gusts when it is windy, leaves when leavesOn (see leavesBlowing() in motologic).
// Same 15 FPS tick as the rain. initWindAnimation() also stops it.
void initWindAnimation(const WindArea &area = WIND_AREA_RIDE);   // also picks where it blows
void updateWindAnimation(int windKmh, bool gustsOn, bool leavesOn);
void drawWindAnimation(Adafruit_SSD1306 &display);

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

// Composite renderers. They draw into the buffer only; the caller flushes with display.display().
void renderSkylineCard(Adafruit_SSD1306 &display, bool isNight, int weatherCondition, int intensity, int windSpeed, const char *tempStr, char trendArrow);
void renderBottomCard(Adafruit_SSD1306 &display, int weatherCondition, int windSpeed, float precipMm, bool isNight);
// bestDay: column to highlight (inverse header), -1 for none
void renderWeeklyMatrix(Adafruit_SSD1306 &display, const char weekAM[7], const char weekPM[7], uint8_t startDow, int bestDay);
// Next hours (up to 6 columns): hour, temperature, rain bar (mm) with chance-of-rain tick, gusts (km/h).
// firstHour = local hour of hours[0]. Footer: best time to leave and the time of the last update (-1 = unknown).
void renderHourlyView(Adafruit_SSD1306 &display, const HourSlice* hours, size_t count, int firstHour,
                      bool hasLeave, int leaveHour, bool leaveNow, int updHour, int updMinute);
// Clock screen: time (blinking colon) and date, centred. timeValid false shows a waiting message.
// weekday 0 = Sunday, month 1-12.
void renderClockView(Adafruit_SSD1306 &display, bool timeValid, int hour, int minute, bool colon,
                     int weekday, int day, int month, int year);
// Small status marks on the primary view: "TMRW" tag and WiFi signal bars (bars 0-4, -1 = not connected)
void renderStatusMarks(Adafruit_SSD1306 &display, bool showTomorrow, int wifiBars);
void renderPrimaryView(Adafruit_SSD1306 &display, char badgeType, bool isNight, int weatherCondition, int intensity, int windSpeed, const char *tempStr, char trendArrow, float precipMm);
// Kids variant: one column of the day strip
struct KidsColumn {
    int  part;      // KIDS_PART_*
    bool valid;     // false: no forecast for it (drawn as a question mark)
    int  outfit;    // OUTFIT_* (clothes screen)
    int  weather;   // KIDS_WEATHER_* (weather screen)
    int  light;     // KIDS_LIGHT_*: sun, setting sun or moon
    int  temp;      // the part's temperature, as shown (weather screen): see kidsPartOutlook
    int  precip;    // KIDS_PRECIP_*: how hard it rains or snows (the falling drops or flakes)
    bool umbrella;  // some rain: an umbrella with the outfit (clothes screen, village)
    // The weather screen's time-lapse: the picture (KIDS_WEATHER_*) and rain or snow (KIDS_PRECIP_*) of each hour
    uint8_t hours;
    uint8_t hourWeather[KIDS_MAX_PART_HOURS];
    uint8_t hourPrecip[KIDS_MAX_PART_HOURS];
};
// The time-lapse plays a column's hours in `loopMs` (the time the screen is shown), each hour at least this long
#define KIDS_TIMELAPSE_MIN_HOUR_MS  1500UL
#define KIDS_TIMELAPSE_DEFAULT_MS  12000UL   // a screen that stays: it loops in this time
// Kids variant: the next parts of the day in up to three columns, outfits (weather = false) or weather
// pictures with the part's temperature (the night column: a bed instead of an outfit). nowColumn gets dots
// underneath (-1 = none); nightBefore is the column a night lies before (a dotted line with a bed; -1 or 0 =
// none). The weather screen plays each column's hours as a seamless time-lapse in loopMs, elapsedMs after the
// screen appeared: clouds glide in and out, rain and snow fall from their clouds onto the numbers.
void renderKidsDayStrip(Adafruit_SSD1306 &display, const KidsColumn* cols, size_t count, int nowColumn,
                        int nightBefore, bool weather, unsigned long elapsedMs, unsigned long loopMs);
// Whether a column's weather picture moves (rain, snow, or weather that changes during its hours)
bool kidsColumnAnimates(const KidsColumn &c);
// Kids variant: the countdown screen. The picture of the event (a cake with a candle per year and the initial,
// a pumpkin, Sinterklaas' mitre and staff, a Christmas tree), the number of sleeps and, up to ten, as many
// beds to count. On the day itself the picture with falling confetti (timeMs moves it).
void renderKidsCountdown(Adafruit_SSD1306 &display, const KidsCountdown &c, unsigned long timeMs);
// The weather report blowing away in the wind, frame `frame` (15 per second) from the moment it lets go:
// the letters peel off from the right, speed up, lift and wobble, with a few gust streaks. strength 1 is
// about 65 km/h gusts (faster above). Returns false once everything has left the screen.
bool renderReportBlowFrame(Adafruit_SSD1306 &display, const char (*lines)[REPORT_COLS + 1], size_t count, int frame,
                           float strength);
// Kids home screen: what to wear right now (`now`, see kidsNowOutlook; at night a sleeping moon) on the left,
// the village of the ride screen on the right, and on its street the current part of the day
void renderKidsVillageView(Adafruit_SSD1306 &display, const KidsColumn &now, bool hasOutfit,
                           bool isNight, int weatherCondition, int intensity, int windSpeed, const char *tempStr,
                           char trendArrow);
// Weather report: lines of text (REPORT_COLS characters at most, REPORT_DEGREE for the degree sign), centred
// vertically
void renderReportView(Adafruit_SSD1306 &display, const char (*lines)[REPORT_COLS + 1], size_t count);
// Setup access point instructions (full width): network name, password and IP address
void renderApInfoView(Adafruit_SSD1306 &display, const char* ssid, const char* password, const char* ip);
void renderLoadingView(Adafruit_SSD1306 &display, const char* line1, const char* line2, unsigned long timeMs);

#endif // DISPLAY_H
