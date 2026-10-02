# Hardware

Designed for a bare ESP-01 and a 0.96" SSD1306 module, wired as below. Cheap "mini weather clock" DIY
soldering kits (an ESP8266 board, a 0.96" OLED and an acrylic case, a few euros on AliExpress) use
the same chip family and the same kind of display, so they are good candidates. **They are untested
with this firmware.** Check three things before you flash one:

- the display is an SSD1306 at address `0x3C` (some 0.96" modules use an SH1106 or a different address);
- which GPIOs the OLED and the button or touch pad use: set `OLED_SDA`, `OLED_SCL` and `TOUCH_PIN` in
  `firmware/include/config.h`;
- the flash size: the `esp01_1m` environment assumes 1 MB, so a board with 4 MB (NodeMCU, ESP-12F)
  needs its own environment in `platformio.ini` (`board = esp12e` or `nodemcuv2`).

A kit without a touch pad or button can use any momentary switch to ground on a free GPIO.

| Part | Notes |
|------|-------|
| ESP-01 (ESP8266, 1 MB flash) | the `esp01_1m` board in PlatformIO |
| SSD1306 128×64 I²C OLED, address `0x3C` | |
| Touch input | a TTP223 module or a push button |
| 3.3 V supply, ≥ 300 mA | the ESP8266 draws current spikes when transmitting |

Wiring (all 3.3 V):

| ESP-01 pin | GPIO | Connects to |
|-----------|------|-------------|
| 0 | GPIO0 | OLED **SDA** |
| 2 | GPIO2 | OLED **SCL** |
| RX | GPIO3 | touch sensor output |

Notes:

- GPIO0 and GPIO2 are boot-strap pins and must be **high at power-up**. The OLED's I²C pull-ups
  normally do that; do not hold either low.
- GPIO3 is the UART RX pin, so **serial output and serial flashing are unavailable while the touch
  sensor is attached**. All diagnostics go to the log in the web UI instead. Disconnect the sensor
  while flashing over serial.
- Touch polarity is set in `firmware/include/config.h`: `TOUCH_ACTIVE_HIGH 0` (default) expects the
  pin to be pulled **low** when touched (button to ground, internal pull-up). Set it to `1` for
  modules such as the TTP223 that drive the pin **high**.
