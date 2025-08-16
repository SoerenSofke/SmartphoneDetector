#!/bin/bash

arduino-cli compile --output-dir . --fqbn adafruit:samd:adafruit_feather_m0_express KeyboardAdapter21

python3 uf2conv.py -c -o KeyboardAdapter21.uf2 KeyboardAdapter21.ino.bin
