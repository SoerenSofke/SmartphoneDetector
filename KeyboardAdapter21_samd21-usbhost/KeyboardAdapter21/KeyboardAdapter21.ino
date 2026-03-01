#define LED_ON LOW
#define LED_OFF HIGH

#include <KeyboardController.h>
#include <Adafruit_CH9328.h>

Adafruit_CH9328 hid;

USBHost usb;
KeyboardController keyboard(usb);

const int ledPin = 13;

void setup()
{
  pinMode(ledPin, OUTPUT);
  
  Serial1.begin(9600);
  while (!Serial1) delay(100);
  hid.begin(&Serial1);
}

void keyPressed()
{
  digitalWrite(ledPin, LED_ON);
  hid.typeString("a");
}

void keyReleased()
{
  digitalWrite(ledPin, LED_OFF);
}

void loop()
{
  usb.Task();
}