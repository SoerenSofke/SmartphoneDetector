#include "UsbMidi.h"

UsbMidi usbMidi;

void tsprint(const char* msg) {
    unsigned long ms = millis();
    unsigned long s  = ms / 1000;
    unsigned long m  = s / 60;
    unsigned long h  = m / 60;
    Serial.printf("[%02lu:%02lu:%02lu.%03lu] %s\r\n",
                  h % 100, m % 60, s % 60, ms % 1000, msg);
}

void onMidiMessage(const uint8_t (&data)[4]) {
    tsprint("MIDI Message Received");
}

void onDeviceConnect() {
    tsprint("MIDI Device Connected");
}

void onDeviceDisconnected() {
    tsprint("MIDI Device Disconnected");
}

void setup() {
    Serial.begin(115200);
    delay(2000);        

    usbMidi.onMidiMessage(onMidiMessage);
    usbMidi.onDeviceConnected(onDeviceConnect);
    usbMidi.onDeviceDisconnected(onDeviceDisconnected);
    usbMidi.begin();
}

void loop() {
    usbMidi.update();
}