#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#pragma message("__cplusplus: " TOSTRING(__cplusplus))

#include "EspUsbHostKeybord.h"
#include <ranges>

class MyEspUsbHostKeybord : public EspUsbHostKeybord
{
public:
  void onKey(usb_transfer_t *transfer)
  {
    uint8_t *const p = transfer->data_buffer;
    Serial.printf("onKey %02x %02x %02x %02x %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  };
};

MyEspUsbHostKeybord usbHost;

using namespace std::views;

void setup()
{
  Serial.begin(115200);
  usbHost.begin();
}

void loop()
{
  usbHost.task();

  auto squares = iota(0) |
                 transform([](int x)
                           { return x * x; }) |
                 take(10);

  for (int val : squares)
  {
    Serial.print(val);
    Serial.print(" ");
  }
  Serial.println(); // Neue Zeile am Ende
}
