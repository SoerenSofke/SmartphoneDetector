#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Board informartion: https://www.waveshare.com/wiki/ESP32-P4-Pico
./bin/arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# Index aktualisieren und Paket installieren
./bin/arduino-cli core update-index
./bin/arduino-cli core install esp32:esp32@3.3.7

# search ESP32-P4 (Pico) boards: ESP32P4 Dev Module: esp32:esp32:esp32p4
./bin/arduino-cli board listall esp32:esp32:esp32p4

# Compile ardunio scatch for ESP32-P4
./bin/arduino-cli compile --output-dir . --fqbn esp32:esp32:esp32p4 Synth


# Upload the compiled sketch to the ESP32-P4 via NixOS
# nix-shell -p esptool tio
#[nix-shell:/dev]$ esptool --chip esp32p4 --port /dev/ttyACM0 write-flash 0x0 /home/sofke/Downloads/Synth.ino.merged.bin
#[nix-shell:/dev]$ esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0x0 /home/sofke/Downloads/Synth.ino.merged.bin
#[nix-shell:/dev]$ tio /dev/ttyACM0