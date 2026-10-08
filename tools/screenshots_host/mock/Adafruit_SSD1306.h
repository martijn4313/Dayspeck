// Host stand-in for the SSD1306 driver: a 128x64 buffer under the real Adafruit GFX drawing code
#ifndef HOST_ADAFRUIT_SSD1306_H
#define HOST_ADAFRUIT_SSD1306_H

#include "Adafruit_GFX.h"

#define SSD1306_BLACK 0
#define SSD1306_WHITE 1
#define SSD1306_INVERSE 2
#define SSD1306_DISPLAYOFF 0xAE
#define SSD1306_DISPLAYON 0xAF
#define SSD1306_SETCONTRAST 0x81

class Adafruit_SSD1306 : public Adafruit_GFX {
public:
    uint8_t px[64][128];
    Adafruit_SSD1306() : Adafruit_GFX(128, 64) { clearDisplay(); }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        if (color == SSD1306_INVERSE) px[y][x] ^= 1;
        else px[y][x] = color ? 1 : 0;
    }
    bool getPixel(int16_t x, int16_t y) { return x >= 0 && x < 128 && y >= 0 && y < 64 && px[y][x]; }
    void clearDisplay() { memset(px, 0, sizeof(px)); }
    void display() {}
    uint8_t *getBuffer() { return &px[0][0]; }   // showFrame() fingerprints it
    void ssd1306_command(uint8_t) {}
};

#endif
