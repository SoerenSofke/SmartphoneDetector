#include <Arduino.h>
#include <BleGamepad.h>
#include <Adafruit_NeoPixel.h> // green / red / blue

#define PIN_NEOPIXEL 14
#define NUMPIXELS 64
#define RGB_BRIGHTNESS 10

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN_NEOPIXEL, NEO_RGB + NEO_KHZ800);

BleGamepad bleGamepad;

void setPixels(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < NUMPIXELS; i++)
    {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
    pixels.show();
}

void setup()
{
    bleGamepad.begin();
}

bool is_connected = false;

void loop()
{
    for (int i = 0; i < 20; i++)
    {
        setPixels(0, 0, RGB_BRIGHTNESS);
        delay(1000);

        if (bleGamepad.isConnected())
        {
            is_connected = true;
            break;
        }
    }

    if (is_connected)
    {
        setPixels(0, RGB_BRIGHTNESS, 0);
        delay(10000);
        setPixels(0, 0, 0);
    }
    else
    {
        for (int i = 0; i < 60; i++)
        {
            setPixels(RGB_BRIGHTNESS, 0, 0);
            delay(500);
            setPixels(0, 0, 0);
            delay(500);
        }
    }

    while (true)
    {
        delay(10000);
    }
}