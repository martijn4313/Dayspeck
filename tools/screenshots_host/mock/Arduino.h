// Host stand-in for the parts of the Arduino core that the firmware's drawing code and Adafruit GFX use
#ifndef HOST_ARDUINO_H
#define HOST_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "Print.h"

#define PROGMEM
#define PI 3.14159265358979f
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_ptr(addr) (*(const void *const *)(addr))

#define radians(deg) ((deg) * PI / 180.0f)

class __FlashStringHelper;
#define F(s) ((const __FlashStringHelper *)(s))

// Just enough of String for Adafruit GFX's text bounds of a String (unused by the firmware's drawing code)
class String {
public:
    String(const char *s = "") : s_(s) {}
    unsigned length() const { return (unsigned)strlen(s_); }
    const char *c_str() const { return s_; }
private:
    const char *s_;
};

typedef bool boolean;
typedef uint8_t byte;

using std::min;
using std::max;
template <typename T, typename L, typename H> T constrain(T v, L lo, H hi) { return v < lo ? (T)lo : v > hi ? (T)hi : v; }

// Deterministic, so the pictures come out the same every time (hostSeed() restarts the sequence)
void hostSeed(unsigned long seed);
long random(long howBig);
long random(long howSmall, long howBig);
inline void randomSeed(unsigned long seed) { hostSeed(seed); }
inline int analogRead(int) { return 0; }
unsigned long millis();
extern unsigned long hostMillis;   // what millis() returns

#endif
