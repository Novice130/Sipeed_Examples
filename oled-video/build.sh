#!/bin/sh
# Build the OLED video player for the NanoKVM (riscv64) and copy it over.  Needs: brew install zig
cd "$(dirname "$0")"
zig c++ -target riscv64-linux-musl -std=c++17 -O2 -static -s -fno-exceptions -fno-rtti oled_video.cpp -o oled_video || exit 1
if [ "$1" = "deploy" ]; then scp oled_video "${KVM:-root@192.168.0.222}":/root/; fi
