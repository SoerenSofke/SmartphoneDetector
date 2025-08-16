#!/bin/bash

arduino-cli compile --output-dir . --fqbn esp32:esp32:esp32s3 --build-property "build.extra_flags=-DARDUINO_USB_MODE=0" KeyboardAdapter21