#!/bin/sh
# Build for the NanoKVM (riscv64) and copy it over.  Needs: brew install zig
# include/ holds the Mario game engine + art, copied from the ESP32-035 CYD repo's mario-arcade example.
cd "$(dirname "$0")"
zig c++ -target riscv64-linux-musl -std=c++17 -O2 -static -s -fno-exceptions -fno-rtti -Iinclude -I. oled_mario.cpp -o oled_mario || exit 1
if [ "$1" = "deploy" ]; then scp oled_mario oled-on.sh S99oledmario "${KVM:-root@192.168.0.222}":/root/; fi
