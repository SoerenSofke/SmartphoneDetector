#!/bin/bash

arduino-cli compile --output-dir . --fqbn esp32:esp32:esp32s2 --build-property "build.extra_flags=-DARDUINO_USB_MODE=0 -DESP32S2" KeyboardAdapter21

# --build-property "build.extra_flags=-DARDUINO_USB_MODE=1 -DESP32S2"

esptool --chip esp32s2 merge-bin -o KeyboardAdapter21.merged.bin \
  --flash-size 4MB \
  0x1000 KeyboardAdapter21.ino.bootloader.bin \
  0x8000 KeyboardAdapter21.ino.partitions.bin \
  0x10000 KeyboardAdapter21.ino.bin
