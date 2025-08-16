#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Core-Index aktualisieren
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# ESP32 Core installieren
arduino-cli core install esp32:esp32@2.0.17

# Bibliotheken installieren
arduino-cli lib install "ESP32-USB-Soft-Host"
arduino-cli lib install "Adafruit NeoPixel"
