#!/bin/bash

# Arduino CLI installieren
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh


# SAMD21 Core installieren
./bin/arduino-cli core search --all
./bin/arduino-cli core install arduino:samd@1.8.14 


# Bibliotheken installieren
./bin/arduino-cli lib search USBHost --verbose
./bin/arduino-cli lib install "USBHost@1.0.5"

