#include "UsbMidi.h"

UsbMidi usbMidi;

void onMidiMessage(const uint8_t (&data)[4]) {
    Serial.println("MIDI Message Received");
}

void onDeviceConnect() {
    Serial.println("MIDI Device Connected");
}

void onDeviceDisconnected() {
    Serial.println("MIDI Device Disconnected");
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