#!/bin/sh
# Build clip_server for the NanoKVM (riscv64) and copy it over.  Needs: brew install zig
# ./build.sh deploy also copies the S98clipserver init script.
cd "$(dirname "$0")"
zig c++ -target riscv64-linux-musl -std=c++17 -O2 -static -s -fno-exceptions -fno-rtti clip_server.cpp -o clip_server || exit 1
if [ "$1" = "deploy" ]; then scp clip_server S98clipserver "${KVM:-root@192.168.0.222}":/root/; fi
