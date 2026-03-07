#define LED_ON LOW
#define LED_OFF HIGH

// clang-format off
#define MOD_L_CTRL    -0x01
#define MOD_L_SHIFT   -0x02
#define MOD_L_ALT     -0x04
#define MOD_L_GUI     -0x08
#define MOD_R_CTRL    -0x10
#define MOD_R_SHIFT   -0x20
#define MOD_R_ALT     -0x40
#define MOD_R_GUI     -0x80

#define KEY_ENTER     0x28
#define KEY_BACKSPACE 0x2A

#define KEY_A         0x04
#define KEY_B         0x05
#define KEY_C         0x06
#define KEY_D         0x07
#define KEY_E         0x08
#define KEY_F         0x09
#define KEY_G         0x0A
#define KEY_H         0x0B
#define KEY_I         0x0C
#define KEY_J         0x0D
#define KEY_K         0x0E
#define KEY_L         0x0F
#define KEY_M         0x10
#define KEY_N         0x11
#define KEY_O         0x12
#define KEY_P         0x13
#define KEY_Q         0x14
#define KEY_R         0x15
#define KEY_S         0x16
#define KEY_T         0x17
#define KEY_U         0x18
#define KEY_V         0x19
#define KEY_W         0x1A
#define KEY_X         0x1B
#define KEY_Y         0x1C
#define KEY_Z         0x1D
// clang-format on

#include <KeyboardController.h>
#include <Adafruit_CH9328.h>

Adafruit_CH9328 hid;

USBHost usb;
KeyboardController keyboard(usb);

const int ledPin = 13;
const int16_t *layer0 = nullptr;

byte pressedKeys[6] = {0, 0, 0, 0, 0, 0};
byte modifiers = 0;

inline const int16_t *getLayer0()
{
  static int16_t layer[111];
  static bool initialized = false;

  if (!initialized)
  {
    memset(layer, 0x00, sizeof(layer));

    // clang-format off
    layer[110] = KEY_J; layer[103] = KEY_L; layer[102] = KEY_U; layer[57] = KEY_Y; layer[56] = KEY_A; layer[49] = KEY_BACKSPACE;
    layer[109] = KEY_H; layer[104] = KEY_N; layer[101] = KEY_E; layer[48] = KEY_I; layer[55] = KEY_O; layer[50] = KEY_ENTER;
    layer[108] = KEY_K; layer[105] = KEY_M; layer[100] = KEY_A; layer[97] = KEY_A; layer[54] = KEY_A; layer[51] = MOD_R_SHIFT;
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
  char keyCode = keyboard.getKey();
  if (keyCode == '\0')
    return;

  int idx = (int)keyCode;
  if (idx < 0 || idx > 110)
    return;

  int16_t code = layer0[idx];

  if (code < 0)
  {
    modifiers |= (byte)(-code);
  }
  else
  {
    byte key = (byte)code;

    bool alreadyPressed = false;
    for (int i = 0; i < 6; i++)
    {
      if (pressedKeys[i] == key)
      {
        alreadyPressed = true;
        break;
      }
    }

    if (!alreadyPressed)
    {
      for (int i = 0; i < 6; i++)
      {
        if (pressedKeys[i] == 0)
        {
          pressedKeys[i] = key;
          break;
        }
      }
    }
  }

  hid.sendKeyPress(pressedKeys, modifiers);
}

void keyReleased()
{
  char keyCode = keyboard.getKey();

  if (keyCode != '\0')
  {
    int idx = (int)keyCode;
    if (idx >= 0 && idx <= 110)
    {
      int16_t code = layer0[idx];

      if (code < 0)
      {
        modifiers &= ~((byte)(-code));
      }
      else
      {
        byte key = (byte)code;

        for (int i = 0; i < 6; i++)
        {
          if (pressedKeys[i] == key)
          {
            for (int j = i; j < 5; j++)
              pressedKeys[j] = pressedKeys[j + 1];
            pressedKeys[5] = 0;
            break;
          }
        }
      }
    }
  }

  hid.sendKeyPress(pressedKeys, modifiers);
}

void loop()
{
  usb.Task();
  digitalWrite(ledPin, usb.getUsbTaskState() == USB_STATE_RUNNING ? LED_ON : LED_OFF);
}