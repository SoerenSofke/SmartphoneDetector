#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Core-Index aktualisieren
./bin/arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# ESP32 Core installieren
./bin/arduino-cli core search --all
./bin/arduino-cli core install esp32:esp32@3.3.3

# Bibliotheken suchen
./bin/arduino-cli lib search "ESP32_USB_STREAM"

# Bibliotheken installieren
./bin/arduino-cli lib install ESP32_USB_STREAM@0.1.0

# Bibliotheken auflisten
./bin/arduino-cli lib install
