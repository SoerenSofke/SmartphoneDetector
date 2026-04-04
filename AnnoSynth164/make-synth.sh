#!/bin/bash

./bin/arduino-cli compile --output-dir . --fqbn esp32:esp32:esp32s3 Synth
rm Synth.ino.bin
rm Synth.ino.bootloader.bin
rm Synth.ino.elf
rm Synth.ino.map
rm Synth.ino.partitions.bin