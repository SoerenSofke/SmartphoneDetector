#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh


# Seeed SAMD Boards zur config hinzufügen
./bin/arduino-cli config add board_manager.additional_urls \
  https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json


# Index aktualisieren und Paket installieren
./bin/arduino-cli core update-index
./bin/arduino-cli core install Seeeduino:samd


# SAMD21 Core installieren
./bin/arduino-cli core search --all
./bin/arduino-cli core install Seeeduino:samd@1.8.5 


# Bibliotheken installieren
./bin/arduino-cli lib search USBHost --verbose
./bin/arduino-cli lib install "USBHost@1.0.5"

