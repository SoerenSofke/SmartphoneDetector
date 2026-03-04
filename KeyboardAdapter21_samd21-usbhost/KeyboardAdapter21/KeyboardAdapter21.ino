#define LED_ON LOW
#define LED_OFF HIGH

#include <KeyboardController.h>
#include <Adafruit_CH9328.h>

Adafruit_CH9328 hid;

USBHost usb;
KeyboardController keyboard(usb);

const int ledPin = 13;

const uint8_t *layer0 = nullptr;

inline const uint8_t *getLayer0()
{
  static uint8_t layer[111];
  static bool initialized = false;

  if (!initialized)
  {
    memset(layer, 0x00, sizeof(layer));

    // clang-format off
    layer[110] = 0;  layer[103] = 1;  layer[102] = 2;  layer[57] = 3;  layer[56] = 4;  layer[49] = 5;
    layer[109] = 6;  layer[104] = 7;  layer[101] = 8;  layer[48] = 9;  layer[55] = 10; layer[50] = 11;
    layer[108] = 12; layer[105] = 13; layer[100] = 14; layer[97] = 15; layer[54] = 16; layer[51] = 17;
    layer[107] = 18; layer[106] = 19; layer[ 99] = 20; layer[98] = 21; layer[53] = 22; layer[52] = 23;
    // clang-format on

    initialized = true;
  }

  return layer;
}

void setup()
{
  pinMode(ledPin, OUTPUT);

  Serial1.begin(9600);
  while (!Serial1)
    delay(100);
  hid.begin(&Serial1);

  layer0 = getLayer0();
}

void keyPressed()
{
  char key = keyboard.getKey();

  if (key == '\0')
    return;

  int idx = (int)key;

  if (idx < 0 || idx > 110)
    return;

  char buf[12];
  snprintf(buf, sizeof(buf), "%d", layer0[idx]);
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