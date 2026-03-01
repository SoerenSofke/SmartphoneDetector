
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#pragma message("__cplusplus: " TOSTRING(__cplusplus))


#define LED_ON  LOW
#define LED_OFF HIGH

#include<KeyboardController.h>

USBHost usb;

KeyboardController keyboard(usb);

const int ledPin = 13;

void setup()
{
  pinMode(ledPin, OUTPUT);
}

void keyPressed()
{
  digitalWrite(ledPin, LED_ON);
}

void keyReleased()
{
  digitalWrite(ledPin, LED_OFF);
}

void loop()
{
  usb.Task();
}