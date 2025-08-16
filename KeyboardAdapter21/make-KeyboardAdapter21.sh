#!/bin/bash

arduino-cli compile --output-dir . --fqbn esp32:esp32:esp32s2 --build-property "build.extra_flags=-DARDUINO_USB_MODE=0 -DESP32S2" KeyboardAdapter21