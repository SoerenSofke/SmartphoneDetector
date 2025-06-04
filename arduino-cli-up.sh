#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Verzeichnis erstellen
mkdir -p /home/codespace/.arduino15

# Konfigurationsdatei verlinken
ln -s /workspaces/SmartphoneDetector/arduino-cli.yaml /home/codespace/.arduino15/arduino-cli.yaml

# Core-Index aktualisieren
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# ESP32 Core installieren
arduino-cli core install esp32:esp32

# Bibliotheken installieren
arduino-cli lib install "Adafruit NeoPixel"
arduino-cli lib install "NimBLE-Arduino"
arduino-cli lib install "ESP32-BLE-Gamepad"
