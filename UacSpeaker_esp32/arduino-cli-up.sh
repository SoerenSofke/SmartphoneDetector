#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Core-Index aktualisieren
./bin/arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# ESP32 Core installieren
./bin/arduino-cli core search --all
## 2.0.x is required for ESP32_USB_STREAM@0.1.0 (esp32:esp32@3.x is not supported for now by library)
./bin/arduino-cli core install esp32:esp32@2.0.17
## pyserial is required by esp32:esp32@2.x
python3 -m pip install pyserial

# Bibliotheken suchen
./bin/arduino-cli lib search "ESP32_USB_STREAM"

# Bibliotheken installieren
./bin/arduino-cli lib install ESP32_USB_STREAM@0.1.0

# Bibliotheken auflisten
./bin/arduino-cli lib list
