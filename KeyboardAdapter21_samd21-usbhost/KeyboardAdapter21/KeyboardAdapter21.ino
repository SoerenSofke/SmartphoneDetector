#include <KeyboardController.h>
#include <Adafruit_CH9328.h>

#define LED_ON LOW
#define LED_OFF HIGH

// clang-format off
// Negative values in the layer table encode "modifier bitmask"
#define ML_CTRL      ((int16_t)-0x01)
#define ML_SHIFT     ((int16_t)-0x02)
#define ML_ALT       ((int16_t)-0x04)
#define ML_GUI       ((int16_t)-0x08)
#define MR_CTRL      ((int16_t)-0x10)
#define MR_SHIFT     ((int16_t)-0x20)
#define MR_ALT       ((int16_t)-0x40)
#define MR_GUI       ((int16_t)-0x80)

#define C_ENTER       0x28
#define C_ESCAPE      0x29
#define C_BACKSPACE   0x2A
#define C_TAB         0x2B
#define C_SPACE       0x2C

#define C_UP          0x52
#define C_DOWN        0x51
#define C_LEFT        0x50
#define C_RIGHT       0x4F

#define C_INSERT      0x49
#define C_DELETE      0x4C
#define C_HOME        0x4A
#define C_END         0x4D
#define C_PAGE_UP     0x4B
#define C_PAGE_DOWN   0x4E

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

Adafruit_CH9328 hid;

USBHost usb;
KeyboardController keyboard(usb);

enum class DeviceRole
{
  CONTROLLER, // Pin D3 → GND  (LOW)
  PERIPHERAL  // Pin D3 → open (HIGH, Pull-up)
};

DeviceRole deviceRole;

constexpr uint8_t LED_PIN = 13;
const int16_t *layer0 = nullptr;

uint8_t pressedKeys[6] = {0, 0, 0, 0, 0, 0};
uint8_t modifiers = 0;

struct LedPulser
{
  uint8_t pin;
  bool active;
  bool baseState;
  unsigned long startMs;
  unsigned long durationMs;

  void begin(uint8_t ledPin, unsigned long delayMs = 50)
  {
    if (delayMs == 0)
      delayMs = 1;

    pin = ledPin;
    active = false;
    startMs = 0;
    durationMs = delayMs;

    pinMode(pin, OUTPUT);
  }

  void update(bool idleState)
  {
    baseState = idleState;

    unsigned long now = millis();
    if ((unsigned long)(now - startMs) >= durationMs)
    {
      active = false;
    }

    digitalWrite(pin, active ? !baseState : baseState);
  }

  void startPulse()
  {
    startMs = millis();
    active = true;
  }
};

LedPulser ledPulser;

inline const int16_t *getLayer0()
{
  static int16_t layer[111];
  static bool initialized = false;

  if (!initialized)
  {
    memset(layer, 0x00, sizeof(layer));

    // clang-format off
    layer[110] = KEY_J;   layer[103] = KEY_L;   layer[102] = KEY_U;   layer[57] = KEY_Y;  layer[56] = KEY_A;  layer[49] = C_BACKSPACE;
    layer[109] = KEY_H;   layer[104] = KEY_N;   layer[101] = KEY_E;   layer[48] = KEY_I;  layer[55] = KEY_O;  layer[50] = C_ENTER;
    layer[108] = KEY_K;   layer[105] = KEY_M;   layer[100] = KEY_A;   layer[97] = KEY_A;  layer[54] = C_UP;   layer[51] = MR_SHIFT;
    layer[107] = C_SPACE; layer[106] = C_SPACE; layer[ 99] = C_SPACE; layer[98] = C_LEFT; layer[53] = C_DOWN; layer[52] = C_RIGHT;
    // clang-format on

    initialized = true;
  }

  return layer;
}

void setup()
{
  ledPulser.begin(LED_PIN, 20);
  ledPulser.update(LED_OFF);

  // Read device role from pin D3
  pinMode(D3, INPUT_PULLUP);
  delay(1); // Give pull-up time to charge
  deviceRole = (digitalRead(D3) == LOW) ? DeviceRole::CONTROLLER : DeviceRole::PERIPHERAL;

  Serial1.begin(9600);
  delay(2000); // Give CH9328 time to boot
  hid.begin(&Serial1);

  layer0 = getLayer0();
}

int16_t getCode()
{
  int keyCode = keyboard.getKey();
  if (keyCode == 0)
    return 0;

  if (layer0 == nullptr)
    return 0;

  int idx = keyCode;
  if (idx < 0 || idx > 110)
    return 0;

  return layer0[idx];
}

void pressKey(uint8_t key)
{
  for (int i = 0; i < 6; i++)
  {
    if (pressedKeys[i] == key)
      return;
  }
  for (int i = 0; i < 6; i++)
  {
    if (pressedKeys[i] == 0)
    {
      pressedKeys[i] = key;
      return;
    }
  }
}

void releaseKey(uint8_t key)
{
  for (int i = 0; i < 6; i++)
  {
    if (pressedKeys[i] == key)
    {
      for (int j = i; j < 5; j++)
        pressedKeys[j] = pressedKeys[j + 1];
      pressedKeys[5] = 0;
      return;
    }
  }
}

void keyPressedController()
{
  int16_t code = getCode();
  if (code == 0)
    return;

  if (code < 0)
    modifiers |= (uint8_t)(-code);
  else
    pressKey((uint8_t)code);

  hid.sendKeyPress(pressedKeys, modifiers);
  ledPulser.startPulse();
}

void keyPressedPeripheral()
{
  int keyCode = keyboard.getKey();
  if (keyCode == 0)
    return;

  // Send positive key code (encoded as int8_t) to signal key press
  Serial1.write((uint8_t)(int8_t)(keyCode)); // 1 to 127
  ledPulser.startPulse();
}

// KeyboardController.h: Callback for key press event on physical keyboard connected to USB host
void keyPressed()
{
  (deviceRole == DeviceRole::CONTROLLER) ? keyPressedController() : keyPressedPeripheral();
}

void keyReleasedController()
{
  int16_t code = getCode();
  if (code == 0)
    return;

  if (code < 0)
    modifiers &= ~((uint8_t)(-code));
  else
    releaseKey((uint8_t)code);

  hid.sendKeyPress(pressedKeys, modifiers);
  ledPulser.startPulse();
}

void keyReleasedPeripheral()
{
  int keyCode = keyboard.getKey();
  if (keyCode == 0)
    return;

  // Send negative key code (encoded as int8_t) to signal key release
  Serial1.write((uint8_t)(int8_t)(-keyCode)); // -1 to -127
  ledPulser.startPulse();
}

// KeyboardController.h: Callback for key release event on physical keyboard connected to USB host
void keyReleased()
{
  (deviceRole == DeviceRole::CONTROLLER) ? keyReleasedController() : keyReleasedPeripheral();
}

void loop()
{
  usb.Task();
  ledPulser.update(usb.getUsbTaskState() == USB_STATE_RUNNING ? LED_ON : LED_OFF);

  if (deviceRole == DeviceRole::CONTROLLER && Serial1.available() > 0)
  {
    int b = Serial1.read();
    (void)b;
    ledPulser.startPulse();
  }
}