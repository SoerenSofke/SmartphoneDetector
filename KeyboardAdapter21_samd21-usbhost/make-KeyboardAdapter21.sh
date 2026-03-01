#!/bin/bash

./bin/arduino-cli compile --output-dir . --fqbn Seeeduino:samd:seeed_XIAO_m0 KeyboardAdapter21 && \
python3 uf2conv.py -c -o KeyboardAdapter21.uf2 KeyboardAdapter21.ino.bin && \
rm KeyboardAdapter21.ino*

