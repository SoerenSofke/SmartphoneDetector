#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#pragma message("__cplusplus: " TOSTRING(__cplusplus))

#include <Adafruit_NeoPixel.h>
#include <KeyboardController.h>

Adafruit_NeoPixel pixels(1, PIN_NEOPIXEL);
USBHost usb;

KeyboardController keyboard(usb);

void setup(){  
  pixels.begin();
}

void keyPressed(){    
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));    
    pixels.show();
  
    delay(10);
    
    pixels.clear();
    pixels.show();
}

void keyReleased() {
    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    pixels.show();
  
    delay(10);
    
    pixels.clear();
    pixels.show();
}

void loop(){
  usb.Task();
}