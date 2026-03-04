#define LED_ON LOW
#define LED_OFF HIGH

#define KEY_ENTER       0x28
#define KEY_BACKSPACE   0x2A

#define KEY_A           0x04
#define KEY_B           0x05
#define KEY_C           0x06
#define KEY_D           0x07
#define KEY_E           0x08
#define KEY_F           0x09
#define KEY_G           0x0A
#define KEY_H           0x0B
#define KEY_I           0x0C
#define KEY_J           0x0D
#define KEY_K           0x0E
#define KEY_L           0x0F
#define KEY_M           0x10
#define KEY_N           0x11
#define KEY_O           0x12
#define KEY_P           0x13
#define KEY_Q           0x14
#define KEY_R           0x15
#define KEY_S           0x16
#define KEY_T           0x17
#define KEY_U           0x18
#define KEY_V           0x19
#define KEY_W           0x1A
#define KEY_X           0x1B
#define KEY_Y           0x1C
#define KEY_Z           0x1D

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
    layer[110] = KEY_J; layer[103] = KEY_L; layer[102] = KEY_U; layer[57] = KEY_Y; layer[56] = KEY_A; layer[49] = KEY_BACKSPACE;
    layer[109] = KEY_H; layer[104] = KEY_N; layer[101] = KEY_E; layer[48] = KEY_I; layer[55] = KEY_O; layer[50] = KEY_ENTER;
    layer[108] = KEY_K; layer[105] = KEY_M; layer[100] = KEY_A; layer[97] = KEY_A; layer[54] = KEY_A; layer[51] = KEY_A;
    layer[107] = KEY_A; layer[106] = KEY_A; layer[ 99] = KEY_A; layer[98] = KEY_A; layer[53] = KEY_A; layer[52] = KEY_A;
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
  // hid.typeString(buf);

  byte keys[6] = {layer0[idx], 0, 0, 0, 0, 0};
  hid.sendKeyPress(keys, 0);
}

void keyReleased()
{
  byte noKeysPressed[6] = {0, 0, 0, 0, 0, 0};
  hid.sendKeyPress(noKeysPressed, 0);
}

void loop()
{
  usb.Task();
  digitalWrite(ledPin, usb.getUsbTaskState() == USB_STATE_RUNNING ? LED_ON : LED_OFF);
}