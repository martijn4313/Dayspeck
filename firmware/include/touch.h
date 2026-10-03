// Dayspeck Bedside Display — Touch Input Declarations
// GPIO3 (RX pin) capacitive touch sensor handler

#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>

// Touch event types
#define TOUCH_NONE      0
#define TOUCH_SHORT     1
#define TOUCH_LONG      2

// Initialize touch sensor on GPIO3 (RX pin)
void touch_init();

// Call every loop iteration to update touch state
void touch_update();

// Get the pending touch event (TOUCH_NONE/SHORT/LONG) and clear it.
// Call once per loop iteration and act on the returned value: every call consumes the event.
int touch_get_event();

#endif // TOUCH_H
