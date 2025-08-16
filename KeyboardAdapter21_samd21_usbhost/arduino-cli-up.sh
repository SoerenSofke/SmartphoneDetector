#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Core-Index aktualisieren
arduino-cli core update-index --additional-urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json

# ESP32 Core installieren
arduino-cli core install adafruit:samd@1.7.16 --additional-urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json


# Bibliotheken installieren
arduino-cli lib install "Adafruit NeoPixel"
arduino-cli lib install "USBHost"
