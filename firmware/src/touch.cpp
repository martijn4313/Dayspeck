// MotoWeather Bedside Display — Touch Input Implementation
// GPIO3 (RX pin) capacitive touch sensor handler

#include "touch.h"
#include "config.h"

// Touch state machine
static int touchState = TOUCH_NONE;
static bool touchPressed = false;
static unsigned long touchStartMs = 0;
static unsigned long lastDebounceMs = 0;
static bool touchHandled = false;  // prevent multiple fires

// Initialize touch sensor on GPIO3 (RX pin)
void touch_init() {
#if TOUCH_ACTIVE_HIGH
    pinMode(TOUCH_PIN, INPUT);
#else
    pinMode(TOUCH_PIN, INPUT_PULLUP);
#endif
    touchState = TOUCH_NONE;
    touchPressed = false;
    touchStartMs = 0;
    lastDebounceMs = 0;
    touchHandled = false;
}

// Call every loop iteration to update touch state
void touch_update() {
    unsigned long now = millis();
    
    // Read GPIO3; the polarity depends on the sensor (TOUCH_ACTIVE_HIGH in config.h)
    bool pressed = (digitalRead(TOUCH_PIN) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW));
    
    // Debounce check
    if (pressed != touchPressed) {
        if (now - lastDebounceMs > TOUCH_DEBOUNCE_MS) {
            touchPressed = pressed;
            lastDebounceMs = now;
            
            if (touchPressed) {
                // Touch just pressed
                touchStartMs = now;
                touchHandled = false;
                touchState = TOUCH_NONE;
            } else {
                // Touch just released - check if it was a short tap
                unsigned long pressDuration = now - touchStartMs;
                if (pressDuration < TOUCH_LONG_PRESS_MS && !touchHandled) {
                    touchState = TOUCH_SHORT;
                    touchHandled = true;
                }
            }
        }
    }
    
    // Check for long press while still held
    if (touchPressed && !touchHandled) {
        unsigned long pressDuration = now - touchStartMs;
        if (pressDuration >= TOUCH_LONG_PRESS_MS) {
            touchState = TOUCH_LONG;
            touchHandled = true;
        }
    }
}

// Return and consume current event
int touch_get_event() {
    int event = touchState;
    touchState = TOUCH_NONE;
    return event;
}
