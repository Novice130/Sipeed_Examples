#!/bin/sh
# Build flappy_server for the NanoKVM (riscv64) and copy it over.  Needs: brew install zig
# ./build.sh deploy also copies the S97flappy init script.
# Mac preview without hardware:  clang++ -std=c++17 -O2 flappy_server.cpp -o flappy_sim && ./flappy_sim --sim out 900 30
cd "$(dirname "$0")"
zig c++ -target riscv64-linux-musl -std=c++17 -O2 -static -s -fno-exceptions -fno-rtti flappy_server.cpp -o flappy_server || exit 1
if [ "$1" = "deploy" ]; then scp flappy_server S97flappy "${KVM:-root@192.168.0.222}":/root/; fi
