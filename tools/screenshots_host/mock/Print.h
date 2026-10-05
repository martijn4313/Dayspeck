// Host stand-in for Arduino's Print: text goes through write(), like on the device
#ifndef HOST_PRINT_H
#define HOST_PRINT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    size_t write(const char *s) { size_t n = 0; while (*s) n += write((uint8_t)*s++); return n; }
    size_t print(const char *s) { return write(s); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { char b[16]; snprintf(b, sizeof(b), "%d", v); return write(b); }
    size_t print(unsigned v) { char b[16]; snprintf(b, sizeof(b), "%u", v); return write(b); }
    size_t print(long v) { char b[24]; snprintf(b, sizeof(b), "%ld", v); return write(b); }
    size_t print(double v, int digits = 2) { char b[32]; snprintf(b, sizeof(b), "%.*f", digits, v); return write(b); }
};

#endif
