#!/bin/bash

./bin/arduino-cli compile \
    --output-dir . \
    --fqbn esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=custom \
    --build-property "upload.maximum_size=8323072" \
    --build-property "compiler.cpp.extra_flags=-Wa,-I{build.source.path}" \
    --build-property "compiler.c.extra_flags=-Wa,-I{build.source.path}" \
    Synth
rm Synth.ino.bin
rm Synth.ino.bootloader.bin
rm Synth.ino.elf
rm Synth.ino.map
rm Synth.ino.partitions.bin