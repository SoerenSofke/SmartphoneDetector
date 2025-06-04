#include <Adafruit_NeoPixel.h>


#define PIN_NEOPIXEL 14
#define RGB_BRIGHTNESS 10

void setup() {
}

void loop() {
  neopixelWrite(PIN_NEOPIXEL, RGB_BRIGHTNESS, 0, 0);
  delay(1000);
  neopixelWrite(PIN_NEOPIXEL, 0, RGB_BRIGHTNESS, 0);
  delay(1000);
  neopixelWrite(PIN_NEOPIXEL, 0, 0, RGB_BRIGHTNESS);
  delay(1000);
  neopixelWrite(PIN_NEOPIXEL, 0, 0, 0);
  delay(1000);
}