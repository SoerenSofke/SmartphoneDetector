#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh


# Core-Index aktualisieren
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json


# ESP32 Core installieren
arduino-cli core search --all
arduino-cli core install esp32:esp32@3.3.0


# Bibliotheken installieren
arduino-cli lib install "EspUsbHost"