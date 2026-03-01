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
  while (!Serial1)
    delay(100);
  hid.begin(&Serial1);
}

void keyPressed()
{
  char buf[2] = {(char)keyboard.getKey(), '\0'};
  hid.typeString(buf);
}

void keyReleased()
{
}

void loop()
{
  usb.Task();
  digitalWrite(ledPin, usb.getUsbTaskState() == USB_STATE_RUNNING ? LED_ON : LED_OFF);
}